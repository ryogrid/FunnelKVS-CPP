#ifndef FUNNELKVS_PROTOCOL_H
#define FUNNELKVS_PROTOCOL_H

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>

namespace funnelkvs {

enum class OpCode : uint8_t {
    GET = 0x01,
    PUT = 0x02,
    DELETE = 0x03,
    JOIN = 0x10,
    STABILIZE = 0x11,
    NOTIFY = 0x12,
    PING = 0x13,
    REPLICATE = 0x14
};

enum class StatusCode : uint8_t {
    SUCCESS = 0x00,
    KEY_NOT_FOUND = 0x01,
    ERROR = 0x02,
    REDIRECT = 0x03
};

struct Request {
    OpCode opcode;
    std::vector<uint8_t> key;
    std::vector<uint8_t> value;
    
    Request() : opcode(OpCode::GET) {}
    Request(OpCode op, const std::vector<uint8_t>& k, const std::vector<uint8_t>& v = {})
        : opcode(op), key(k), value(v) {}
};

struct Response {
    StatusCode status;
    std::vector<uint8_t> value;
    
    Response() : status(StatusCode::ERROR) {}
    Response(StatusCode s, const std::vector<uint8_t>& v = {})
        : status(s), value(v) {}
};

class Protocol {
public:
    static std::vector<uint8_t> encodeRequest(const Request& req);
    static bool decodeRequest(const std::vector<uint8_t>& data, Request& req);
    
    static std::vector<uint8_t> encodeResponse(const Response& resp);
    static bool decodeResponse(const std::vector<uint8_t>& data, Response& resp);
    
private:
    static void writeUint32(std::vector<uint8_t>& buffer, uint32_t value);
    static bool readUint32(const uint8_t* data, size_t& offset, size_t len, uint32_t& value);
};

} // namespace funnelkvs

#endif // FUNNELKVS_PROTOCOL_H