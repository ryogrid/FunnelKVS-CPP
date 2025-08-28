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
}

void ChordNode::leave() {
    stop_maintenance();
    
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    // Notify successor about leaving (transfer data)
    if (successor_list[0] && *successor_list[0] != self_info) {
        // In full implementation, transfer keys to successor
        std::cout << "Node " << self_info.to_string() << " leaving ring" << std::endl;
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
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    auto successor = successor_list[0];
    if (!successor || *successor == self_info) {
        return;
    }
    
    // Ask successor for its predecessor
    auto x = contact_node(successor, "get_predecessor");
    
    if (x && *x != self_info && in_range(x->id, self_info.id, successor->id, false)) {
        // Update successor
        successor_list[0] = x;
        successor = x;
    }
    
    // Notify successor about this node
    if (successor && *successor != self_info) {
        contact_node(successor, "notify", self_info.id);
    }
}

void ChordNode::notify(std::shared_ptr<NodeInfo> node) {
    if (!node || *node == self_info) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    if (!predecessor || in_range(node->id, predecessor->id, self_info.id, false)) {
        predecessor = node;
        std::cout << "Node " << self_info.to_string() 
                  << " updated predecessor to " << node->to_string() << std::endl;
    }
}

void ChordNode::fix_fingers() {
    std::lock_guard<std::mutex> lock(routing_mutex);
    
    next_finger_to_fix = (next_finger_to_fix + 1) % FINGER_TABLE_SIZE;
    
    Hash160 finger_start = get_finger_start(next_finger_to_fix);
    auto successor = find_successor(finger_start);
    
    if (successor) {
        finger_table[next_finger_to_fix] = successor;
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
        if (!replicas.empty()) {
            bool replication_success = replication_manager->replicate_put(key, value, replicas);
            if (!replication_success) {
                std::cerr << "Warning: Replication failed for key '" << key << "'" << std::endl;
                // Continue with local storage success
            }
        }
        
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
        // Remove from local storage first
        bool local_removed = local_storage->remove(key);
        
        // Remove from replicas
        auto replicas = get_replica_nodes(key_id);
        if (!replicas.empty()) {
            bool replication_success = replication_manager->replicate_delete(key, replicas);
            if (!replication_success) {
                std::cerr << "Warning: Replication delete failed for key '" << key << "'" << std::endl;
            }
        }
        
        return local_removed;
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
        std::this_thread::sleep_for(stabilize_interval);
    }
}

void ChordNode::fix_fingers_loop() {
    while (running.load()) {
        try {
            fix_fingers();
        } catch (const std::exception& e) {
            std::cerr << "Error in fix_fingers: " << e.what() << std::endl;
        }
        std::this_thread::sleep_for(fix_fingers_interval);
    }
}

void ChordNode::failure_detection_loop() {
    while (running.load()) {
        try {
            // Check successor and predecessor health
            {
                std::lock_guard<std::mutex> lock(routing_mutex);
                
                // Check successor list
                for (auto& successor : successor_list) {
                    if (successor && *successor != self_info) {
                        failure_detector->ping_node(successor);
                        if (failure_detector->is_node_failed(successor)) {
                            handle_node_failure(successor);
                        }
                    }
                }
                
                // Check predecessor
                if (predecessor && *predecessor != self_info) {
                    failure_detector->ping_node(predecessor);
                    if (failure_detector->is_node_failed(predecessor)) {
                        handle_node_failure(predecessor);
                    }
                }
            }
            
            // Cleanup old failure detection entries
            failure_detector->cleanup_old_entries();
            
        } catch (const std::exception& e) {
            std::cerr << "Error in failure_detection: " << e.what() << std::endl;
        }
        std::this_thread::sleep_for(failure_check_interval);
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
    // Get all keys from local storage for re-replication
    // This is a simplified implementation
    std::cout << "Triggering re-replication after node failure" << std::endl;
    
    // In a full implementation, we would:
    // 1. Identify keys that need re-replication
    // 2. Find new replica nodes
    // 3. Copy data to new replicas
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

} // namespace funnelkvs