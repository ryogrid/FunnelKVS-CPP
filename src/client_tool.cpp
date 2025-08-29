#include "client.h"
#include <iostream>
#include <sstream>
#include <cstdlib>

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options] command [arguments]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h HOST    Server host (default: 127.0.0.1)" << std::endl;
    std::cout << "  -p PORT    Server port (default: 8001)" << std::endl;
    std::cout << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  put KEY VALUE    Store a key-value pair" << std::endl;
    std::cout << "  get KEY          Retrieve value for a key" << std::endl;
    std::cout << "  delete KEY       Delete a key" << std::endl;
    std::cout << "  ping             Check server connectivity" << std::endl;
    std::cout << "  shutdown         Shutdown the server (admin command)" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " put mykey myvalue" << std::endl;
    std::cout << "  " << program_name << " get mykey" << std::endl;
    std::cout << "  " << program_name << " -h 192.168.1.100 -p 8080 get mykey" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    uint16_t port = 8001;
    
    int arg_index = 1;
    while (arg_index < argc && argv[arg_index][0] == '-') {
        std::string option = argv[arg_index];
        
        if (option == "-h" && arg_index + 1 < argc) {
            host = argv[++arg_index];
        } else if (option == "-p" && arg_index + 1 < argc) {
            port = static_cast<uint16_t>(std::atoi(argv[++arg_index]));
        } else if (option == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << option << std::endl;
            print_usage(argv[0]);
            return 1;
        }
        arg_index++;
    }
    
    if (arg_index >= argc) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string command = argv[arg_index++];
    
    funnelkvs::Client client(host, port);
    
    if (!client.connect()) {
        std::cerr << "Failed to connect to server at " << host << ":" << port << std::endl;
        return 1;
    }
    
    try {
        if (command == "put") {
            if (arg_index + 1 >= argc) {
                std::cerr << "PUT requires KEY and VALUE arguments" << std::endl;
                return 1;
            }
            std::string key = argv[arg_index];
            std::string value_str = argv[arg_index + 1];
            std::vector<uint8_t> value(value_str.begin(), value_str.end());
            
            if (client.put(key, value)) {
                std::cout << "OK" << std::endl;
            } else {
                std::cerr << "Failed to store key" << std::endl;
                return 1;
            }
            
        } else if (command == "get") {
            if (arg_index >= argc) {
                std::cerr << "GET requires KEY argument" << std::endl;
                return 1;
            }
            std::string key = argv[arg_index];
            std::vector<uint8_t> value;
            
            if (client.get(key, value)) {
                std::string value_str(value.begin(), value.end());
                std::cout << value_str << std::endl;
            } else {
                std::cerr << "Key not found" << std::endl;
                return 1;
            }
            
        } else if (command == "delete") {
            if (arg_index >= argc) {
                std::cerr << "DELETE requires KEY argument" << std::endl;
                return 1;
            }
            std::string key = argv[arg_index];
            
            if (client.remove(key)) {
                std::cout << "OK" << std::endl;
            } else {
                std::cerr << "Key not found" << std::endl;
                return 1;
            }
            
        } else if (command == "ping") {
            if (client.ping()) {
                std::cout << "PONG" << std::endl;
            } else {
                std::cerr << "Ping failed" << std::endl;
                return 1;
            }
            
        } else if (command == "shutdown") {
            if (client.admin_shutdown()) {
                std::cout << "Shutdown command sent successfully" << std::endl;
            } else {
                std::cerr << "Failed to send shutdown command" << std::endl;
                return 1;
            }
            
        } else {
            std::cerr << "Unknown command: " << command << std::endl;
            print_usage(argv[0]);
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    client.disconnect();
    return 0;
}