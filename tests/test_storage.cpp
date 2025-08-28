#include "../include/storage.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <vector>

using namespace funnelkvs;

void test_basic_operations() {
    Storage storage;
    
    std::string key = "test_key";
    std::vector<uint8_t> value = {'v', 'a', 'l', 'u', 'e'};
    std::vector<uint8_t> retrieved;
    
    assert(!storage.exists(key));
    assert(!storage.get(key, retrieved));
    
    storage.put(key, value);
    assert(storage.exists(key));
    assert(storage.get(key, retrieved));
    assert(retrieved == value);
    assert(storage.size() == 1);
    
    assert(storage.remove(key));
    assert(!storage.exists(key));
    assert(!storage.get(key, retrieved));
    assert(storage.size() == 0);
    
    assert(!storage.remove(key));
    
    std::cout << "✓ test_basic_operations passed" << std::endl;
}

void test_overwrite() {
    Storage storage;
    
    std::string key = "key1";
    std::vector<uint8_t> value1 = {'v', '1'};
    std::vector<uint8_t> value2 = {'v', '2'};
    std::vector<uint8_t> retrieved;
    
    storage.put(key, value1);
    assert(storage.get(key, retrieved));
    assert(retrieved == value1);
    
    storage.put(key, value2);
    assert(storage.get(key, retrieved));
    assert(retrieved == value2);
    assert(storage.size() == 1);
    
    std::cout << "✓ test_overwrite passed" << std::endl;
}

void test_multiple_keys() {
    Storage storage;
    
    for (int i = 0; i < 100; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::vector<uint8_t> value(1, static_cast<uint8_t>(i));
        storage.put(key, value);
    }
    
    assert(storage.size() == 100);
    
    for (int i = 0; i < 100; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::vector<uint8_t> retrieved;
        assert(storage.get(key, retrieved));
        assert(retrieved.size() == 1);
        assert(retrieved[0] == static_cast<uint8_t>(i));
    }
    
    storage.clear();
    assert(storage.size() == 0);
    
    std::cout << "✓ test_multiple_keys passed" << std::endl;
}

void test_concurrent_access() {
    Storage storage;
    const int num_threads = 10;
    const int ops_per_thread = 1000;
    
    auto worker = [&storage](int thread_id) {
        for (int i = 0; i < ops_per_thread; ++i) {
            std::string key = "t" + std::to_string(thread_id) + "_k" + std::to_string(i);
            std::vector<uint8_t> value = {static_cast<uint8_t>(thread_id), static_cast<uint8_t>(i)};
            storage.put(key, value);
            
            std::vector<uint8_t> retrieved;
            assert(storage.get(key, retrieved));
            assert(retrieved == value);
        }
    };
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    assert(storage.size() == num_threads * ops_per_thread);
    
    std::cout << "✓ test_concurrent_access passed" << std::endl;
}

void test_concurrent_mixed_operations() {
    Storage storage;
    const int num_threads = 10;
    const int ops_per_thread = 100;
    
    for (int i = 0; i < 50; ++i) {
        storage.put("shared_" + std::to_string(i), {static_cast<uint8_t>(i)});
    }
    
    auto worker = [&storage](int thread_id) {
        for (int i = 0; i < ops_per_thread; ++i) {
            int op = i % 3;
            std::string key = "shared_" + std::to_string(i % 50);
            
            if (op == 0) {
                std::vector<uint8_t> value = {static_cast<uint8_t>(thread_id), static_cast<uint8_t>(i)};
                storage.put(key, value);
            } else if (op == 1) {
                std::vector<uint8_t> retrieved;
                storage.get(key, retrieved);
            } else {
                storage.remove(key);
            }
        }
    };
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "✓ test_concurrent_mixed_operations passed" << std::endl;
}

void test_large_values() {
    Storage storage;
    
    std::string key = "large_key";
    std::vector<uint8_t> large_value(1024 * 1024, 0xAB);
    std::vector<uint8_t> retrieved;
    
    storage.put(key, large_value);
    assert(storage.get(key, retrieved));
    assert(retrieved.size() == large_value.size());
    assert(retrieved == large_value);
    
    std::cout << "✓ test_large_values passed" << std::endl;
}

void test_empty_key_value() {
    Storage storage;
    
    std::string empty_key = "";
    std::vector<uint8_t> value = {'v'};
    std::vector<uint8_t> empty_value;
    std::vector<uint8_t> retrieved;
    
    storage.put(empty_key, value);
    assert(storage.get(empty_key, retrieved));
    assert(retrieved == value);
    
    std::string key = "normal_key";
    storage.put(key, empty_value);
    assert(storage.get(key, retrieved));
    assert(retrieved.empty());
    
    std::cout << "✓ test_empty_key_value passed" << std::endl;
}

int main() {
    std::cout << "Running storage tests..." << std::endl;
    
    test_basic_operations();
    test_overwrite();
    test_multiple_keys();
    test_concurrent_access();
    test_concurrent_mixed_operations();
    test_large_values();
    test_empty_key_value();
    
    std::cout << "\nAll storage tests passed!" << std::endl;
    return 0;
}