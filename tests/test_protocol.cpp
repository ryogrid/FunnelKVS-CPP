#include "../include/protocol.h"
#include <iostream>
#include <cassert>
#include <cstring>

using namespace funnelkvs;

void test_encode_decode_request() {
    std::vector<uint8_t> key = {'t', 'e', 's', 't', '_', 'k', 'e', 'y'};
    std::vector<uint8_t> value = {'t', 'e', 's', 't', '_', 'v', 'a', 'l', 'u', 'e'};
    
    Request original(OpCode::PUT, key, value);
    std::vector<uint8_t> encoded = Protocol::encodeRequest(original);
    
    Request decoded;
    assert(Protocol::decodeRequest(encoded, decoded));
    assert(decoded.opcode == original.opcode);
    assert(decoded.key == original.key);
    assert(decoded.value == original.value);
    
    std::cout << "✓ test_encode_decode_request passed" << std::endl;
}

void test_encode_decode_request_no_value() {
    std::vector<uint8_t> key = {'k', 'e', 'y', '1'};
    
    Request original(OpCode::GET, key);
    std::vector<uint8_t> encoded = Protocol::encodeRequest(original);
    
    Request decoded;
    assert(Protocol::decodeRequest(encoded, decoded));
    assert(decoded.opcode == OpCode::GET);
    assert(decoded.key == key);
    assert(decoded.value.empty());
    
    std::cout << "✓ test_encode_decode_request_no_value passed" << std::endl;
}

void test_encode_decode_response() {
    std::vector<uint8_t> value = {'r', 'e', 's', 'p', 'o', 'n', 's', 'e'};
    
    Response original(StatusCode::SUCCESS, value);
    std::vector<uint8_t> encoded = Protocol::encodeResponse(original);
    
    Response decoded;
    assert(Protocol::decodeResponse(encoded, decoded));
    assert(decoded.status == StatusCode::SUCCESS);
    assert(decoded.value == value);
    
    std::cout << "✓ test_encode_decode_response passed" << std::endl;
}

void test_encode_decode_response_no_value() {
    Response original(StatusCode::KEY_NOT_FOUND);
    std::vector<uint8_t> encoded = Protocol::encodeResponse(original);
    
    Response decoded;
    assert(Protocol::decodeResponse(encoded, decoded));
    assert(decoded.status == StatusCode::KEY_NOT_FOUND);
    assert(decoded.value.empty());
    
    std::cout << "✓ test_encode_decode_response_no_value passed" << std::endl;
}

void test_decode_invalid_request() {
    std::vector<uint8_t> invalid_data = {0x01, 0x00};
    Request req;
    assert(!Protocol::decodeRequest(invalid_data, req));
    
    std::vector<uint8_t> empty_data;
    assert(!Protocol::decodeRequest(empty_data, req));
    
    std::cout << "✓ test_decode_invalid_request passed" << std::endl;
}

void test_decode_invalid_response() {
    std::vector<uint8_t> invalid_data = {0x00};
    Response resp;
    assert(!Protocol::decodeResponse(invalid_data, resp));
    
    std::vector<uint8_t> empty_data;
    assert(!Protocol::decodeResponse(empty_data, resp));
    
    std::cout << "✓ test_decode_invalid_response passed" << std::endl;
}

void test_large_data() {
    std::vector<uint8_t> large_key(1000, 'k');
    std::vector<uint8_t> large_value(10000, 'v');
    
    Request original(OpCode::PUT, large_key, large_value);
    std::vector<uint8_t> encoded = Protocol::encodeRequest(original);
    
    Request decoded;
    assert(Protocol::decodeRequest(encoded, decoded));
    assert(decoded.key.size() == 1000);
    assert(decoded.value.size() == 10000);
    assert(decoded.key == large_key);
    assert(decoded.value == large_value);
    
    std::cout << "✓ test_large_data passed" << std::endl;
}

void test_all_opcodes() {
    std::vector<OpCode> opcodes = {
        OpCode::GET, OpCode::PUT, OpCode::DELETE,
        OpCode::JOIN, OpCode::STABILIZE, OpCode::NOTIFY,
        OpCode::PING, OpCode::REPLICATE
    };
    
    std::vector<uint8_t> key = {'k'};
    for (OpCode op : opcodes) {
        Request original(op, key);
        std::vector<uint8_t> encoded = Protocol::encodeRequest(original);
        Request decoded;
        assert(Protocol::decodeRequest(encoded, decoded));
        assert(decoded.opcode == op);
    }
    
    std::cout << "✓ test_all_opcodes passed" << std::endl;
}

void test_all_status_codes() {
    std::vector<StatusCode> statuses = {
        StatusCode::SUCCESS, StatusCode::KEY_NOT_FOUND,
        StatusCode::ERROR, StatusCode::REDIRECT
    };
    
    for (StatusCode status : statuses) {
        Response original(status);
        std::vector<uint8_t> encoded = Protocol::encodeResponse(original);
        Response decoded;
        assert(Protocol::decodeResponse(encoded, decoded));
        assert(decoded.status == status);
    }
    
    std::cout << "✓ test_all_status_codes passed" << std::endl;
}

int main() {
    std::cout << "Running protocol tests..." << std::endl;
    
    test_encode_decode_request();
    test_encode_decode_request_no_value();
    test_encode_decode_response();
    test_encode_decode_response_no_value();
    test_decode_invalid_request();
    test_decode_invalid_response();
    test_large_data();
    test_all_opcodes();
    test_all_status_codes();
    
    std::cout << "\nAll protocol tests passed!" << std::endl;
    return 0;
}