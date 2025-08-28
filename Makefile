CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -O2 -pthread -I./include
LDFLAGS = -pthread

SRC_DIR = src
INC_DIR = include
TEST_DIR = tests
BUILD_DIR = build
BIN_DIR = bin

SERVER_MAIN = $(SRC_DIR)/server_main.cpp
CLIENT_MAIN = $(SRC_DIR)/client_main.cpp

SOURCES = $(filter-out $(SERVER_MAIN) $(CLIENT_MAIN), $(wildcard $(SRC_DIR)/*.cpp))
OBJECTS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
TEST_SOURCES = $(wildcard $(TEST_DIR)/*.cpp)
TEST_BINARIES = $(patsubst $(TEST_DIR)/%.cpp,$(BIN_DIR)/%,$(TEST_SOURCES))

.PHONY: all clean test server client dirs

all: dirs server client test

dirs:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

server: dirs $(OBJECTS) $(SERVER_MAIN)
	$(CXX) $(CXXFLAGS) $(SERVER_MAIN) $(OBJECTS) -o $(BIN_DIR)/funnelkvs-server $(LDFLAGS)

client: dirs $(OBJECTS) $(CLIENT_MAIN)
	$(CXX) $(CXXFLAGS) $(CLIENT_MAIN) $(OBJECTS) -o $(BIN_DIR)/funnelkvs-client $(LDFLAGS)

$(BIN_DIR)/test_protocol: $(TEST_DIR)/test_protocol.cpp $(BUILD_DIR)/protocol.o
	$(CXX) $(CXXFLAGS) $< $(BUILD_DIR)/protocol.o -o $@ $(LDFLAGS)

$(BIN_DIR)/test_storage: $(TEST_DIR)/test_storage.cpp $(BUILD_DIR)/storage.o
	$(CXX) $(CXXFLAGS) $< $(BUILD_DIR)/storage.o -o $@ $(LDFLAGS)

$(BIN_DIR)/test_integration: $(TEST_DIR)/test_integration.cpp $(OBJECTS)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS)

test: dirs $(BIN_DIR)/test_protocol $(BIN_DIR)/test_storage $(BIN_DIR)/test_integration
	@echo "Running unit tests..."
	@$(BIN_DIR)/test_protocol
	@echo ""
	@$(BIN_DIR)/test_storage
	@echo ""
	@$(BIN_DIR)/test_integration

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

help:
	@echo "FunnelKVS Build System"
	@echo "====================="
	@echo "Available targets:"
	@echo "  make         - Build everything (server, client, and tests)"
	@echo "  make server  - Build the server executable"
	@echo "  make client  - Build the client executable"
	@echo "  make test    - Build and run all tests"
	@echo "  make clean   - Remove all build artifacts"
	@echo "  make help    - Show this help message"