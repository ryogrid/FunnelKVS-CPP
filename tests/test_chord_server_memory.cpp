#include "../include/chord_server.h"
#include <iostream>
#include <memory>

using namespace funnelkvs;

int main() {
    std::cout << "Testing ChordServer memory management" << std::endl;
    
    try {
        // Test 1: Single ChordServer creation and destruction
        std::cout << "1. Creating single ChordServer..." << std::endl;
        {
            std::unique_ptr<ChordServer> server(new ChordServer("127.0.0.1", 10000));
            std::cout << "   Created server on port 10000" << std::endl;
        }
        std::cout << "   Server destroyed successfully" << std::endl;
        
        // Test 2: Sequential ChordServer creation
        std::cout << "2. Creating servers sequentially..." << std::endl;
        for (int i = 0; i < 3; i++) {
            std::cout << "   Creating server " << (i+1) << "..." << std::endl;
            std::unique_ptr<ChordServer> server(new ChordServer("127.0.0.1", 10000 + i));
            std::cout << "   Created server on port " << (10000 + i) << std::endl;
            // Server will be destroyed when exiting scope
        }
        std::cout << "   All servers destroyed successfully" << std::endl;
        
        std::cout << "✅ All tests passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred" << std::endl;
        return 1;
    }
    
    return 0;
}