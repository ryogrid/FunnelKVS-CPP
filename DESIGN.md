# FunnelKVS Design Document

## 1. System Overview

FunnelKVS is a simple distributed key-value store implementing the Chord DHT protocol for scalable data distribution. The system provides basic GET, PUT, and DELETE operations with built-in replication for fault tolerance.

### Key Features
- Chord-based distributed hash table
- Consistent hashing with SHA-1 (160-bit key space)
- Configurable replication factor (default: 3)
- Multi-threaded request handling
- In-memory storage only
- Binary protocol for efficient communication

## 2. System Architecture

### 2.1 Components

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   Client    │────▶│  Node (N1)  │────▶│  Node (N2)  │
│   Library   │     │   Port:8001 │     │   Port:8002 │
└─────────────┘     └─────────────┘     └─────────────┘
                            │                    │
                            └────────────────────┘
                              Chord Ring Network
```

### 2.2 Node Architecture

Each node consists of:
- **Network Layer**: TCP socket server with thread pool
- **DHT Layer**: Chord protocol implementation
- **Storage Layer**: Thread-safe in-memory hash map
- **Replication Manager**: Handles data replication to successors
- **Maintenance Thread**: Periodic stabilization and fix_fingers

## 3. Chord DHT Implementation

### 3.1 Node Identifier
- Each node has a 160-bit identifier (SHA-1 hash of IP:port)
- Keys are also hashed to 160-bit identifiers
- Key k is stored at successor(k) - the first node whose ID ≥ k

### 3.2 Routing Table (Finger Table)
Each node maintains:
- **Predecessor**: Previous node in the ring
- **Successor List**: Next R nodes (R = replication factor)
- **Finger Table**: 160 entries for O(log N) routing
  - finger[i] = successor(n + 2^i mod 2^160)

### 3.3 Core Operations

#### Join
```cpp
// Simplified join process
void join(Node* existing) {
    if (existing) {
        predecessor = nullptr;
        successor = existing->find_successor(this->id);
    } else {
        // First node in ring
        predecessor = this;
        successor = this;
    }
}
```

#### Stabilization (runs periodically)
```cpp
void stabilize() {
    Node* x = successor->get_predecessor();
    if (x && in_range(x->id, this->id, successor->id)) {
        successor = x;
    }
    successor->notify(this);
}
```

#### Fix Fingers (runs periodically)
```cpp
void fix_fingers() {
    next_finger = (next_finger + 1) % 160;
    finger[next_finger] = find_successor(id + pow(2, next_finger));
}
```

### 3.4 Key Lookup
```cpp
Node* find_successor(uint160_t key) {
    if (in_range(key, this->id, successor->id)) {
        return successor;
    }
    Node* n = closest_preceding_node(key);
    return n->find_successor(key);
}
```

## 4. Network Protocol

### 4.1 Binary Protocol Format

#### Request Format
```
┌─────────┬──────────┬─────────┬──────────┬─────────┐
│ OpCode  │ KeyLen   │   Key   │ ValueLen │  Value  │
│ (1 byte)│ (4 bytes)│(variable)│(4 bytes) │(variable)│
└─────────┴──────────┴─────────┴──────────┴─────────┘
```

#### Response Format
```
┌─────────┬──────────┬─────────┐
│ Status  │ ValueLen │  Value  │
│ (1 byte)│ (4 bytes)│(variable)│
└─────────┴──────────┴─────────┘
```

### 4.2 Operation Codes
- `0x01`: GET - retrieve value for key
- `0x02`: PUT - store key-value pair
- `0x03`: DELETE - remove key
- `0x10`: JOIN - node join request
- `0x11`: STABILIZE - stabilization protocol
- `0x12`: NOTIFY - predecessor notification
- `0x13`: PING - health check
- `0x14`: REPLICATE - replication request

### 4.3 Status Codes
- `0x00`: SUCCESS
- `0x01`: KEY_NOT_FOUND
- `0x02`: ERROR
- `0x03`: REDIRECT (includes successor info)

## 5. Data Storage

### 5.1 Storage Engine
```cpp
class Storage {
private:
    std::unordered_map<std::string, std::vector<uint8_t>> data;
    std::shared_mutex mutex;  // C++17 reader/writer lock
    
public:
    bool get(const std::string& key, std::vector<uint8_t>& value);
    void put(const std::string& key, const std::vector<uint8_t>& value);
    bool remove(const std::string& key);
};
```

### 5.2 Key Distribution
- Hash function: SHA-1(key) → 160-bit identifier
- Key ownership: successor(hash(key))
- Load balancing through consistent hashing

## 6. Replication Strategy

### 6.1 Replication Model
- **Replication Factor**: R = 3 (configurable)
- **Strategy**: Store copies on R successive nodes
- **Consistency**: Eventually consistent

### 6.2 Write Operation
1. Client sends PUT to any node
2. Node routes to owner = successor(hash(key))
3. Owner stores locally
4. Owner replicates to next R-1 successors
5. Returns success after local write (async replication)

### 6.3 Read Operation
1. Client sends GET to any node
2. Node routes to owner = successor(hash(key))
3. If owner unavailable, try replicas in order
4. Return first successful response

### 6.4 Replica Synchronization
```cpp
void replicate_to_successors() {
    for (int i = 0; i < replication_factor - 1; i++) {
        Node* replica = successor_list[i];
        for (auto& [key, value] : local_data) {
            if (should_replicate(key, replica)) {
                replica->store_replica(key, value);
            }
        }
    }
}
```

## 7. Fault Tolerance

### 7.1 Failure Detection
- Periodic heartbeat/ping to successors and predecessor
- Timeout: 5 seconds (configurable)
- Mark node as failed after 3 consecutive timeouts

### 7.2 Failure Recovery
```cpp
void handle_node_failure(Node* failed) {
    if (failed == successor) {
        // Promote next successor
        successor = successor_list[0];
        successor_list.remove(0);
        successor_list.add(find_next_successor());
    }
    if (failed == predecessor) {
        // Wait for stabilization to fix
        predecessor = nullptr;
    }
    // Trigger re-replication for affected keys
    re_replicate_keys(failed);
}
```

### 7.3 Data Recovery
- When node joins: pull data from successor
- When node fails: replicas already exist on successors
- Periodic anti-entropy: compare and sync with replicas

## 8. Client Design

### 8.1 Client Library
```cpp
class FunnelKVSClient {
private:
    std::vector<std::string> node_addresses;
    ConnectionPool connections;
    
public:
    bool put(const std::string& key, const std::vector<uint8_t>& value);
    bool get(const std::string& key, std::vector<uint8_t>& value);
    bool remove(const std::string& key);
};
```

### 8.2 Connection Management
- Connection pooling with lazy initialization
- Automatic retry with exponential backoff
- Round-robin node selection for load balancing

### 8.3 Client Tool
```bash
# Command-line interface
funnelkvs-cli --node localhost:8001 put "key1" "value1"
funnelkvs-cli --node localhost:8001 get "key1"
funnelkvs-cli --node localhost:8001 delete "key1"
```

## 9. Threading Model

### 9.1 Server Threads
- **Main Thread**: Accept connections
- **Worker Pool**: Handle client requests (size = 2 * CPU cores)
- **Maintenance Thread**: Stabilization, fix_fingers (1 thread)
- **Replication Thread**: Background replication (1 thread)

### 9.2 Thread Safety
```cpp
class ThreadSafeNode {
    mutable std::shared_mutex routing_mutex;  // For finger table
    mutable std::shared_mutex storage_mutex;  // For data store
    
