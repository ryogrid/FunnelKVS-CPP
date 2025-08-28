#include "server.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <cstdlib>

std::atomic<bool> shutdown_requested(false);
funnelkvs::Server* g_server = nullptr;

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
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -p PORT    Server port (default: 8001)" << std::endl;
    std::cout << "  -t THREADS Number of worker threads (default: 8)" << std::endl;
    std::cout << "  -h         Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    uint16_t port = 8001;
    size_t num_threads = 8;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-p" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
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
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        funnelkvs::Server server(port, num_threads);
        g_server = &server;
        
        std::cout << "FunnelKVS Server" << std::endl;
        std::cout << "Port: " << port << std::endl;
        std::cout << "Worker threads: " << num_threads << std::endl;
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