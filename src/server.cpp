#include "server.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <errno.h>

namespace funnelkvs {

ThreadPool::ThreadPool(size_t threads) : stop(false) {
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back(&ThreadPool::worker_thread, this);
    }
}

ThreadPool::~ThreadPool() {
    stop.store(true);
    condition.notify_all();
    for (std::thread& worker : workers) {
        worker.join();
    }
}

void ThreadPool::worker_thread() {
    while (!stop.load()) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            condition.wait(lock, [this] { return stop.load() || !tasks.empty(); });
            if (stop.load() && tasks.empty()) {
                return;
            }
            if (!tasks.empty()) {
                task = std::move(tasks.front());
                tasks.pop();
            }
        }
        if (task) {
            task();
        }
    }
}

Server::Server(uint16_t port, size_t num_threads) 
    : thread_pool(num_threads), server_fd(-1), running(false), port(port) {
}

Server::~Server() {
    stop();
}

void Server::setup_server_socket() {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        throw std::runtime_error("Failed to create socket");
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(server_fd);
        throw std::runtime_error("Failed to set socket options");
    }
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(server_fd);
        throw std::runtime_error("Failed to bind to port " + std::to_string(port));
    }
    
    if (listen(server_fd, 128) < 0) {
        close(server_fd);
        throw std::runtime_error("Failed to listen on socket");
    }
}

void Server::start() {
    if (running.load()) {
        return;
    }
    
    setup_server_socket();
    running.store(true);
    accept_thread = std::thread(&Server::accept_loop, this);
    std::cout << "Server started on port " << port << std::endl;
}

void Server::stop() {
    if (!running.load()) {
        return;
    }
    
    running.store(false);
    if (server_fd >= 0) {
        shutdown(server_fd, SHUT_RDWR);
        close(server_fd);
    }
    
    if (accept_thread.joinable()) {
        accept_thread.join();
    }
    std::cout << "Server stopped" << std::endl;
}

void Server::accept_loop() {
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    
    if (setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        std::cerr << "Failed to set socket timeout" << std::endl;
    }
    
    while (running.load()) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            if (running.load()) {
                std::cerr << "Failed to accept connection" << std::endl;
            }
            continue;
        }
        
        thread_pool.enqueue([this, client_fd]() {
            handle_client(client_fd);
        });
    }
}

bool Server::receive_data(int fd, std::vector<uint8_t>& buffer, size_t expected_size) {
    buffer.resize(expected_size);
    size_t total_received = 0;
    
    while (total_received < expected_size) {
        ssize_t received = recv(fd, buffer.data() + total_received, 
                                expected_size - total_received, 0);
        if (received <= 0) {
            return false;
        }
        total_received += received;
    }
    return true;
}

bool Server::send_data(int fd, const std::vector<uint8_t>& data) {
    size_t total_sent = 0;
    while (total_sent < data.size()) {
        ssize_t sent = send(fd, data.data() + total_sent, 
                           data.size() - total_sent, 0);
        if (sent <= 0) {
            return false;
        }
        total_sent += sent;
    }
    return true;
}

void Server::handle_client(int client_fd) {
    // Set socket timeouts for client operations (5 seconds)
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        // Non-fatal, continue without timeout
    }
    if (setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        // Non-fatal, continue without timeout
    }
    
    while (true) {
        // Read opcode and key length first
        std::vector<uint8_t> initial_header(5);
        if (!receive_data(client_fd, initial_header, 5)) {
            break;
        }
        
        uint32_t key_len = (static_cast<uint32_t>(initial_header[1]) << 24) |
                           (static_cast<uint32_t>(initial_header[2]) << 16) |
                           (static_cast<uint32_t>(initial_header[3]) << 8) |
                           static_cast<uint32_t>(initial_header[4]);
        
        // Read key and value length
        std::vector<uint8_t> key_and_value_len(key_len + 4);
        if (!receive_data(client_fd, key_and_value_len, key_len + 4)) {
            break;
        }
        
        uint32_t value_len = (static_cast<uint32_t>(key_and_value_len[key_len]) << 24) |
                            (static_cast<uint32_t>(key_and_value_len[key_len + 1]) << 16) |
                            (static_cast<uint32_t>(key_and_value_len[key_len + 2]) << 8) |
                            static_cast<uint32_t>(key_and_value_len[key_len + 3]);
        
        // Read value if present
        std::vector<uint8_t> value_data;
        if (value_len > 0) {
            value_data.resize(value_len);
            if (!receive_data(client_fd, value_data, value_len)) {
                break;
            }
        }
        
        // Reconstruct complete request
        std::vector<uint8_t> full_request = initial_header;
        full_request.insert(full_request.end(), key_and_value_len.begin(), key_and_value_len.end());
        full_request.insert(full_request.end(), value_data.begin(), value_data.end());
        
        Request request;
        if (Protocol::decodeRequest(full_request, request)) {
            process_request(client_fd, request);
        } else {
            Response error_response(StatusCode::ERROR);
            std::vector<uint8_t> encoded = Protocol::encodeResponse(error_response);
            send_data(client_fd, encoded);
            break;
        }
    }
    
    close(client_fd);
}

void Server::process_request(int client_fd, const Request& request) {
    Response response;
    std::string key(request.key.begin(), request.key.end());
    
    switch (request.opcode) {
        case OpCode::GET: {
            std::vector<uint8_t> value;
            if (storage.get(key, value)) {
                response.status = StatusCode::SUCCESS;
                response.value = value;
            } else {
                response.status = StatusCode::KEY_NOT_FOUND;
            }
            break;
        }
        
        case OpCode::PUT: {
            storage.put(key, request.value);
            response.status = StatusCode::SUCCESS;
            break;
        }
        
        case OpCode::DELETE: {
            if (storage.remove(key)) {
                response.status = StatusCode::SUCCESS;
            } else {
                response.status = StatusCode::KEY_NOT_FOUND;
            }
            break;
        }
        
        case OpCode::PING: {
            response.status = StatusCode::SUCCESS;
            break;
        }
        
        default:
            response.status = StatusCode::ERROR;
            break;
    }
    
    std::vector<uint8_t> encoded = Protocol::encodeResponse(response);
    send_data(client_fd, encoded);
}

} // namespace funnelkvs