    // Reader operations use shared_lock
    // Writer operations use unique_lock
};
```

## 10. Configuration

### 10.1 Node Configuration
```cpp
struct NodeConfig {
    std::string bind_address = "0.0.0.0";
    uint16_t port = 8001;
    int replication_factor = 3;
    int worker_threads = 8;
    int stabilize_interval_ms = 1000;
    int fix_fingers_interval_ms = 500;
    int ping_interval_ms = 2000;
    int ping_timeout_ms = 5000;
};
```

### 10.2 Client Configuration
```cpp
struct ClientConfig {
    int connection_timeout_ms = 5000;
    int request_timeout_ms = 10000;
    int max_retries = 3;
    int retry_delay_ms = 100;
};
```

## 11. Error Handling

### 11.1 Network Errors
- Connection failures: Retry with next node
- Timeout: Mark node as potentially failed
- Partial writes: Roll back or mark inconsistent

### 11.2 Consistency Issues
- Concurrent updates: Last-write-wins
- Split-brain: Prefer higher node ID
- Stale reads: Accept eventual consistency

## 12. Performance Considerations

### 12.1 Optimizations
- Batch stabilization updates
- Cache finger table lookups
- Parallel replication
- Zero-copy networking where possible

### 12.2 Scalability Targets
- Support 100+ nodes
- 10,000+ operations/second per node
- Sub-millisecond latency for cache hits
- O(log N) routing hops

## 13. Testing Strategy

### 13.1 Unit Tests
- Chord protocol logic
- Storage operations
- Binary protocol encoding/decoding

### 13.2 Integration Tests
- Multi-node setup
- Node join/leave scenarios
- Failure recovery
- Data consistency verification

### 13.3 Stress Tests
- High concurrency
- Network partitions
- Cascading failures
- Large key-value sizes

## 14. Implementation Phases

### Phase 1: Core Infrastructure
- Basic TCP server
- Binary protocol
- Single-node storage

### Phase 2: Chord DHT
- Node join/leave
- Routing
- Stabilization

### Phase 3: Replication
- Successor replication
- Failure detection
- Re-replication

### Phase 4: Production Features
- Monitoring
- Performance optimization
- Stress testing