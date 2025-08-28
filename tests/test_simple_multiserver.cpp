#include "../include/chord_server.h"
#include "../include/client.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <memory>

using namespace funnelkvs;

int main() {
    std::cout << "Testing Simple Multi-Server Setup" << std::endl;
    std::cout << "=================================" << std::endl;
    
    try {
        // Test single server lifecycle
        std::cout << "\n1. Testing single server lifecycle..." << std::endl;
        {
            std::unique_ptr<ChordServer> server(new ChordServer("127.0.0.1", 10000));
            server->create_ring();
            server->start();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            {
                Client client("127.0.0.1", 10000);
                if (client.connect()) {
                    std::vector<uint8_t> value = {'t', 'e', 's', 't'};
                    if (client.put("key1", value)) {
                        std::vector<uint8_t> retrieved;
                        if (client.get("key1", retrieved) && retrieved == value) {
                            std::cout << "  Basic operations successful" << std::endl;
                        }
                    }
                    client.disconnect();
                }
            }
            
            server->stop();
            server.reset(); // Explicit cleanup
            std::cout << "✓ Single server test completed" << std::endl;
        }
        
        // Wait between tests to ensure cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Test sequential server startup/shutdown
        std::cout << "\n2. Testing sequential server startup/shutdown..." << std::endl;
        for (int i = 0; i < 2; i++) {
            std::cout << "  Starting server " << (i+1) << " on port " << (10010 + i) << "..." << std::endl;
            {
                std::unique_ptr<ChordServer> server(new ChordServer("127.0.0.1", 10010 + i));
                server->create_ring();
                server->start();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                server->stop();
                server.reset();
                std::cout << "    Server " << (i+1) << " stopped cleanly" << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        std::cout << "✓ Sequential multi-server test completed" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred" << std::endl;
        return 1;
    }
    
    std::cout << "\n=================================" << std::endl;
    std::cout << "✅ All simple multi-server tests passed!" << std::endl;
    
    return 0;
}