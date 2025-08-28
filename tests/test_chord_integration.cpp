#include "../include/chord_server.h"
#include "../include/client.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <vector>

using namespace funnelkvs;

void test_single_chord_node() {
    std::cout << "Testing single Chord node..." << std::endl;
    
    ChordServer server("127.0.0.1", 9001);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Test basic operations on single node
    Client client("127.0.0.1", 9001);
    assert(client.connect());
    
    std::string key = "test_key";
    std::vector<uint8_t> value = {'v', 'a', 'l', 'u', 'e'};
    
    assert(client.put(key, value));
    
    std::vector<uint8_t> retrieved;
    assert(client.get(key, retrieved));
    assert(retrieved == value);
    
    assert(client.remove(key));
    assert(!client.get(key, retrieved));
    
    client.disconnect();
    server.stop();
    
    std::cout << "✓ test_single_chord_node passed" << std::endl;
}

void test_chord_node_info() {
    std::cout << "Testing Chord node information..." << std::endl;
    
    ChordServer server("127.0.0.1", 9002);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Get node info
    NodeInfo info = server.get_node_info();
    assert(info.address == "127.0.0.1");
    assert(info.port == 9002);
    assert(info.id != Hash160{}); // Should have valid ID
    
    // Test that different addresses produce different IDs
    ChordServer server2("127.0.0.1", 9003);
    NodeInfo info2 = server2.get_node_info();
    assert(info.id != info2.id);
    
    server.stop();
    
    std::cout << "✓ test_chord_node_info passed" << std::endl;
}

void test_key_distribution() {
    std::cout << "Testing key distribution in single node..." << std::endl;
    
    ChordServer server("127.0.0.1", 9004);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    Client client("127.0.0.1", 9004);
    assert(client.connect());
    
    // Store multiple keys
    for (int i = 0; i < 10; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::vector<uint8_t> value = {'v', static_cast<uint8_t>(i)};
        assert(client.put(key, value));
    }
    
    // Retrieve all keys
    for (int i = 0; i < 10; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::vector<uint8_t> retrieved;
        assert(client.get(key, retrieved));
        assert(retrieved.size() == 2);
        assert(retrieved[0] == 'v');
        assert(retrieved[1] == static_cast<uint8_t>(i));
    }
    
    client.disconnect();
    server.stop();
    
    std::cout << "✓ test_key_distribution passed" << std::endl;
}

void test_concurrent_chord_operations() {
    std::cout << "Testing concurrent Chord operations..." << std::endl;
    
    ChordServer server("127.0.0.1", 9005);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    const int num_threads = 3;
    const int ops_per_thread = 20;
    std::vector<std::thread> threads;
    
    auto worker = [](int thread_id) {
        Client client("127.0.0.1", 9005);
        assert(client.connect());
        
        for (int i = 0; i < ops_per_thread; ++i) {
            std::string key = "t" + std::to_string(thread_id) + "_k" + std::to_string(i);
            std::vector<uint8_t> value = {static_cast<uint8_t>(thread_id), static_cast<uint8_t>(i)};
            
            assert(client.put(key, value));
            
            std::vector<uint8_t> retrieved;
            assert(client.get(key, retrieved));
            assert(retrieved == value);
        }
        
        client.disconnect();
    };
    
    // Start concurrent operations
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    
    // Wait for completion
    for (auto& t : threads) {
        t.join();
    }
    
    server.stop();
    
    std::cout << "✓ test_concurrent_chord_operations passed" << std::endl;
}

void test_chord_hash_consistency() {
    std::cout << "Testing Chord hash consistency..." << std::endl;
    
    // Test that same keys produce same hash locations
    std::string key1 = "consistent_key";
    Hash160 hash1 = SHA1::hash(key1);
    Hash160 hash2 = SHA1::hash(key1);
    assert(hash1 == hash2);
    
    // Test that different keys produce different hashes
    std::string key2 = "different_key";
    Hash160 hash3 = SHA1::hash(key2);
    assert(hash1 != hash3);
    
    // Test node ID generation consistency
    NodeInfo node1 = NodeInfo::from_address("127.0.0.1", 8001);
    NodeInfo node2 = NodeInfo::from_address("127.0.0.1", 8001);
    assert(node1.id == node2.id);
    
    NodeInfo node3 = NodeInfo::from_address("127.0.0.1", 8002);
    assert(node1.id != node3.id);
    
    std::cout << "✓ test_chord_hash_consistency passed" << std::endl;
}

void test_chord_server_lifecycle() {
    std::cout << "Testing Chord server lifecycle..." << std::endl;
    
    ChordServer server("127.0.0.1", 9006);
    assert(!server.is_chord_enabled());
    
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    assert(server.is_chord_enabled());
    assert(server.is_running());
    
    // Test stopping and restarting
    server.stop();
    assert(!server.is_running());
    
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(server.is_running());
    assert(server.is_chord_enabled());
    
    server.stop();
    
    std::cout << "✓ test_chord_server_lifecycle passed" << std::endl;
}

void test_chord_ring_operations() {
    std::cout << "Testing Chord ring operations..." << std::endl;
    
    ChordServer server("127.0.0.1", 9007);
    
    // Create new ring
    server.create_ring();
    assert(server.is_chord_enabled());
    
    // Test leaving ring
    server.leave_ring();
    assert(!server.is_chord_enabled());
    
    // Test joining ring (self-join for single node test)
    server.join_ring("127.0.0.1", 9007);
    assert(server.is_chord_enabled());
    
    server.stop();
    
    std::cout << "✓ test_chord_ring_operations passed" << std::endl;
}

int main() {
    std::cout << "Running Chord integration tests..." << std::endl;
    std::cout << "(Note: Multi-node network tests are simplified in Phase 2)" << std::endl;
    std::cout << std::endl;
    
    test_single_chord_node();
    test_chord_node_info();
    test_key_distribution();
    test_concurrent_chord_operations();
    test_chord_hash_consistency();
    test_chord_server_lifecycle();
    test_chord_ring_operations();
    
    std::cout << std::endl;
    std::cout << "All Chord integration tests passed!" << std::endl;
    std::cout << "Phase 2 implementation complete with basic Chord DHT functionality." << std::endl;
    
    return 0;
}