#ifndef FUNNELKVS_HASH_H
#define FUNNELKVS_HASH_H

#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace funnelkvs {

// 160-bit (20-byte) hash value for Chord DHT
using Hash160 = std::array<uint8_t, 20>;

class SHA1 {
public:
    static Hash160 hash(const std::string& input);
    static Hash160 hash(const std::vector<uint8_t>& input);
    static Hash160 hash(const uint8_t* data, size_t length);
    
    static std::string to_string(const Hash160& hash);
    static Hash160 from_string(const std::string& hex_str);
    
private:
    static void process_block(const uint8_t block[64], uint32_t h[5]);
};

// Utility functions for Hash160 operations
bool operator==(const Hash160& a, const Hash160& b);
bool operator!=(const Hash160& a, const Hash160& b);
bool operator<(const Hash160& a, const Hash160& b);
bool operator<=(const Hash160& a, const Hash160& b);
bool operator>(const Hash160& a, const Hash160& b);
bool operator>=(const Hash160& a, const Hash160& b);

// Chord-specific hash operations
Hash160 add_power_of_two(const Hash160& base, int power);
bool in_range(const Hash160& id, const Hash160& start, const Hash160& end, bool inclusive_end = true);
Hash160 distance(const Hash160& from, const Hash160& to);

} // namespace funnelkvs

#endif // FUNNELKVS_HASH_H