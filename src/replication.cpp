#include "replication.h"
#include "chord.h"
#include "protocol.h"
#include <iostream>
#include <algorithm>

namespace funnelkvs {

ReplicationManager::ReplicationManager(const ReplicationConfig& cfg) 
    : config(cfg), running(false) {
    if (config.enable_async_replication) {
        start_async_processing();
    }
}

ReplicationManager::~ReplicationManager() {
    stop_async_processing();
}

bool ReplicationManager::replicate_put(const std::string& key, const std::vector<uint8_t>& value,
                                       const std::vector<std::shared_ptr<NodeInfo>>& replicas) {
    // If async replication is enabled, enqueue the task and return immediately
    if (config.enable_async_replication) {
        ReplicationTask task(ReplicationTask::PUT, key, value, replicas);
        enqueue_replication_task(task);
        
        // Record timestamp without holding main mutex
        {
            std::lock_guard<std::mutex> lock(mutex);
            replication_timestamps[key] = std::chrono::steady_clock::now();
        }
        return true; // Async replication always returns success immediately
    }
    
    // Synchronous replication - perform network operations without holding lock
    int successful_replications = 0;
    int required_replications = std::min(config.replication_factor - 1, static_cast<int>(replicas.size()));
    
    // Perform network operations without holding mutex
    for (int i = 0; i < required_replications; ++i) {
        if (!replicas[i] || replicas[i]->port == 0) {
            continue;
        }
        
        bool success = send_replication_request(replicas[i], "PUT", key, value);
        if (success) {
            successful_replications++;
        } else {
            std::cerr << "Synchronous replication failed to " << replicas[i]->to_string() << std::endl;
        }
    }
    
    // Record replication timestamp after network operations
    {
        std::lock_guard<std::mutex> lock(mutex);
        replication_timestamps[key] = std::chrono::steady_clock::now();
    }
    
    // For sync replication, we need all replications to succeed
    bool result = (successful_replications == required_replications);
    
    if (!result) {
        std::cerr << "Replication failed for key '" << key << "': " 
                  << successful_replications << "/" << required_replications << " succeeded" << std::endl;
    }
    
    return result;
}

bool ReplicationManager::replicate_delete(const std::string& key,
                                          const std::vector<std::shared_ptr<NodeInfo>>& replicas) {
    // If async replication is enabled, enqueue the task and return immediately
    if (config.enable_async_replication) {
        ReplicationTask task(ReplicationTask::DELETE, key, {}, replicas);
        enqueue_replication_task(task);
        
        // Remove from timestamp tracking
        {
            std::lock_guard<std::mutex> lock(mutex);
            replication_timestamps.erase(key);
        }
        return true; // Async replication always returns success immediately
    }
    
    // Synchronous replication - perform network operations without holding lock
    int successful_replications = 0;
    int required_replications = std::min(config.replication_factor - 1, static_cast<int>(replicas.size()));
    
    // Perform network operations without holding mutex
    for (int i = 0; i < required_replications; ++i) {
        if (!replicas[i] || replicas[i]->port == 0) {
            continue;
        }
        
        bool success = send_replication_request(replicas[i], "DELETE", key);
        if (success) {
            successful_replications++;
        }
    }
    
    // Remove from timestamp tracking after network operations
    {
        std::lock_guard<std::mutex> lock(mutex);
        replication_timestamps.erase(key);
    }
    
    return (successful_replications == required_replications);
}

bool ReplicationManager::get_from_replicas(const std::string& key, std::vector<uint8_t>& value,
                                          const std::vector<std::shared_ptr<NodeInfo>>& replicas) {
    // Try to read from each replica in order until successful
    for (const auto& replica : replicas) {
        if (!replica || replica->port == 0) {
            continue;
        }
        
        try {
            Client client(replica->address, replica->port);
            if (client.connect()) {
                if (client.get(key, value)) {
                    return true;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to read from replica " << replica->to_string() 
                      << ": " << e.what() << std::endl;
            continue;
        }
    }
    
    return false;
}

void ReplicationManager::handle_replica_failure(std::shared_ptr<NodeInfo> failed_node,
                                               const std::vector<std::shared_ptr<NodeInfo>>& new_replicas,
                                               const std::unordered_map<std::string, std::vector<uint8_t>>& keys_to_replicate) {
    std::lock_guard<std::mutex> lock(mutex);
    
    std::cout << "Handling replica failure for node: " << failed_node->to_string() << std::endl;
    
    // Re-replicate all keys that were stored on the failed node
    int successful_re_replications = 0;
    
    for (const auto& entry : keys_to_replicate) {
        const std::string& key = entry.first;
        const std::vector<uint8_t>& value = entry.second;
        // Find a new replica to store this key
        for (const auto& new_replica : new_replicas) {
            if (!new_replica || new_replica->port == 0 || *new_replica == *failed_node) {
                continue;
            }
            
            if (send_replication_request(new_replica, "PUT", key, value)) {
                successful_re_replications++;
                break; // Successfully replicated to this new node
            }
        }
    }
    
    std::cout << "Re-replicated " << successful_re_replications 
              << "/" << keys_to_replicate.size() << " keys after node failure" << std::endl;
}

bool ReplicationManager::send_replication_request(std::shared_ptr<NodeInfo> target,
                                                  const std::string& operation,
                                                  const std::string& key,
                                                  const std::vector<uint8_t>& value) {
    try {
        Client client(target->address, target->port);
        if (!client.connect()) {
            return false;
        }
        
        if (operation == "PUT") {
            return client.put(key, value);
        } else if (operation == "DELETE") {
            return client.remove(key);
        }
        
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Replication request failed to " << target->to_string() 
                  << ": " << e.what() << std::endl;
        return false;
    }
}

bool ReplicationManager::ping_node(std::shared_ptr<NodeInfo> node) {
    try {
        Client client(node->address, node->port);
        return client.connect() && client.ping();
    } catch (...) {
        return false;
    }
}

void ReplicationManager::start_async_processing() {
    if (running.load()) {
        return; // Already running
    }
    
    running = true;
    processing_thread = std::thread(&ReplicationManager::process_replication_queue, this);
}

void ReplicationManager::stop_async_processing() {
    if (!running.load()) {
        return; // Not running
    }
    
    running = false;
    queue_cv.notify_all(); // Wake up the processing thread
    
    if (processing_thread.joinable()) {
        processing_thread.join();
    }
}

void ReplicationManager::enqueue_replication_task(const ReplicationTask& task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        pending_tasks.push(task);
    }
    queue_cv.notify_one(); // Wake up the processing thread
}

void ReplicationManager::process_replication_queue() {
    while (running.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        
        // Wait for tasks or shutdown signal
        queue_cv.wait(lock, [this] {
            return !pending_tasks.empty() || !running.load();
        });
        
        while (!pending_tasks.empty() && running.load()) {
            ReplicationTask task = pending_tasks.front();
            pending_tasks.pop();
            
            // Release lock while processing the task (prevents deadlock)
            lock.unlock();
            
            // Process the task with retries
            bool success = process_single_task(task);
            
            if (!success && task.retry_count < config.max_retries) {
                // Re-enqueue for retry
                task.retry_count++;
                std::cerr << "Replication task failed for key '" << task.key 
                         << "', retry " << task.retry_count << "/" << config.max_retries << std::endl;
                enqueue_replication_task(task);
            } else if (!success) {
                std::cerr << "Replication task permanently failed for key '" << task.key 
                         << "' after " << config.max_retries << " retries" << std::endl;
            }
            
            // Re-acquire lock for next iteration
            lock.lock();
        }
    }
}

bool ReplicationManager::process_single_task(ReplicationTask& task) {
    int successful_replications = 0;
    int required_replications = std::min(config.replication_factor - 1, 
                                        static_cast<int>(task.replicas.size()));
    
    for (int i = 0; i < required_replications; ++i) {
        if (!task.replicas[i] || task.replicas[i]->port == 0) {
            continue;
        }
        
        bool success = false;
        if (task.type == ReplicationTask::PUT) {
            success = send_replication_request(task.replicas[i], "PUT", task.key, task.value);
        } else if (task.type == ReplicationTask::DELETE) {
            success = send_replication_request(task.replicas[i], "DELETE", task.key);
        }
        
        if (success) {
            successful_replications++;
        }
    }
    
    // Return true if we achieved the required replication factor
    return (successful_replications >= required_replications);
}

// FailureDetector implementation
FailureDetector::FailureDetector(const FailureConfig& cfg) : config(cfg) {
}

void FailureDetector::ping_node(std::shared_ptr<NodeInfo> node) {
    if (!node) {
        return;
    }
    
    std::string node_key = get_node_key(node);
    bool is_responsive = ping_node_impl(node);
    
    std::lock_guard<std::mutex> lock(mutex);
    auto& status = node_statuses[node_key];
    
    if (is_responsive) {
        status.last_seen = std::chrono::steady_clock::now();
        status.consecutive_failures = 0;
        status.is_suspected = false;
        status.is_failed = false;
    } else {
        status.consecutive_failures++;
        
        if (status.consecutive_failures >= config.failure_threshold) {
            status.is_failed = true;
            std::cout << "Node marked as failed: " << node->to_string() << std::endl;
        } else if (status.consecutive_failures >= config.failure_threshold / 2) {
            status.is_suspected = true;
        }
    }
}

void FailureDetector::mark_node_responsive(std::shared_ptr<NodeInfo> node) {
    if (!node) {
        return;
    }
    
    std::string node_key = get_node_key(node);
    std::lock_guard<std::mutex> lock(mutex);
    
    auto& status = node_statuses[node_key];
    status.last_seen = std::chrono::steady_clock::now();
    status.consecutive_failures = 0;
    status.is_suspected = false;
    status.is_failed = false;
}

void FailureDetector::mark_node_failed(std::shared_ptr<NodeInfo> node) {
    if (!node) {
        return;
    }
    
    std::string node_key = get_node_key(node);
    std::lock_guard<std::mutex> lock(mutex);
    
    auto& status = node_statuses[node_key];
    status.is_failed = true;
    status.consecutive_failures = config.failure_threshold;
}

bool FailureDetector::is_node_failed(std::shared_ptr<NodeInfo> node) const {
    if (!node) {
        return true;
    }
    
    std::string node_key = get_node_key(node);
    std::lock_guard<std::mutex> lock(mutex);
    
    auto it = node_statuses.find(node_key);
    return it != node_statuses.end() && it->second.is_failed;
}

bool FailureDetector::is_node_suspected(std::shared_ptr<NodeInfo> node) const {
    if (!node) {
        return true;
    }
    
    std::string node_key = get_node_key(node);
    std::lock_guard<std::mutex> lock(mutex);
    
    auto it = node_statuses.find(node_key);
    return it != node_statuses.end() && it->second.is_suspected;
}

std::vector<std::shared_ptr<NodeInfo>> FailureDetector::get_failed_nodes() const {
    std::lock_guard<std::mutex> lock(mutex);
    
    std::vector<std::shared_ptr<NodeInfo>> failed_nodes;
    
    for (const auto& entry : node_statuses) {
        const std::string& node_key = entry.first;
        const NodeStatus& status = entry.second;
        if (status.is_failed) {
            // Parse node_key back to NodeInfo
            size_t colon_pos = node_key.find(':');
            if (colon_pos != std::string::npos) {
                std::string addr = node_key.substr(0, colon_pos);
                uint16_t port = static_cast<uint16_t>(std::stoi(node_key.substr(colon_pos + 1)));
                failed_nodes.push_back(std::make_shared<NodeInfo>(NodeInfo::from_address(addr, port)));
            }
        }
    }
    
    return failed_nodes;
}

void FailureDetector::cleanup_old_entries(std::chrono::minutes max_age) {
    std::lock_guard<std::mutex> lock(mutex);
    
    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - max_age;
    
    for (auto it = node_statuses.begin(); it != node_statuses.end();) {
        if (it->second.last_seen < cutoff) {
            it = node_statuses.erase(it);
        } else {
            ++it;
        }
    }
}

std::string FailureDetector::get_node_key(std::shared_ptr<NodeInfo> node) const {
    return node->address + ":" + std::to_string(node->port);
}

bool FailureDetector::ping_node_impl(std::shared_ptr<NodeInfo> node) {
    try {
        Client client(node->address, node->port);
        client.connect();
        return client.ping();
    } catch (...) {
        return false;
    }
}

} // namespace funnelkvs