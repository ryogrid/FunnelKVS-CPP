#ifndef FUNNELKVS_REPLICATION_H
#define FUNNELKVS_REPLICATION_H

#include "hash.h"
#include "client.h"
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>

namespace funnelkvs {

struct NodeInfo;

class ReplicationManager {
public:
    struct ReplicationConfig {
        int replication_factor;
        int sync_timeout_ms;
        int max_retries;
        bool enable_async_replication;
        
        ReplicationConfig() 
            : replication_factor(3), sync_timeout_ms(5000), max_retries(3), enable_async_replication(false) {}
        ReplicationConfig(int factor, int timeout_ms, bool async = false)
            : replication_factor(factor), sync_timeout_ms(timeout_ms), max_retries(3), enable_async_replication(async) {}
    };
    
private:
    ReplicationConfig config;
    mutable std::mutex mutex;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> replication_timestamps;
    
public:
    ReplicationManager() : ReplicationManager(ReplicationConfig()) {}
    explicit ReplicationManager(const ReplicationConfig& cfg);
    ~ReplicationManager() = default;
    
    // Core replication operations
    bool replicate_put(const std::string& key, const std::vector<uint8_t>& value,
                      const std::vector<std::shared_ptr<NodeInfo>>& replicas);
    
    bool replicate_delete(const std::string& key,
                         const std::vector<std::shared_ptr<NodeInfo>>& replicas);
    
    // Read from replicas with fallback
    bool get_from_replicas(const std::string& key, std::vector<uint8_t>& value,
                          const std::vector<std::shared_ptr<NodeInfo>>& replicas);
    
    // Failure handling
    void handle_replica_failure(std::shared_ptr<NodeInfo> failed_node,
                               const std::vector<std::shared_ptr<NodeInfo>>& new_replicas,
                               const std::unordered_map<std::string, std::vector<uint8_t>>& keys_to_replicate);
    
    // Configuration
    void set_replication_factor(int factor) { 
        std::lock_guard<std::mutex> lock(mutex);
        config.replication_factor = factor; 
    }
    int get_replication_factor() const { 
        std::lock_guard<std::mutex> lock(mutex);
        return config.replication_factor; 
    }
    
    // Statistics
    size_t get_replication_count() const {
        std::lock_guard<std::mutex> lock(mutex);
        return replication_timestamps.size();
    }
    
    // Node communication
    bool ping_node(std::shared_ptr<NodeInfo> node);
    
private:
    bool send_replication_request(std::shared_ptr<NodeInfo> target,
                                 const std::string& operation,
                                 const std::string& key,
                                 const std::vector<uint8_t>& value = {});
};

// Failure detector for monitoring node health
class FailureDetector {
public:
    struct FailureConfig {
        int ping_interval_ms;
        int ping_timeout_ms;
        int failure_threshold;
        
        FailureConfig() 
            : ping_interval_ms(2000), ping_timeout_ms(5000), failure_threshold(3) {}
        FailureConfig(int interval_ms, int timeout_ms, int threshold)
            : ping_interval_ms(interval_ms), ping_timeout_ms(timeout_ms), failure_threshold(threshold) {}
    };
    
private:
    struct NodeStatus {
        std::chrono::steady_clock::time_point last_seen;
        int consecutive_failures;
        bool is_suspected;
        bool is_failed;
        
        NodeStatus() : consecutive_failures(0), is_suspected(false), is_failed(false) {}
    };
    
    FailureConfig config;
    mutable std::mutex mutex;
    std::unordered_map<std::string, NodeStatus> node_statuses; // key: address:port
    
public:
    FailureDetector() : FailureDetector(FailureConfig()) {}
    explicit FailureDetector(const FailureConfig& cfg);
    ~FailureDetector() = default;
    
    // Monitor node health
    void ping_node(std::shared_ptr<NodeInfo> node);
    void mark_node_responsive(std::shared_ptr<NodeInfo> node);
    void mark_node_failed(std::shared_ptr<NodeInfo> node);
    
    // Query node status
    bool is_node_failed(std::shared_ptr<NodeInfo> node) const;
    bool is_node_suspected(std::shared_ptr<NodeInfo> node) const;
    
    // Get failed nodes list
    std::vector<std::shared_ptr<NodeInfo>> get_failed_nodes() const;
    
    // Cleanup
    void cleanup_old_entries(std::chrono::minutes max_age = std::chrono::minutes(30));
    
private:
    std::string get_node_key(std::shared_ptr<NodeInfo> node) const;
    bool ping_node_impl(std::shared_ptr<NodeInfo> node);
};

} // namespace funnelkvs

#endif // FUNNELKVS_REPLICATION_H