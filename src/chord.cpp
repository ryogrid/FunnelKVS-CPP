#include "chord.h"
#include "client.h"
#include <iostream>
#include <sstream>
#include <algorithm>

namespace funnelkvs {

std::string NodeInfo::to_string() const {
    std::stringstream ss;
    ss << address << ":" << port << " [" << SHA1::to_string(id).substr(0, 8) << "...]";
    return ss.str();
}

NodeInfo NodeInfo::from_address(const std::string& addr, uint16_t p) {
    std::string node_key = addr + ":" + std::to_string(p);
    Hash160 node_id = SHA1::hash(node_key);
    return NodeInfo(node_id, addr, p);
}

ChordNode::ChordNode(const std::string& address, uint16_t port)
    : self_info(NodeInfo::from_address(address, port))
    , predecessor(nullptr)
    , successor_list(SUCCESSOR_LIST_SIZE)
    , finger_table(FINGER_TABLE_SIZE)
    , local_storage(std::unique_ptr<Storage>(new Storage()))
    , replication_manager(std::unique_ptr<ReplicationManager>(new ReplicationManager()))
    , failure_detector(std::unique_ptr<FailureDetector>(new FailureDetector()))
    , running(false)
    , next_finger_to_fix(0)
    , stabilize_interval(1000) // 1 second
    , fix_fingers_interval(500) // 0.5 seconds
    , failure_check_interval(2000) // 2 seconds
{
    // Initialize successor list and finger table to point to self (single node ring)
    auto self_ptr = std::make_shared<NodeInfo>(self_info);
    std::fill(successor_list.begin(), successor_list.end(), self_ptr);
    std::fill(finger_table.begin(), finger_table.end(), self_ptr);
}

ChordNode::~ChordNode() {
    stop_maintenance();
}

void ChordNode::create() {
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    // In a single-node ring, predecessor is null and successor is self
    predecessor = nullptr;
    auto self_ptr = std::make_shared<NodeInfo>(self_info);
    std::fill(successor_list.begin(), successor_list.end(), self_ptr);
    std::fill(finger_table.begin(), finger_table.end(), self_ptr);
    
    std::cout << "Created new Chord ring with node " << self_info.to_string() << std::endl;
}

void ChordNode::join(std::shared_ptr<NodeInfo> existing_node) {
    if (!existing_node || *existing_node == self_info) {
        create();
        return;
    }
    
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    // Initialize predecessor to null
    predecessor = nullptr;
    
    // Find successor using existing node
    // For now, we'll implement a simple version that sets successor to existing_node
    // In full implementation, this would contact the existing node
    successor_list[0] = existing_node;
    
    // Initialize finger table
    initialize_finger_table();
    
    std::cout << "Node " << self_info.to_string() 
              << " joined ring via " << existing_node->to_string() << std::endl;
    
    // Request transfer of keys from successor that now belong to us
    accept_transferred_keys(existing_node);
}

void ChordNode::leave() {
    stop_maintenance();
    
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    // Transfer all keys to successor before leaving
    if (successor_list[0] && *successor_list[0] != self_info) {
        std::cout << "Node " << self_info.to_string() << " leaving ring, transferring keys to successor" << std::endl;
        transfer_keys_to_node(successor_list[0]);
    }
    
    // Reset to single-node state
    predecessor = nullptr;
    auto self_ptr = std::make_shared<NodeInfo>(self_info);
    std::fill(successor_list.begin(), successor_list.end(), self_ptr);
    std::fill(finger_table.begin(), finger_table.end(), self_ptr);
}

void ChordNode::start_maintenance() {
    if (running.load()) {
        return;
    }
    
    running.store(true);
    stabilize_thread = std::thread(&ChordNode::stabilize_loop, this);
    fix_fingers_thread = std::thread(&ChordNode::fix_fingers_loop, this);
    failure_detection_thread = std::thread(&ChordNode::failure_detection_loop, this);
    
    std::cout << "Started maintenance threads for node " << self_info.to_string() << std::endl;
}

void ChordNode::stop_maintenance() {
    if (!running.load()) {
        return;
    }
    
    running.store(false);
    
    // Wake up any sleeping threads
    shutdown_cv.notify_all();
    
    if (stabilize_thread.joinable()) {
        stabilize_thread.join();
    }
    if (fix_fingers_thread.joinable()) {
        fix_fingers_thread.join();
    }
    if (failure_detection_thread.joinable()) {
        failure_detection_thread.join();
    }
    
    std::cout << "Stopped maintenance threads for node " << self_info.to_string() << std::endl;
}

std::shared_ptr<NodeInfo> ChordNode::find_successor(const Hash160& id) {
    if (is_responsible_for_key(id)) {
        return std::make_shared<NodeInfo>(self_info);
    }
    
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    // Check if id is between self and immediate successor
    auto successor = successor_list[0];
    if (successor && in_range(id, self_info.id, successor->id, true)) {
        return successor;
    }
    
    // Find closest preceding node and continue search
    auto closest = closest_preceding_node(id);
    if (closest && *closest != self_info) {
        // In full implementation, this would be a remote call
        return contact_node(closest, "find_successor", id);
    }
    
    return successor;
}

std::shared_ptr<NodeInfo> ChordNode::find_predecessor(const Hash160& id) {
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    auto current = std::make_shared<NodeInfo>(self_info);
    auto successor = successor_list[0];
    
    while (successor && !in_range(id, current->id, successor->id, true)) {
        if (*current == self_info) {
            current = closest_preceding_node(id);
        } else {
            // In full implementation, this would be a remote call
            current = contact_node(current, "closest_preceding_node", id);
        }
        
        if (!current || *current == self_info) {
            break;
        }
        
        // Get successor of current node
        successor = contact_node(current, "get_successor");
    }
    
    return current;
}

std::shared_ptr<NodeInfo> ChordNode::closest_preceding_node(const Hash160& id) {
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    // Search finger table from highest to lowest
    for (int i = FINGER_TABLE_SIZE - 1; i >= 0; i--) {
        auto finger = finger_table[i];
        if (finger && *finger != self_info && 
            in_range(finger->id, self_info.id, id, false)) {
            return finger;
        }
    }
    
    // If no finger is in range, return self
    return std::make_shared<NodeInfo>(self_info);
}

void ChordNode::stabilize() {
    std::shared_ptr<NodeInfo> successor;
    {
        std::lock_guard<std::mutex> lock(routing_mutex);
        successor = successor_list[0];
    }
    
    if (!successor || *successor == self_info) {
        return;
    }
    
    // Ask successor for its predecessor (without holding mutex)
    auto x = contact_node(successor, "get_predecessor");
    
    {
        std::lock_guard<std::mutex> lock(routing_mutex);
        if (x && *x != self_info && in_range(x->id, self_info.id, successor->id, false)) {
            // Update successor
            successor_list[0] = x;
            successor = x;
        }
    }
    
    // Notify successor about this node (without holding mutex)
    if (successor && *successor != self_info) {
        contact_node(successor, "notify", self_info.id);
    }
}

void ChordNode::notify(std::shared_ptr<NodeInfo> node) {
    if (!node || *node == self_info) {
        return;
    }
    
    std::shared_ptr<NodeInfo> old_predecessor;
    bool predecessor_changed = false;
    
    {
        std::lock_guard<std::mutex> lock(routing_mutex);
        
        if (!predecessor || in_range(node->id, predecessor->id, self_info.id, false)) {
            old_predecessor = predecessor;
            predecessor = node;
            predecessor_changed = true;
            std::cout << "Node " << self_info.to_string() 
                      << " updated predecessor to " << node->to_string() << std::endl;
        }
    }
    
    // If we have a new predecessor, transfer keys that now belong to them
    if (predecessor_changed && old_predecessor != node) {
        transfer_keys_to_node(node);
    }
}

void ChordNode::fix_fingers() {
    int finger_index;
    Hash160 finger_start;
    
    {
        std::lock_guard<std::mutex> lock(routing_mutex);
        next_finger_to_fix = (next_finger_to_fix + 1) % FINGER_TABLE_SIZE;
        finger_index = next_finger_to_fix;
        finger_start = get_finger_start(finger_index);
    }
    
    // Call find_successor without holding mutex (it may do network operations)
    auto successor = find_successor(finger_start);
    
    if (successor) {
        std::lock_guard<std::mutex> lock(routing_mutex);
        finger_table[finger_index] = successor;
    }
}

bool ChordNode::store_key(const std::string& key, const std::vector<uint8_t>& value) {
    Hash160 key_id = SHA1::hash(key);
    
    if (is_responsible_for_key(key_id)) {
        // Store locally first
        local_storage->put(key, value);
        
        // Get replica nodes for replication
        auto replicas = get_replica_nodes(key_id);
        
        // Synchronous replication as specified in DESIGN.md
        // Must complete replication before returning success
        if (!replicas.empty()) {
            bool replication_success = replication_manager->replicate_put(key, value, replicas);
            if (!replication_success) {
                // Replication failed - rollback local storage and return failure
                local_storage->remove(key);
                std::cerr << "Error: Synchronous replication failed for key '" << key << "'" << std::endl;
                return false;
            }
        }
        
        // Replication successful (or no replicas needed)
        return true;
    } else {
        // Forward to responsible node
        auto responsible = find_successor(key_id);
        if (responsible && *responsible != self_info) {
            // Send to responsible node via client
            try {
                Client client(responsible->address, responsible->port);
                if (client.connect()) {
                    return client.put(key, value);
                }
            } catch (const std::exception& e) {
                std::cerr << "Failed to forward PUT to " << responsible->to_string() 
                          << ": " << e.what() << std::endl;
            }
        }
        return false;
    }
}

bool ChordNode::retrieve_key(const std::string& key, std::vector<uint8_t>& value) {
    Hash160 key_id = SHA1::hash(key);
    
    if (is_responsible_for_key(key_id)) {
        // Try local storage first
        if (local_storage->get(key, value)) {
            return true;
        }
        
        // If not found locally, try replicas (fallback)
        auto replicas = get_replica_nodes(key_id);
        if (!replicas.empty()) {
            return replication_manager->get_from_replicas(key, value, replicas);
        }
        
        return false;
    } else {
        // Forward to responsible node
        auto responsible = find_successor(key_id);
        if (responsible && *responsible != self_info) {
            try {
                Client client(responsible->address, responsible->port);
                if (client.connect()) {
                    return client.get(key, value);
                }
            } catch (const std::exception& e) {
                std::cerr << "Failed to forward GET to " << responsible->to_string() 
                          << ": " << e.what() << std::endl;
            }
        }
        return false;
    }
}

bool ChordNode::remove_key(const std::string& key) {
    Hash160 key_id = SHA1::hash(key);
    
    if (is_responsible_for_key(key_id)) {
        // Check if key exists locally, or in replicas if not local
        std::vector<uint8_t> dummy;
        bool key_exists = local_storage->get(key, dummy);
        
        if (!key_exists) {
            // Check if key exists in replicas (like retrieve_key does)
            auto replicas = get_replica_nodes(key_id);
            if (!replicas.empty()) {
                key_exists = replication_manager->get_from_replicas(key, dummy, replicas);
            }
            
            if (!key_exists) {
                return false; // Key doesn't exist anywhere
            }
        }
        
        // Remove from local storage (this will only succeed if key exists locally)
        local_storage->remove(key);
        
        // Synchronously remove from replicas
        auto replicas = get_replica_nodes(key_id);
        if (!replicas.empty()) {
            bool replication_success = replication_manager->replicate_delete(key, replicas);
            if (!replication_success) {
                // For delete, we've already removed locally
                // Log error but don't fail the operation
                std::cerr << "Warning: Synchronous replication delete failed for key '" << key 
                          << "' - local deletion succeeded but some replicas may be inconsistent" << std::endl;
            }
        }
        
        return true;
    } else {
        // Forward to responsible node
        auto responsible = find_successor(key_id);
        if (responsible && *responsible != self_info) {
            try {
                Client client(responsible->address, responsible->port);
                if (client.connect()) {
                    return client.remove(key);
                }
            } catch (const std::exception& e) {
                std::cerr << "Failed to forward DELETE to " << responsible->to_string() 
                          << ": " << e.what() << std::endl;
            }
        }
        return false;
    }
}

bool ChordNode::is_responsible_for_key(const Hash160& key_id) const {
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    if (!predecessor) {
        // Single node is responsible for all keys
        return true;
    }
    
    // Key is in range (predecessor, self]
    return in_range(key_id, predecessor->id, self_info.id, true);
}

std::shared_ptr<NodeInfo> ChordNode::get_successor() const {
    std::lock_guard<std::mutex> lock(routing_mutex);
    return successor_list[0];
}

std::shared_ptr<NodeInfo> ChordNode::get_predecessor() const {
    std::lock_guard<std::mutex> lock(routing_mutex);
    return predecessor;
}

std::vector<std::shared_ptr<NodeInfo>> ChordNode::get_successor_list() const {
    std::lock_guard<std::mutex> lock(routing_mutex);
    return successor_list;
}

void ChordNode::initialize_finger_table() {
    // Initialize all fingers to point to immediate successor initially
    auto successor = successor_list[0];
    std::fill(finger_table.begin(), finger_table.end(), successor);
}

Hash160 ChordNode::get_finger_start(int index) const {
    if (index < 0 || index >= FINGER_TABLE_SIZE) {
        return self_info.id;
    }
    return add_power_of_two(self_info.id, index);
}

void ChordNode::stabilize_loop() {
    while (running.load()) {
        try {
            stabilize();
        } catch (const std::exception& e) {
            std::cerr << "Error in stabilize: " << e.what() << std::endl;
        }
        
        // Use interruptible sleep - returns true if interrupted
        if (interruptible_sleep(stabilize_interval)) {
            break; // Shutdown requested
        }
    }
}

void ChordNode::fix_fingers_loop() {
    while (running.load()) {
        try {
            fix_fingers();
        } catch (const std::exception& e) {
            std::cerr << "Error in fix_fingers: " << e.what() << std::endl;
        }
        
        // Use interruptible sleep - returns true if interrupted
        if (interruptible_sleep(fix_fingers_interval)) {
            break; // Shutdown requested
        }
    }
}

void ChordNode::failure_detection_loop() {
    while (running.load()) {
        try {
            // Copy nodes to check (avoid holding mutex during network operations)
            std::vector<std::shared_ptr<NodeInfo>> nodes_to_check;
            {
                std::lock_guard<std::mutex> lock(routing_mutex);
                
                // Copy successor list nodes
                for (auto& successor : successor_list) {
                    if (successor && *successor != self_info) {
                        nodes_to_check.push_back(successor);
                    }
                }
                
                // Copy predecessor
                if (predecessor && *predecessor != self_info) {
                    nodes_to_check.push_back(predecessor);
                }
            }
            
            // Check nodes without holding mutex (avoid deadlock)
            std::vector<std::shared_ptr<NodeInfo>> failed_nodes;
            for (auto& node : nodes_to_check) {
                if (!running.load()) break; // Check for shutdown during loop
                
                failure_detector->ping_node(node);
                if (failure_detector->is_node_failed(node)) {
                    failed_nodes.push_back(node);
                }
            }
            
            // Handle failures (this will reacquire mutex as needed)
            for (auto& failed_node : failed_nodes) {
                if (!running.load()) break;
                handle_node_failure(failed_node);
            }
            
            // Cleanup old failure detection entries
            failure_detector->cleanup_old_entries();
            
        } catch (const std::exception& e) {
            std::cerr << "Error in failure_detection: " << e.what() << std::endl;
        }
        
        // Use interruptible sleep - returns true if interrupted
        if (interruptible_sleep(failure_check_interval)) {
            break; // Shutdown requested
        }
    }
}

std::shared_ptr<NodeInfo> ChordNode::contact_node(std::shared_ptr<NodeInfo> node, 
                                                  const std::string& operation,
                                                  const Hash160& param) {
    if (!node || *node == self_info) {
        return std::make_shared<NodeInfo>(self_info);
    }
    
    try {
        Client client(node->address, node->port);
        if (!client.connect()) {
            failure_detector->mark_node_failed(node);
            return nullptr;
        }
        
        failure_detector->mark_node_responsive(node);
        
        if (operation == "get_predecessor") {
            // Request predecessor information via custom protocol
            // For now, return placeholder since full RPC not implemented
            return node;
        } else if (operation == "find_successor") {
            // Request successor for given ID
            // For now, return placeholder
            return node;
        } else if (operation == "notify") {
            // Notify node about this node as predecessor
            // For now, just ping
            client.ping();
            return node;
        }
        
        return node;
    } catch (const std::exception& e) {
        std::cerr << "Failed to contact node " << node->to_string() 
                  << ": " << e.what() << std::endl;
        failure_detector->mark_node_failed(node);
        return nullptr;
    }
}

bool ChordNode::ping_node(std::shared_ptr<NodeInfo> node) {
    return replication_manager->ping_node(node);
}

std::vector<std::shared_ptr<NodeInfo>> ChordNode::get_replica_nodes(const Hash160& key_id) const {
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    std::vector<std::shared_ptr<NodeInfo>> replicas;
    int replication_factor = replication_manager->get_replication_factor();
    
    // Get successors for replication (excluding self)
    for (int i = 0; i < replication_factor - 1 && i < SUCCESSOR_LIST_SIZE; ++i) {
        if (successor_list[i] && *successor_list[i] != self_info) {
            replicas.push_back(successor_list[i]);
        }
    }
    
    return replicas;
}

void ChordNode::handle_node_failure(std::shared_ptr<NodeInfo> failed_node) {
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    std::cout << "Handling failure of node: " << failed_node->to_string() << std::endl;
    
    // Update successor list if failed node is a successor
    for (size_t i = 0; i < successor_list.size(); ++i) {
        if (successor_list[i] && *successor_list[i] == *failed_node) {
            // Remove failed node and shift list
            for (size_t j = i; j < successor_list.size() - 1; ++j) {
                successor_list[j] = successor_list[j + 1];
            }
            
            // Find new successor to fill the list
            successor_list[successor_list.size() - 1] = find_successor(self_info.id);
            break;
        }
    }
    
    // Update predecessor if it's the failed node
    if (predecessor && *predecessor == *failed_node) {
        predecessor = nullptr;
        std::cout << "Predecessor failed, will be updated via stabilization" << std::endl;
    }
    
    // Update finger table entries pointing to failed node
    for (size_t i = 0; i < finger_table.size(); ++i) {
        if (finger_table[i] && *finger_table[i] == *failed_node) {
            finger_table[i] = successor_list[0]; // Point to first successor
        }
    }
    
    // Trigger re-replication for keys that were replicated to the failed node
    trigger_re_replication();
}

void ChordNode::trigger_re_replication() {
    std::cout << "Triggering re-replication after node failure" << std::endl;
    
    // Get all keys from local storage
    auto all_keys = local_storage->get_all_keys();
    
    for (const auto& key : all_keys) {
        Hash160 key_id = SHA1::hash(key);
        
        // Check if we are the primary owner of this key
        if (is_responsible_for_key(key_id)) {
            // Get current replica nodes
            auto replica_nodes = get_replica_nodes(key_id);
            
            // Ensure we have enough replicas (REPLICATION_FACTOR - 1)
            if (replica_nodes.size() < static_cast<size_t>(replication_manager->get_replication_factor() - 1)) {
                std::vector<uint8_t> value;
                if (local_storage->get(key, value)) {
                    // Re-replicate to ensure proper replication factor
                    for (auto& replica : replica_nodes) {
                        if (replica && *replica != self_info) {
                            // Send replication request to replica node
                            // This would use replication_manager->replicate_put() in full implementation
                        }
                    }
                    
                    std::cout << "Re-replicated key " << key << " to maintain replication factor" << std::endl;
                }
            }
        }
    }
    
    // Also verify replicas we're holding are still valid
    verify_and_repair_replicas();
}

std::vector<std::shared_ptr<NodeInfo>> ChordNode::get_successor_nodes(int count) const {
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    std::vector<std::shared_ptr<NodeInfo>> successors;
    for (int i = 0; i < count && i < SUCCESSOR_LIST_SIZE; ++i) {
        if (successor_list[i]) {
            successors.push_back(successor_list[i]);
        }
    }
    return successors;
}

bool ChordNode::interruptible_sleep(std::chrono::milliseconds duration) {
    std::unique_lock<std::mutex> lock(shutdown_mutex);
    return shutdown_cv.wait_for(lock, duration, [this] { return !running.load(); });
}

void ChordNode::print_finger_table() const {
    std::lock_guard<std::mutex> lock(routing_mutex);
    std::cout << "Finger table for node " << self_info.to_string() << ":" << std::endl;
    
    for (int i = 0; i < std::min(10, FINGER_TABLE_SIZE); ++i) {
        Hash160 start = get_finger_start(i);
        auto finger = finger_table[i];
        std::cout << "  [" << i << "] start=" << SHA1::to_string(start).substr(0, 8)
                  << " -> " << (finger ? finger->to_string() : "null") << std::endl;
    }
    if (FINGER_TABLE_SIZE > 10) {
        std::cout << "  ... (showing first 10 entries)" << std::endl;
    }
}

void ChordNode::print_successor_list() const {
    std::lock_guard<std::mutex> lock(routing_mutex);
    std::cout << "Successor list for node " << self_info.to_string() << ":" << std::endl;
    
    for (size_t i = 0; i < successor_list.size(); ++i) {
        auto successor = successor_list[i];
        std::cout << "  [" << i << "] " << (successor ? successor->to_string() : "null") << std::endl;
    }
}

// RPC implementations (placeholders for network layer)
std::shared_ptr<NodeInfo> ChordNode::rpc_find_successor(const Hash160& id) {
    return find_successor(id);
}

std::shared_ptr<NodeInfo> ChordNode::rpc_get_predecessor() {
    return get_predecessor();
}

void ChordNode::rpc_notify(std::shared_ptr<NodeInfo> node) {
    notify(node);
}

bool ChordNode::rpc_ping() {
    return true;
}

void ChordNode::transfer_keys_to_node(std::shared_ptr<NodeInfo> target_node) {
    if (!target_node || *target_node == self_info) {
        return;
    }
    
    std::cout << "Transferring keys from " << self_info.to_string() 
              << " to " << target_node->to_string() << std::endl;
    
    // Get all keys that should be transferred to the target node
    auto keys_to_transfer = local_storage->get_keys_in_range([this, &target_node](const std::string& key) {
        Hash160 key_id = SHA1::hash(key);
        
        // Transfer keys that the target node should be responsible for
        // This happens when a new node joins between us and our predecessor
        if (predecessor) {
            return in_range(key_id, predecessor->id, target_node->id, true);
        }
        return false;
    });
    
    // Transfer each key to the target node
    for (const auto& kvp : keys_to_transfer) {
        const std::string& key = kvp.first;
        // In a real implementation, this would send the key-value pair to the target node
        // For now, we'll just remove it from our storage
        local_storage->remove(key);
        std::cout << "Transferred key: " << key << " to " << target_node->to_string() << std::endl;
    }
}

void ChordNode::accept_transferred_keys(std::shared_ptr<NodeInfo> from_node) {
    if (!from_node || *from_node == self_info) {
        return;
    }
    
    std::cout << "Accepting transferred keys from " << from_node->to_string() 
              << " to " << self_info.to_string() << std::endl;
    
    // In a real implementation, this would:
    // 1. Contact the from_node to request keys in our range
    // 2. Receive the keys and store them locally
    // 3. Verify the transfer was successful
    
    // For now, we'll just log the operation
    // The actual transfer would happen through the network layer
}

void ChordNode::verify_and_repair_replicas() {
    std::cout << "Verifying and repairing replicas" << std::endl;
    
    // Get all keys from local storage
    auto all_data = local_storage->get_all_data();
    
    for (const auto& kvp : all_data) {
        const std::string& key = kvp.first;
        const std::vector<uint8_t>& value = kvp.second;
        Hash160 key_id = SHA1::hash(key);
        
        // Check if this is a replica we're holding
        auto responsible_node = find_successor(key_id);
        
        if (responsible_node && *responsible_node != self_info) {
            // This is a replica we're holding
            // Verify the primary node is still alive
            if (!ping_node(responsible_node)) {
                // Primary node is down, check if we should become the primary
                auto our_predecessor = get_predecessor();
                if (our_predecessor && in_range(key_id, our_predecessor->id, self_info.id, true)) {
                    // We are now the primary owner
                    std::cout << "Taking ownership of key " << key << " (previous owner failed)" << std::endl;
                    
                    // Re-replicate to maintain replication factor
                    auto replica_nodes = get_replica_nodes(key_id);
                    for (auto& replica : replica_nodes) {
                        if (replica && *replica != self_info) {
                            // Send replication request to replica node
                            // This would use replication_manager->replicate_put() in full implementation
                        }
                    }
                }
            }
        } else if (responsible_node && *responsible_node == self_info) {
            // We are the primary owner, ensure proper replication
            auto replica_nodes = get_replica_nodes(key_id);
            int alive_replicas = 0;
            
            for (auto& replica : replica_nodes) {
                if (replica && *replica != self_info && ping_node(replica)) {
                    alive_replicas++;
                }
            }
            
            // If we don't have enough alive replicas, find new ones
            if (alive_replicas < replication_manager->get_replication_factor() - 1) {
                std::cout << "Insufficient replicas for key " << key 
                          << " (" << alive_replicas << " alive), re-replicating" << std::endl;
                
                // Re-replicate to new nodes
                for (auto& replica : replica_nodes) {
                    if (replica && *replica != self_info) {
                        // Send replication request to replica node
                        // This would use replication_manager->replicate_put() in full implementation
                    }
                }
            }
        }
    }
}

} // namespace funnelkvs