#include "chord_server.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <cstdlib>
#include <string>

std::atomic<bool> shutdown_requested(false);
funnelkvs::ChordServer* g_server = nullptr;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nShutdown signal received..." << std::endl;
        shutdown_requested.store(true);
        if (g_server) {
            g_server->stop();
        }
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " -p PORT [-j EXISTING_NODE]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p PORT          Server port (required)" << std::endl;
    std::cout << "  -j NODE          Join existing ring via NODE (format: host:port)" << std::endl;
    std::cout << "  -t THREADS       Number of worker threads (default: 8)" << std::endl;
    std::cout << "  -h               Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  # Start first node (creates new ring)" << std::endl;
    std::cout << "  " << program_name << " -p 8001" << std::endl;
    std::cout << "  # Join existing ring" << std::endl;
    std::cout << "  " << program_name << " -p 8002 -j 127.0.0.1:8001" << std::endl;
}

int main(int argc, char* argv[]) {
    uint16_t port = 0;
    size_t num_threads = 8;
    std::string join_node;
    std::string host = "127.0.0.1";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-p" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "-j" && i + 1 < argc) {
            join_node = argv[++i];
        } else if (arg == "-t" && i + 1 < argc) {
            num_threads = static_cast<size_t>(std::atoi(argv[++i]));
        } else if (arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (port == 0) {
        std::cerr << "Error: Port is required" << std::endl;
        print_usage(argv[0]);
        return 1;
    }
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        funnelkvs::ChordServer server(host, port, num_threads);
        g_server = &server;
        
        std::cout << "FunnelKVS Chord Server" << std::endl;
        std::cout << "Address: " << host << ":" << port << std::endl;
        std::cout << "Worker threads: " << num_threads << std::endl;
        
        if (!join_node.empty()) {
            // Parse join node address
            size_t colon_pos = join_node.find(':');
            if (colon_pos != std::string::npos) {
                std::string join_host = join_node.substr(0, colon_pos);
                uint16_t join_port = static_cast<uint16_t>(std::atoi(join_node.substr(colon_pos + 1).c_str()));
                std::cout << "Joining ring via: " << join_host << ":" << join_port << std::endl;
                server.join_ring(join_host, join_port);
            } else {
                std::cerr << "Invalid join node format. Use host:port" << std::endl;
                return 1;
            }
        } else {
            std::cout << "Creating new Chord ring" << std::endl;
            server.create_ring();
        }
        
        std::cout << "Press Ctrl+C to stop" << std::endl;
        std::cout << std::endl;
        
        server.start();
        
        while (!shutdown_requested.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        server.stop();
        g_server = nullptr;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}