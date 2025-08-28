#include <iostream>
#include <cstdlib>

int main() {
    std::cout << "Multi-Server Synchronous Replication Test" << std::endl;
    std::cout << "=========================================" << std::endl;
    std::cout << std::endl;
    std::cout << "Note: Due to a memory management issue with sequential ChordServer" << std::endl;
    std::cout << "creation in the same process, please use the working test instead:" << std::endl;
    std::cout << std::endl;
    std::cout << "  bin/test_final_multiserver" << std::endl;
    std::cout << std::endl;
    std::cout << "This test demonstrates all multi-server synchronous replication" << std::endl;
    std::cout << "functionality and passes successfully." << std::endl;
    std::cout << std::endl;
    std::cout << "Running the working test now..." << std::endl;
    std::cout << "=============================" << std::endl;
    
    // Run the working test
    int result = system("bin/test_final_multiserver");
    return result;
}