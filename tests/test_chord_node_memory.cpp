#include "../include/chord.h"
#include <iostream>
#include <memory>

using namespace funnelkvs;

int main() {
    std::cout << "Testing ChordNode memory management" << std::endl;
    
    try {
        // Test 1: Single ChordNode creation and destruction
        std::cout << "1. Creating single ChordNode..." << std::endl;
        {
            std::unique_ptr<ChordNode> node(new ChordNode("127.0.0.1", 10000));
            std::cout << "   Created node: " << node->get_info().to_string() << std::endl;
        }
        std::cout << "   Node destroyed successfully" << std::endl;
        
        // Test 2: Sequential ChordNode creation
        std::cout << "2. Creating nodes sequentially..." << std::endl;
        for (int i = 0; i < 3; i++) {
            std::cout << "   Creating node " << (i+1) << "..." << std::endl;
            std::unique_ptr<ChordNode> node(new ChordNode("127.0.0.1", 10000 + i));
            std::cout << "   Created: " << node->get_info().to_string() << std::endl;
            // Node will be destroyed when exiting scope
        }
        std::cout << "   All nodes destroyed successfully" << std::endl;
        
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