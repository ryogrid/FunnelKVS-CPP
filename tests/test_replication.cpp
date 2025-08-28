#include "../include/replication.h"
#include "../include/chord.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace funnelkvs;

void test_replication_manager_basic() {
    ReplicationManager::ReplicationConfig config;
    config.replication_factor = 3;
    config.enable_async_replication = false; // Sync replication
    
    ReplicationManager replicator(config);
    
    assert(replicator.get_replication_factor() == 3);
    
    std::cout << "✓ test_replication_manager_basic passed" << std::endl;
}

void test_failure_detector_basic() {
    FailureDetector::FailureConfig config;
    config.ping_interval_ms = 100;
    config.failure_threshold = 2;
    
    FailureDetector detector(config);
    
    // Create test node
    auto test_node = std::make_shared<NodeInfo>(NodeInfo::from_address("127.0.0.1", 9999));
    
    // Initially, node should not be failed
    assert(!detector.is_node_failed(test_node));
    assert(!detector.is_node_suspected(test_node));
    
    // Mark as responsive
    detector.mark_node_responsive(test_node);
    assert(!detector.is_node_failed(test_node));
    
    // Mark as failed
    detector.mark_node_failed(test_node);
    assert(detector.is_node_failed(test_node));
    
    std::cout << "✓ test_failure_detector_basic passed" << std::endl;
}

void test_chord_replication_integration() {
    ChordNode node("127.0.0.1", 9001);
    node.create();
    
    // Test key storage with replication (single node case)
    std::string key = "test_key";
    std::vector<uint8_t> value = {'v', 'a', 'l', 'u', 'e'};
    
    assert(node.store_key(key, value));
    
    std::vector<uint8_t> retrieved;
    assert(node.retrieve_key(key, retrieved));
    assert(retrieved == value);
    
    assert(node.remove_key(key));
    assert(!node.retrieve_key(key, retrieved));
    
    std::cout << "✓ test_chord_replication_integration passed" << std::endl;
}

void test_replica_nodes_calculation() {
    ChordNode node("127.0.0.1", 9002);
    node.create();
    
    Hash160 test_key_id = SHA1::hash("test_key");
    auto replicas = node.get_replica_nodes(test_key_id);
    
    // In single-node ring, should have no replicas
    assert(replicas.empty());
    
    std::cout << "✓ test_replica_nodes_calculation passed" << std::endl;
}

void test_node_failure_handling() {
    ChordNode node("127.0.0.1", 9003);
    node.create();
    
    // Create a fake failed node
    auto failed_node = std::make_shared<NodeInfo>(NodeInfo::from_address("127.0.0.1", 9999));
    
    // This should not crash (graceful handling)
    node.handle_node_failure(failed_node);
    
    std::cout << "✓ test_node_failure_handling passed" << std::endl;
}

void test_maintenance_threads_start_stop() {
    ChordNode node("127.0.0.1", 9004);
    node.create();
    
    // Start maintenance (should include failure detection thread)
    node.start_maintenance();
    
    // Let it run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Stop maintenance
    node.stop_maintenance();
    
    std::cout << "✓ test_maintenance_threads_start_stop passed" << std::endl;
}

void test_replication_config() {
    ReplicationManager::ReplicationConfig config;
    config.replication_factor = 5;
    config.sync_timeout_ms = 10000;
    config.enable_async_replication = true;
    
    ReplicationManager replicator(config);
    
    assert(replicator.get_replication_factor() == 5);
    
    // Test changing replication factor
    replicator.set_replication_factor(2);
    assert(replicator.get_replication_factor() == 2);
    
    std::cout << "✓ test_replication_config passed" << std::endl;
}

void test_failure_detector_cleanup() {
    FailureDetector detector;
    
    auto test_node1 = std::make_shared<NodeInfo>(NodeInfo::from_address("127.0.0.1", 8001));
    auto test_node2 = std::make_shared<NodeInfo>(NodeInfo::from_address("127.0.0.1", 8002));
    
    detector.mark_node_responsive(test_node1);
    detector.mark_node_responsive(test_node2);
    
    // Cleanup with very short max age should remove entries
    detector.cleanup_old_entries(std::chrono::minutes(0));
    
    // This should not crash
    auto failed_nodes = detector.get_failed_nodes();
    
    std::cout << "✓ test_failure_detector_cleanup passed" << std::endl;
}

void test_concurrent_replication_operations() {
    ReplicationManager replicator;
    
    const int num_threads = 3;
    const int ops_per_thread = 10;
    std::vector<std::thread> threads;
    
    auto worker = [&replicator](int thread_id) {
        for (int i = 0; i < ops_per_thread; ++i) {
            std::string key = "t" + std::to_string(thread_id) + "_k" + std::to_string(i);
            std::vector<uint8_t> value = {static_cast<uint8_t>(thread_id), static_cast<uint8_t>(i)};
            
            // Test with empty replica list (should succeed)
            std::vector<std::shared_ptr<NodeInfo>> empty_replicas;
            replicator.replicate_put(key, value, empty_replicas);
        }
    };
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    assert(replicator.get_replication_count() == num_threads * ops_per_thread);
    
    std::cout << "✓ test_concurrent_replication_operations passed" << std::endl;
}

void test_network_error_handling() {
    ReplicationManager replicator;
    
    // Create a node that doesn't exist (should fail gracefully)
    auto non_existent_node = std::make_shared<NodeInfo>(NodeInfo::from_address("192.168.255.255", 9999));
    std::vector<std::shared_ptr<NodeInfo>> replicas = {non_existent_node};
    
    std::string key = "test_key";
    std::vector<uint8_t> value = {'v', 'a', 'l', 'u', 'e'};
    
    // Should fail gracefully without crashing
    bool result = replicator.replicate_put(key, value, replicas);
    assert(!result); // Should fail due to network error
    
    std::vector<uint8_t> retrieved;
    result = replicator.get_from_replicas(key, retrieved, replicas);
    assert(!result); // Should fail to retrieve
    
    std::cout << "✓ test_network_error_handling passed" << std::endl;
}

int main() {
    std::cout << "Running replication and failure detection tests..." << std::endl;
    std::cout << std::endl;
    
    test_replication_manager_basic();
    test_failure_detector_basic();
    test_chord_replication_integration();
    test_replica_nodes_calculation();
    test_node_failure_handling();
    test_maintenance_threads_start_stop();
    test_replication_config();
    test_failure_detector_cleanup();
    test_concurrent_replication_operations();
    test_network_error_handling();
    
    std::cout << std::endl;
    std::cout << "All replication tests passed!" << std::endl;
    std::cout << "Phase 3 replication functionality implemented successfully." << std::endl;
    
    return 0;
}