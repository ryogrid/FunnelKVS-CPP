#include "protocol.h"
#include <algorithm>

namespace funnelkvs {

void Protocol::writeUint32(std::vector<uint8_t>& buffer, uint32_t value) {
    buffer.push_back((value >> 24) & 0xFF);
    buffer.push_back((value >> 16) & 0xFF);
    buffer.push_back((value >> 8) & 0xFF);
    buffer.push_back(value & 0xFF);
}

bool Protocol::readUint32(const uint8_t* data, size_t& offset, size_t len, uint32_t& value) {
    if (offset + 4 > len) {
        return false;
    }
    value = (static_cast<uint32_t>(data[offset]) << 24) |
            (static_cast<uint32_t>(data[offset + 1]) << 16) |
            (static_cast<uint32_t>(data[offset + 2]) << 8) |
            static_cast<uint32_t>(data[offset + 3]);
    offset += 4;
    return true;
}

std::vector<uint8_t> Protocol::encodeRequest(const Request& req) {
    std::vector<uint8_t> buffer;
    
    buffer.push_back(static_cast<uint8_t>(req.opcode));
    
    writeUint32(buffer, static_cast<uint32_t>(req.key.size()));
    buffer.insert(buffer.end(), req.key.begin(), req.key.end());
    
    writeUint32(buffer, static_cast<uint32_t>(req.value.size()));
    buffer.insert(buffer.end(), req.value.begin(), req.value.end());
    
    return buffer;
}

bool Protocol::decodeRequest(const std::vector<uint8_t>& data, Request& req) {
    if (data.empty()) {
        return false;
    }
    
    size_t offset = 0;
    const uint8_t* ptr = data.data();
    size_t len = data.size();
    
    if (offset >= len) {
        return false;
    }
    req.opcode = static_cast<OpCode>(ptr[offset++]);
    
    uint32_t keyLen = 0;
    if (!readUint32(ptr, offset, len, keyLen)) {
        return false;
    }
    
    if (offset + keyLen > len) {
        return false;
    }
    req.key.assign(ptr + offset, ptr + offset + keyLen);
    offset += keyLen;
    
    uint32_t valueLen = 0;
    if (!readUint32(ptr, offset, len, valueLen)) {
        return false;
    }
    
    if (offset + valueLen > len) {
        return false;
    }
    req.value.assign(ptr + offset, ptr + offset + valueLen);
    
    return true;
}

std::vector<uint8_t> Protocol::encodeResponse(const Response& resp) {
    std::vector<uint8_t> buffer;
    
    buffer.push_back(static_cast<uint8_t>(resp.status));
    
    writeUint32(buffer, static_cast<uint32_t>(resp.value.size()));
    buffer.insert(buffer.end(), resp.value.begin(), resp.value.end());
    
    return buffer;
}

bool Protocol::decodeResponse(const std::vector<uint8_t>& data, Response& resp) {
    if (data.empty()) {
        return false;
    }
    
    size_t offset = 0;
    const uint8_t* ptr = data.data();
    size_t len = data.size();
    
    if (offset >= len) {
        return false;
    }
    resp.status = static_cast<StatusCode>(ptr[offset++]);
    
    uint32_t valueLen = 0;
    if (!readUint32(ptr, offset, len, valueLen)) {
        return false;
    }
    
    if (offset + valueLen > len) {
        return false;
    }
    resp.value.assign(ptr + offset, ptr + offset + valueLen);
    
    return true;
}

} // namespace funnelkvs