#include "hash.h"
#include <iomanip>
#include <sstream>
#include <cstring>

namespace funnelkvs {

static uint32_t left_rotate(uint32_t value, int amount) {
    return (value << amount) | (value >> (32 - amount));
}

Hash160 SHA1::hash(const std::string& input) {
    return hash(reinterpret_cast<const uint8_t*>(input.data()), input.size());
}

Hash160 SHA1::hash(const std::vector<uint8_t>& input) {
    return hash(input.data(), input.size());
}

Hash160 SHA1::hash(const uint8_t* data, size_t length) {
    // Initial hash values (first 32 bits of the fractional parts of the square roots of the first 5 primes)
    uint32_t h[5] = {
        0x67452301,
        0xEFCDAB89,
        0x98BADCFE,
        0x10325476,
        0xC3D2E1F0
    };
    
    // Pre-processing: adding padding bits
    uint64_t bit_len = length * 8;
    size_t new_len = length + 1; // +1 for the '1' bit padding
    
    // Pad until message length in bits ≡ 448 (mod 512)
    while (new_len % 64 != 56) {
        new_len++;
    }
    
    std::vector<uint8_t> padded_message(new_len + 8); // +8 for length
    std::memcpy(padded_message.data(), data, length);
    
    padded_message[length] = 0x80; // Add '1' bit padding
    
    // Add original length in bits as 64-bit big-endian integer
    for (int i = 0; i < 8; i++) {
        padded_message[new_len + i] = (bit_len >> (8 * (7 - i))) & 0xFF;
    }
    
    // Process message in 512-bit chunks
    for (size_t chunk_start = 0; chunk_start < padded_message.size(); chunk_start += 64) {
        process_block(padded_message.data() + chunk_start, h);
    }
    
    // Convert hash values to byte array
    Hash160 result;
    for (int i = 0; i < 5; i++) {
        result[i * 4 + 0] = (h[i] >> 24) & 0xFF;
        result[i * 4 + 1] = (h[i] >> 16) & 0xFF;
        result[i * 4 + 2] = (h[i] >> 8) & 0xFF;
        result[i * 4 + 3] = h[i] & 0xFF;
    }
    
    return result;
}

void SHA1::process_block(const uint8_t block[64], uint32_t h[5]) {
    uint32_t w[80];
    
    // Break chunk into sixteen 32-bit big-endian words
    for (int i = 0; i < 16; i++) {
        w[i] = (block[i * 4] << 24) |
               (block[i * 4 + 1] << 16) |
               (block[i * 4 + 2] << 8) |
               block[i * 4 + 3];
    }
    
    // Extend the sixteen 32-bit words into eighty 32-bit words
    for (int i = 16; i < 80; i++) {
        w[i] = left_rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    
    // Initialize hash value for this chunk
    uint32_t a = h[0];
    uint32_t b = h[1];
    uint32_t c = h[2];
    uint32_t d = h[3];
    uint32_t e = h[4];
    
    // Main loop
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        
        uint32_t temp = left_rotate(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = left_rotate(b, 30);
        b = a;
        a = temp;
    }
    
    // Add this chunk's hash to result so far
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

std::string SHA1::to_string(const Hash160& hash) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < hash.size(); ++i) {
        ss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return ss.str();
}

Hash160 SHA1::from_string(const std::string& hex_str) {
    Hash160 result = {};
    if (hex_str.length() != 40) {
        return result; // Invalid length
    }
    
    for (size_t i = 0; i < 20; ++i) {
        std::string byte_str = hex_str.substr(i * 2, 2);
        result[i] = static_cast<uint8_t>(std::stoi(byte_str, nullptr, 16));
    }
    
    return result;
}

// Comparison operators
bool operator==(const Hash160& a, const Hash160& b) {
    return std::memcmp(a.data(), b.data(), 20) == 0;
}

bool operator!=(const Hash160& a, const Hash160& b) {
    return !(a == b);
}

bool operator<(const Hash160& a, const Hash160& b) {
    return std::memcmp(a.data(), b.data(), 20) < 0;
}

bool operator<=(const Hash160& a, const Hash160& b) {
    return std::memcmp(a.data(), b.data(), 20) <= 0;
}

bool operator>(const Hash160& a, const Hash160& b) {
    return std::memcmp(a.data(), b.data(), 20) > 0;
}

bool operator>=(const Hash160& a, const Hash160& b) {
    return std::memcmp(a.data(), b.data(), 20) >= 0;
}

// Chord-specific operations
Hash160 add_power_of_two(const Hash160& base, int power) {
    if (power < 0 || power >= 160) {
        return base; // Invalid power
    }
    
    Hash160 result = base;
    
    // Calculate which byte and bit to modify
    int byte_index = 19 - (power / 8);
    int bit_index = power % 8;
    
    // Add 2^power (carry propagation)
    bool carry = true;
    for (int i = byte_index; i >= 0 && carry; i--) {
        if (i == byte_index) {
            uint16_t sum = result[i] + (1 << bit_index);
            result[i] = sum & 0xFF;
            carry = (sum > 0xFF);
        } else {
            uint16_t sum = result[i] + (carry ? 1 : 0);
            result[i] = sum & 0xFF;
            carry = (sum > 0xFF);
        }
    }
    
    return result;
}

bool in_range(const Hash160& id, const Hash160& start, const Hash160& end, bool inclusive_end) {
    if (start == end) {
        return inclusive_end ? (id == start) : false;
    }
    
    if (start < end) {
        // Normal range: start < end
        return (id > start) && (inclusive_end ? (id <= end) : (id < end));
    } else {
        // Wrap-around range: start > end
        return (id > start) || (inclusive_end ? (id <= end) : (id < end));
    }
}

Hash160 distance(const Hash160& from, const Hash160& to) {
    if (to >= from) {
        // Simple subtraction
        Hash160 result = {};
        bool borrow = false;
        
        for (int i = 19; i >= 0; i--) {
            int diff = to[i] - from[i] - (borrow ? 1 : 0);
            if (diff < 0) {
                diff += 256;
                borrow = true;
            } else {
                borrow = false;
            }
            result[i] = static_cast<uint8_t>(diff);
        }
        return result;
    } else {
        // Wrap-around: calculate (2^160 - from) + to
        Hash160 max_hash = {};
        std::memset(max_hash.data(), 0xFF, 20);
        
        // Calculate max_hash - from + 1 + to
        Hash160 temp = distance(from, max_hash);
        return add_power_of_two(distance({}, to), 0); // Simplified for wrap-around
    }
}

} // namespace funnelkvs