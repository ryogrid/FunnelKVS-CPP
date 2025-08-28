#include "client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

namespace funnelkvs {

Client::Client(const std::string& host, uint16_t port)
    : server_host(host), server_port(port), socket_fd(-1), connected(false) {
}

Client::~Client() {
    disconnect();
}

bool Client::connect() {
    if (connected) {
        return true;
    }
    
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        return false;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    if (inet_pton(AF_INET, server_host.c_str(), &server_addr.sin_addr) <= 0) {
        close(socket_fd);
        socket_fd = -1;
        return false;
    }
    
    if (::connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(socket_fd);
        socket_fd = -1;
        return false;
    }
    
    connected = true;
    return true;
}

void Client::disconnect() {
    if (connected && socket_fd >= 0) {
        close(socket_fd);
        socket_fd = -1;
        connected = false;
    }
}

bool Client::send_data(const std::vector<uint8_t>& data) {
    size_t total_sent = 0;
    while (total_sent < data.size()) {
        ssize_t sent = send(socket_fd, data.data() + total_sent,
                           data.size() - total_sent, 0);
        if (sent <= 0) {
            return false;
        }
        total_sent += sent;
    }
    return true;
}

bool Client::receive_data(std::vector<uint8_t>& buffer, size_t expected_size) {
    buffer.resize(expected_size);
    size_t total_received = 0;
    
    while (total_received < expected_size) {
        ssize_t received = recv(socket_fd, buffer.data() + total_received,
                                expected_size - total_received, 0);
        if (received <= 0) {
            return false;
        }
        total_received += received;
    }
    return true;
}

bool Client::send_request(const Request& request, Response& response) {
    if (!connected) {
        return false;
    }
    
    std::vector<uint8_t> encoded_request = Protocol::encodeRequest(request);
    if (!send_data(encoded_request)) {
        disconnect();
        return false;
    }
    
    std::vector<uint8_t> header(5);
    if (!receive_data(header, 5)) {
        disconnect();
        return false;
    }
    
    uint32_t value_len = (static_cast<uint32_t>(header[1]) << 24) |
                        (static_cast<uint32_t>(header[2]) << 16) |
                        (static_cast<uint32_t>(header[3]) << 8) |
                        static_cast<uint32_t>(header[4]);
    
    std::vector<uint8_t> payload;
    if (value_len > 0) {
        if (!receive_data(payload, value_len)) {
            disconnect();
            return false;
        }
    }
    
    std::vector<uint8_t> full_response = header;
    full_response.insert(full_response.end(), payload.begin(), payload.end());
    
    if (!Protocol::decodeResponse(full_response, response)) {
        disconnect();
        return false;
    }
    
    return true;
}

bool Client::put(const std::string& key, const std::vector<uint8_t>& value) {
    std::vector<uint8_t> key_bytes(key.begin(), key.end());
    Request request(OpCode::PUT, key_bytes, value);
    Response response;
    
    if (!send_request(request, response)) {
        return false;
    }
    
    return response.status == StatusCode::SUCCESS;
}

bool Client::get(const std::string& key, std::vector<uint8_t>& value) {
    std::vector<uint8_t> key_bytes(key.begin(), key.end());
    Request request(OpCode::GET, key_bytes);
    Response response;
    
    if (!send_request(request, response)) {
        return false;
    }
    
    if (response.status == StatusCode::SUCCESS) {
        value = response.value;
        return true;
    }
    
    return false;
}

bool Client::remove(const std::string& key) {
    std::vector<uint8_t> key_bytes(key.begin(), key.end());
    Request request(OpCode::DELETE, key_bytes);
    Response response;
    
    if (!send_request(request, response)) {
        return false;
    }
    
    return response.status == StatusCode::SUCCESS;
}

bool Client::ping() {
    Request request(OpCode::PING, {});
    Response response;
    
    if (!send_request(request, response)) {
        return false;
    }
    
    return response.status == StatusCode::SUCCESS;
}

} // namespace funnelkvs