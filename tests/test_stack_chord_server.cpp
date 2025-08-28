#include "../include/chord_server.h"
#include <iostream>

using namespace funnelkvs;

int main() {
    std::cout << "Testing ChordServer stack allocation" << std::endl;
    
    try {
        // Test stack allocation to avoid heap corruption
        std::cout << "1. Creating ChordServer on stack..." << std::endl;
        {
            ChordServer server("127.0.0.1", 10000);
            std::cout << "   Created server on port 10000" << std::endl;
        }
        std::cout << "   Server destroyed successfully" << std::endl;
        
        std::cout << "2. Creating second ChordServer on stack..." << std::endl;
        {
            ChordServer server("127.0.0.1", 10001);
            std::cout << "   Created server on port 10001" << std::endl;
        }
        std::cout << "   Server destroyed successfully" << std::endl;
        
        std::cout << "✅ Stack allocation test passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown error occurred" << std::endl;
        return 1;
    }
    
    return 0;
}