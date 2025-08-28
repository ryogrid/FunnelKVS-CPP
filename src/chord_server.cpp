#include "chord_server.h"
#include <iostream>

namespace funnelkvs {

ChordServer::ChordServer(const std::string& address, uint16_t port, size_t num_threads)
    : Server(port, num_threads)
    , chord_enabled(false)
{
    setup_chord_node(address);
}

ChordServer::~ChordServer() {
    if (chord_enabled && chord_node) {
        chord_node->stop_maintenance();
    }
}

void ChordServer::setup_chord_node(const std::string& address) {
    chord_node = std::unique_ptr<ChordNode>(new ChordNode(address, port));
}

void ChordServer::enable_chord() {
    if (!chord_node) {
        std::cerr << "Chord node not initialized" << std::endl;
        return;
    }
    
    chord_enabled = true;
    chord_node->start_maintenance();
    std::cout << "Chord DHT enabled for node " << chord_node->get_info().to_string() << std::endl;
}

void ChordServer::disable_chord() {
    if (!chord_enabled || !chord_node) {
        return;
    }
    
    chord_node->stop_maintenance();
    chord_enabled = false;
    std::cout << "Chord DHT disabled" << std::endl;
}

void ChordServer::create_ring() {
    if (!chord_node) {
        std::cerr << "Chord node not initialized" << std::endl;
        return;
    }
    
    chord_node->create();
    enable_chord();
    std::cout << "Created new Chord ring" << std::endl;
}

void ChordServer::join_ring(const std::string& known_address, uint16_t known_port) {
    if (!chord_node) {
        std::cerr << "Chord node not initialized" << std::endl;
        return;
    }
    
    auto known_node = std::make_shared<NodeInfo>(
        NodeInfo::from_address(known_address, known_port)
    );
    
    chord_node->join(known_node);
    enable_chord();
    std::cout << "Joined Chord ring via " << known_node->to_string() << std::endl;
}

void ChordServer::leave_ring() {
    if (!chord_enabled || !chord_node) {
        return;
    }
    
    chord_node->leave();
    disable_chord();
    std::cout << "Left Chord ring" << std::endl;
}

NodeInfo ChordServer::get_node_info() const {
    if (chord_node) {
        return chord_node->get_info();
    }
    return NodeInfo{};
}

void ChordServer::start() {
    // Start the base server first
    Server::start();
    
    // If Chord is not yet enabled, create a single-node ring
    if (!chord_enabled) {
        create_ring();
    }
}

void ChordServer::stop() {
    disable_chord();
    Server::stop();
}

void ChordServer::process_chord_request(int client_fd, const Request& request) {
    Response response;
    
    if (handle_chord_operation(request, response)) {
        std::vector<uint8_t> encoded = Protocol::encodeResponse(response);
        send_data(client_fd, encoded);
    } else {
        // Handle as regular storage operation
        process_request(client_fd, request);
    }
}

bool ChordServer::handle_chord_operation(const Request& request, Response& response) {
    if (!chord_enabled || !chord_node) {
        return false;
    }
    
    switch (request.opcode) {
        case OpCode::FIND_SUCCESSOR: {
            if (request.key.size() != 20) { // Hash160 is 20 bytes
                response.status = StatusCode::ERROR;
                return true;
            }
            
            Hash160 target_id;
            std::copy(request.key.begin(), request.key.end(), target_id.begin());
            
            auto successor = chord_node->find_successor(target_id);
            if (successor) {
                // Encode NodeInfo in response value
                std::string node_str = successor->address + ":" + std::to_string(successor->port);
                response.value.assign(node_str.begin(), node_str.end());
                response.status = StatusCode::SUCCESS;
            } else {
                response.status = StatusCode::ERROR;
            }
            return true;
        }
        
        case OpCode::GET_PREDECESSOR: {
            auto predecessor = chord_node->get_predecessor();
            if (predecessor) {
                std::string node_str = predecessor->address + ":" + std::to_string(predecessor->port);
                response.value.assign(node_str.begin(), node_str.end());
                response.status = StatusCode::SUCCESS;
            } else {
                response.status = StatusCode::KEY_NOT_FOUND;
            }
            return true;
        }
        
        case OpCode::GET_SUCCESSOR: {
            auto successor = chord_node->get_successor();
            if (successor) {
                std::string node_str = successor->address + ":" + std::to_string(successor->port);
                response.value.assign(node_str.begin(), node_str.end());
                response.status = StatusCode::SUCCESS;
            } else {
                response.status = StatusCode::ERROR;
            }
            return true;
        }
        
        case OpCode::NOTIFY: {
            if (request.value.size() > 0) {
                std::string node_str(request.value.begin(), request.value.end());
                size_t colon_pos = node_str.find(':');
                if (colon_pos != std::string::npos) {
                    std::string addr = node_str.substr(0, colon_pos);
                    uint16_t port = static_cast<uint16_t>(std::stoi(node_str.substr(colon_pos + 1)));
                    auto node = std::make_shared<NodeInfo>(NodeInfo::from_address(addr, port));
                    chord_node->notify(node);
                }
            }
            response.status = StatusCode::SUCCESS;
            return true;
        }
        
        case OpCode::NODE_INFO: {
            NodeInfo info = chord_node->get_info();
            std::string node_str = info.address + ":" + std::to_string(info.port);
            response.value.assign(node_str.begin(), node_str.end());
            response.status = StatusCode::SUCCESS;
            return true;
        }
        
        case OpCode::GET:
        case OpCode::PUT:
        case OpCode::DELETE: {
            // Handle distributed storage operations
            std::string key(request.key.begin(), request.key.end());
            Hash160 key_id = SHA1::hash(key);
            
            if (!chord_node->is_responsible_for_key(key_id)) {
                // Forward to responsible node
                auto responsible = chord_node->find_successor(key_id);
                if (responsible && *responsible != chord_node->get_info()) {
                    response.status = StatusCode::REDIRECT;
                    std::string node_str = responsible->address + ":" + std::to_string(responsible->port);
                    response.value.assign(node_str.begin(), node_str.end());
                    return true;
                }
            }
            
            // Handle locally
            if (request.opcode == OpCode::GET) {
                std::vector<uint8_t> value;
                if (chord_node->retrieve_key(key, value)) {
                    response.status = StatusCode::SUCCESS;
                    response.value = value;
                } else {
                    response.status = StatusCode::KEY_NOT_FOUND;
                }
            } else if (request.opcode == OpCode::PUT) {
                if (chord_node->store_key(key, request.value)) {
                    response.status = StatusCode::SUCCESS;
                } else {
                    response.status = StatusCode::ERROR;
                }
            } else if (request.opcode == OpCode::DELETE) {
                if (chord_node->remove_key(key)) {
                    response.status = StatusCode::SUCCESS;
                } else {
                    response.status = StatusCode::KEY_NOT_FOUND;
                }
            }
            return true;
        }
        
        default:
            return false; // Not a Chord operation
    }
}

// Network operation implementations (simplified for Phase 2)
std::shared_ptr<NodeInfo> ChordServer::remote_find_successor(const NodeInfo& target, const Hash160& id) {
    try {
        Client client(target.address, target.port);
        if (!client.connect()) {
            return nullptr;
        }
        
        // Send FIND_SUCCESSOR request
        std::vector<uint8_t> id_bytes(id.begin(), id.end());
        Request request(OpCode::FIND_SUCCESSOR, id_bytes);
        Response response;
        
        // This is a simplified implementation - full version would use the protocol
        return std::make_shared<NodeInfo>(target); // Placeholder
    } catch (...) {
        return nullptr;
    }
}

std::shared_ptr<NodeInfo> ChordServer::remote_get_predecessor(const NodeInfo& target) {
    try {
        Client client(target.address, target.port);
        if (!client.connect()) {
            return nullptr;
        }
        
        // Send GET_PREDECESSOR request
        Request request(OpCode::GET_PREDECESSOR, {});
        // Placeholder implementation
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

bool ChordServer::remote_notify(const NodeInfo& target, std::shared_ptr<NodeInfo> node) {
    try {
        Client client(target.address, target.port);
        if (!client.connect()) {
            return false;
        }
        
        // Send NOTIFY request
        std::string node_str = node->address + ":" + std::to_string(node->port);
        std::vector<uint8_t> node_data(node_str.begin(), node_str.end());
        Request request(OpCode::NOTIFY, {}, node_data);
        // Placeholder implementation
        return true;
    } catch (...) {
        return false;
    }
}

bool ChordServer::remote_ping(const NodeInfo& target) {
    try {
        Client client(target.address, target.port);
        return client.connect() && client.ping();
    } catch (...) {
        return false;
    }
}

} // namespace funnelkvs