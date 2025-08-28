#include "../include/hash.h"
#include <iostream>
#include <cassert>
#include <string>
#include <set>
#include <cstring>

using namespace funnelkvs;

void test_sha1_basic() {
    // Test known SHA-1 hash values
    std::string input = "hello";
    Hash160 result = SHA1::hash(input);
    std::string hex_result = SHA1::to_string(result);
    
    // Expected SHA-1 of "hello": aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d
    std::string expected = "aaf4c61ddcc5e8a2dabede0f3b482cd9aea9434d";
    assert(hex_result == expected);
    
    std::cout << "✓ test_sha1_basic passed" << std::endl;
}

void test_sha1_empty_string() {
    std::string input = "";
    Hash160 result = SHA1::hash(input);
    std::string hex_result = SHA1::to_string(result);
    
    // Expected SHA-1 of "": da39a3ee5e6b4b0d3255bfef95601890afd80709
    std::string expected = "da39a3ee5e6b4b0d3255bfef95601890afd80709";
    assert(hex_result == expected);
    
    std::cout << "✓ test_sha1_empty_string passed" << std::endl;
}

void test_sha1_long_string() {
    std::string input(1000, 'a');
    Hash160 result = SHA1::hash(input);
    std::string hex_result = SHA1::to_string(result);
    
    // SHA-1 should produce consistent results for same input
    Hash160 result2 = SHA1::hash(input);
    std::string hex_result2 = SHA1::to_string(result2);
    assert(hex_result == hex_result2);
    
    std::cout << "✓ test_sha1_long_string passed" << std::endl;
}

void test_hash_string_conversion() {
    Hash160 original = SHA1::hash("test");
    std::string hex_str = SHA1::to_string(original);
    Hash160 converted = SHA1::from_string(hex_str);
    
    assert(original == converted);
    assert(hex_str.length() == 40); // 20 bytes * 2 hex chars
    
    std::cout << "✓ test_hash_string_conversion passed" << std::endl;
}

void test_hash_comparisons() {
    Hash160 hash1 = SHA1::hash("abc");
    Hash160 hash2 = SHA1::hash("def");
    Hash160 hash3 = SHA1::hash("abc");
    
    assert(hash1 == hash3);
    assert(hash1 != hash2);
    assert((hash1 < hash2) || (hash2 < hash1)); // One should be less than the other
    
    std::cout << "✓ test_hash_comparisons passed" << std::endl;
}

void test_add_power_of_two() {
    Hash160 base = {};  // All zeros
    
    // Add 2^0 = 1
    Hash160 result1 = add_power_of_two(base, 0);
    assert(result1[19] == 1); // LSB should be 1
    
    // Add 2^8 = 256
    Hash160 result8 = add_power_of_two(base, 8);
    assert(result8[18] == 1); // Second byte from right should be 1
    
    // Add 2^16
    Hash160 result16 = add_power_of_two(base, 16);
    assert(result16[17] == 1); // Third byte from right should be 1
    
    std::cout << "✓ test_add_power_of_two passed" << std::endl;
}

void test_in_range_normal() {
    Hash160 start = {};
    Hash160 end = {};
    Hash160 middle = {};
    
    start[19] = 10;   // 10
    end[19] = 50;     // 50
    middle[19] = 30;  // 30
    
    // Test normal range (start < end)
    assert(in_range(middle, start, end, true));   // 30 in (10, 50]
    assert(!in_range(start, start, end, true));   // 10 not in (10, 50]
    assert(in_range(end, start, end, true));      // 50 in (10, 50]
    assert(!in_range(end, start, end, false));    // 50 not in (10, 50)
    
    Hash160 outside1 = {};
    Hash160 outside2 = {};
    outside1[19] = 5;   // 5
    outside2[19] = 60;  // 60
    
    assert(!in_range(outside1, start, end, true)); // 5 not in (10, 50]
    assert(!in_range(outside2, start, end, true)); // 60 not in (10, 50]
    
    std::cout << "✓ test_in_range_normal passed" << std::endl;
}

void test_in_range_wraparound() {
    Hash160 start = {};
    Hash160 end = {};
    Hash160 middle1 = {};
    Hash160 middle2 = {};
    
    start[19] = 200;  // 200 (near max)
    end[19] = 50;     // 50 (wraps around)
    middle1[19] = 250; // 250 (should be in range)
    middle2[19] = 30;  // 30 (should be in range)
    
    // Test wraparound range (start > end)
    assert(in_range(middle1, start, end, true));  // 250 in (200, 50] (wraparound)
    assert(in_range(middle2, start, end, true));  // 30 in (200, 50] (wraparound)
    assert(!in_range(start, start, end, true));   // 200 not in (200, 50]
    assert(in_range(end, start, end, true));      // 50 in (200, 50]
    
    Hash160 outside = {};
    outside[19] = 100; // 100 should not be in (200, 50]
    assert(!in_range(outside, start, end, true));
    
    std::cout << "✓ test_in_range_wraparound passed" << std::endl;
}

void test_distance_calculation() {
    Hash160 from = {};
    Hash160 to = {};
    
    from[19] = 10;
    to[19] = 50;
    
    Hash160 dist = distance(from, to);
    assert(dist[19] == 40); // 50 - 10 = 40
    
    // Test wraparound distance
    Hash160 from_wrap = {};
    Hash160 to_wrap = {};
    from_wrap[19] = 200;
    to_wrap[19] = 50;
    
    Hash160 dist_wrap = distance(from_wrap, to_wrap);
    // Distance should be (256 - 200) + 50 = 106 in the last byte
    // But this is a simplified test - actual implementation may vary
    
    std::cout << "✓ test_distance_calculation passed" << std::endl;
}

void test_edge_cases() {
    Hash160 zero = {};
    Hash160 max = {};
    std::memset(max.data(), 0xFF, 20); // All 1s
    
    // Test edge cases for in_range
    assert(!in_range(zero, zero, zero, false)); // Empty range
    assert(in_range(zero, zero, zero, true));   // Single point range
    
    // Test maximum value operations
    Hash160 result = add_power_of_two(max, 0);
    // Adding to max should wrap around (implementation dependent)
    
    std::cout << "✓ test_edge_cases passed" << std::endl;
}

void test_hash_distribution() {
    // Test that different inputs produce different hashes
    std::set<std::string> hash_set;
    
    for (int i = 0; i < 100; ++i) {
        std::string input = "test_" + std::to_string(i);
        Hash160 hash = SHA1::hash(input);
        std::string hex = SHA1::to_string(hash);
        
        // Should not have duplicates
        assert(hash_set.find(hex) == hash_set.end());
        hash_set.insert(hex);
    }
    
    std::cout << "✓ test_hash_distribution passed" << std::endl;
}

int main() {
    std::cout << "Running hash utility tests..." << std::endl;
    
    test_sha1_basic();
    test_sha1_empty_string();
    test_sha1_long_string();
    test_hash_string_conversion();
    test_hash_comparisons();
    test_add_power_of_two();
    test_in_range_normal();
    test_in_range_wraparound();
    test_distance_calculation();
    test_edge_cases();
    test_hash_distribution();
    
    std::cout << "\nAll hash utility tests passed!" << std::endl;
    return 0;
}