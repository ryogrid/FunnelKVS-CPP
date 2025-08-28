#ifndef FUNNELKVS_CLIENT_H
#define FUNNELKVS_CLIENT_H

#include "protocol.h"
#include <string>
#include <vector>
#include <cstdint>

namespace funnelkvs {

class Client {
private:
    std::string server_host;
    uint16_t server_port;
    int socket_fd;
    bool connected;
    
public:
    Client(const std::string& host, uint16_t port);
    ~Client();
    
    bool connect();
    void disconnect();
    bool is_connected() const { return connected; }
    
    bool put(const std::string& key, const std::vector<uint8_t>& value);
    bool get(const std::string& key, std::vector<uint8_t>& value);
    bool remove(const std::string& key);
    bool ping();
    
private:
    bool send_request(const Request& request, Response& response);
    bool send_data(const std::vector<uint8_t>& data);
    bool receive_data(std::vector<uint8_t>& buffer, size_t expected_size);
};

} // namespace funnelkvs

#endif // FUNNELKVS_CLIENT_H