#ifndef FUNNELKVS_CHORD_H
#define FUNNELKVS_CHORD_H

#include "hash.h"
#include "storage.h"
#include "replication.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

namespace funnelkvs {

struct NodeInfo {
    Hash160 id;
    std::string address;
    uint16_t port;
    
    NodeInfo() : port(0) {}
    NodeInfo(const Hash160& node_id, const std::string& addr, uint16_t p)
        : id(node_id), address(addr), port(p) {}
    
    std::string to_string() const;
    static NodeInfo from_address(const std::string& addr, uint16_t p);
    
    bool operator==(const NodeInfo& other) const {
        return id == other.id;
    }
    
    bool operator!=(const NodeInfo& other) const {
        return !(*this == other);
    }
};

class ChordNode {
private:
    static constexpr int FINGER_TABLE_SIZE = 160; // 2^160 address space
    static constexpr int SUCCESSOR_LIST_SIZE = 8;
    
    NodeInfo self_info;
    std::shared_ptr<NodeInfo> predecessor;
    std::vector<std::shared_ptr<NodeInfo>> successor_list;
    std::vector<std::shared_ptr<NodeInfo>> finger_table;
    
    mutable std::mutex routing_mutex;
    std::unique_ptr<Storage> local_storage;
    
    // Replication and failure detection
    std::unique_ptr<ReplicationManager> replication_manager;
    std::unique_ptr<FailureDetector> failure_detector;
    
    // Maintenance threads
    std::atomic<bool> running;
    std::thread stabilize_thread;
    std::thread fix_fingers_thread;
    std::thread failure_detection_thread;
    
    int next_finger_to_fix;
    std::chrono::milliseconds stabilize_interval;
    std::chrono::milliseconds fix_fingers_interval;
    std::chrono::milliseconds failure_check_interval;
    
public:
    explicit ChordNode(const std::string& address, uint16_t port);
    ~ChordNode();
    
    // Disable copy constructor and assignment
    ChordNode(const ChordNode&) = delete;
    ChordNode& operator=(const ChordNode&) = delete;
    
    // Basic node information
    const NodeInfo& get_info() const { return self_info; }
    Hash160 get_id() const { return self_info.id; }
    
    // Chord protocol operations
    std::shared_ptr<NodeInfo> find_successor(const Hash160& id);
    std::shared_ptr<NodeInfo> find_predecessor(const Hash160& id);
    std::shared_ptr<NodeInfo> closest_preceding_node(const Hash160& id);
    
    // Node lifecycle
    void create(); // Create new Chord ring
    void join(std::shared_ptr<NodeInfo> existing_node);
    void leave();
    void start_maintenance();
    void stop_maintenance();
    
    // Maintenance protocols
    void stabilize();
    void notify(std::shared_ptr<NodeInfo> node);
    void fix_fingers();
    
    // Data operations with replication
    bool store_key(const std::string& key, const std::vector<uint8_t>& value);
    bool retrieve_key(const std::string& key, std::vector<uint8_t>& value);
    bool remove_key(const std::string& key);
    
    // Replication operations
    std::vector<std::shared_ptr<NodeInfo>> get_replica_nodes(const Hash160& key_id) const;
    void handle_node_failure(std::shared_ptr<NodeInfo> failed_node);
    void trigger_re_replication();
    
    // Remote procedure calls (to be called by network layer)
    std::shared_ptr<NodeInfo> rpc_find_successor(const Hash160& id);
    std::shared_ptr<NodeInfo> rpc_get_predecessor();
    void rpc_notify(std::shared_ptr<NodeInfo> node);
    bool rpc_ping();
    
    // Utility functions
    bool is_responsible_for_key(const Hash160& key_id) const;
    std::vector<std::shared_ptr<NodeInfo>> get_successor_list() const;
    std::shared_ptr<NodeInfo> get_predecessor() const;
    std::shared_ptr<NodeInfo> get_successor() const;
    
    // Testing and debugging
    void print_finger_table() const;
    void print_successor_list() const;
    
private:
    void initialize_finger_table();
    void update_finger_table_entry(int index, std::shared_ptr<NodeInfo> node);
    void stabilize_loop();
    void fix_fingers_loop();
    void failure_detection_loop();
    Hash160 get_finger_start(int index) const;
    
    // Network communication helpers
    std::shared_ptr<NodeInfo> contact_node(std::shared_ptr<NodeInfo> node, 
                                          const std::string& operation,
                                          const Hash160& param = Hash160{});
    bool ping_node(std::shared_ptr<NodeInfo> node);
    
    // Helper methods
    std::vector<std::shared_ptr<NodeInfo>> get_successor_nodes(int count) const;
};

} // namespace funnelkvs

#endif // FUNNELKVS_CHORD_H