#ifndef FUNNELKVS_CHORD_SERVER_H
#define FUNNELKVS_CHORD_SERVER_H

#include "server.h"
#include "chord.h"
#include "client.h"
#include <memory>

namespace funnelkvs {

class ChordServer : public Server {
private:
    std::unique_ptr<ChordNode> chord_node;
    bool chord_enabled;
    
public:
    ChordServer(const std::string& address, uint16_t port, size_t num_threads = 8);
    ~ChordServer();
    
    // Chord-specific methods
    void enable_chord();
    void disable_chord();
    void create_ring();
    void join_ring(const std::string& known_address, uint16_t known_port);
    void leave_ring();
    
    // Node information
    NodeInfo get_node_info() const;
    bool is_chord_enabled() const { return chord_enabled; }
    
    // Override server methods to handle Chord operations
    void start() override;
    void stop() override;
    
protected:
    void process_request(int client_fd, const Request& request) override;
    bool handle_chord_operation(const Request& request, Response& response);
    
    // Chord network operations
    std::shared_ptr<NodeInfo> remote_find_successor(const NodeInfo& target, const Hash160& id);
    std::shared_ptr<NodeInfo> remote_get_predecessor(const NodeInfo& target);
    bool remote_notify(const NodeInfo& target, std::shared_ptr<NodeInfo> node);
    bool remote_ping(const NodeInfo& target);
    
private:
    void setup_chord_node(const std::string& address);
};

} // namespace funnelkvs

#endif // FUNNELKVS_CHORD_SERVER_H