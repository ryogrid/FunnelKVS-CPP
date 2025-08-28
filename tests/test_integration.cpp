#include "../include/server.h"
#include "../include/client.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace funnelkvs;

void test_basic_client_server() {
    Server server(8001, 4);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    Client client("127.0.0.1", 8001);
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
    
    std::cout << "✓ test_basic_client_server passed" << std::endl;
}

void test_multiple_clients() {
    Server server(8002, 8);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    const int num_clients = 5;
    std::vector<std::thread> threads;
    
    auto client_work = [](int client_id) {
        Client client("127.0.0.1", 8002);
        assert(client.connect());
        
        for (int i = 0; i < 10; ++i) {
            std::string key = "client" + std::to_string(client_id) + "_key" + std::to_string(i);
            std::vector<uint8_t> value = {static_cast<uint8_t>(client_id), static_cast<uint8_t>(i)};
            
            assert(client.put(key, value));
            
            std::vector<uint8_t> retrieved;
            assert(client.get(key, retrieved));
            assert(retrieved == value);
        }
        
        client.disconnect();
    };
    
    for (int i = 0; i < num_clients; ++i) {
        threads.emplace_back(client_work, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    server.stop();
    
    std::cout << "✓ test_multiple_clients passed" << std::endl;
}

void test_ping() {
    Server server(8003, 2);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    Client client("127.0.0.1", 8003);
    assert(client.connect());
    assert(client.ping());
    
    client.disconnect();
    server.stop();
    
    std::cout << "✓ test_ping passed" << std::endl;
}

void test_large_values() {
    Server server(8004, 4);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    Client client("127.0.0.1", 8004);
    assert(client.connect());
    
    std::string key = "large_key";
    std::vector<uint8_t> large_value(100000, 0xAB);
    assert(client.put(key, large_value));
    
    std::vector<uint8_t> retrieved;
    assert(client.get(key, retrieved));
    assert(retrieved.size() == large_value.size());
    assert(retrieved == large_value);
    
    client.disconnect();
    server.stop();
    
    std::cout << "✓ test_large_values passed" << std::endl;
}

void test_concurrent_operations() {
    Server server(8005, 8);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    const int num_threads = 10;
    const int ops_per_thread = 50;
    std::vector<std::thread> threads;
    
    auto worker = [ops_per_thread](int thread_id) {
        Client client("127.0.0.1", 8005);
        assert(client.connect());
        
        for (int i = 0; i < ops_per_thread; ++i) {
            std::string key = "key_" + std::to_string((thread_id * ops_per_thread + i) % 100);
            std::vector<uint8_t> value = {static_cast<uint8_t>(thread_id), static_cast<uint8_t>(i)};
            
            int op = i % 3;
            if (op == 0) {
                client.put(key, value);
            } else if (op == 1) {
                std::vector<uint8_t> retrieved;
                client.get(key, retrieved);
            } else {
                client.remove(key);
            }
        }
        
        client.disconnect();
    };
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    server.stop();
    
    std::cout << "✓ test_concurrent_operations passed" << std::endl;
}

void test_reconnect() {
    Server server(8006, 2);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    Client client("127.0.0.1", 8006);
    assert(client.connect());
    
    std::string key = "reconnect_key";
    std::vector<uint8_t> value = {'t', 'e', 's', 't'};
    assert(client.put(key, value));
    
    client.disconnect();
    assert(!client.is_connected());
    
    assert(client.connect());
    assert(client.is_connected());
    
    std::vector<uint8_t> retrieved;
    assert(client.get(key, retrieved));
    assert(retrieved == value);
    
    client.disconnect();
    server.stop();
    
    std::cout << "✓ test_reconnect passed" << std::endl;
}

int main() {
    std::cout << "Running integration tests..." << std::endl;
    
    test_basic_client_server();
    test_multiple_clients();
    test_ping();
    test_large_values();
    test_concurrent_operations();
    test_reconnect();
    
    std::cout << "\nAll integration tests passed!" << std::endl;
    return 0;
}