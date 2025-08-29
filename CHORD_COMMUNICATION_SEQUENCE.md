# Chord Protocol Communication Sequences

## Overview

This document provides detailed sequence diagrams and explanations for the key communication patterns in the FunnelKVS-CPP Chord DHT implementation. The Chord protocol ensures efficient distributed hash table operations through carefully orchestrated node-to-node communications.

## Key Communication Methods

### Core Chord Operations
- `find_successor(Hash160 id)` - Locates the node responsible for a given key
- `get_predecessor()` - Returns the predecessor node in the Chord ring
- `notify(NodeInfo node)` - Notifies a node of a potential new predecessor
- `stabilize()` - Periodic ring maintenance operation
- `join(NodeInfo existing_node)` - Adds a new node to an existing ring

### Network Communication Methods
- `remote_find_successor(target, id)` - Remote procedure call for successor lookup
- `remote_get_predecessor(target)` - Remote call to get predecessor information
- `remote_notify(target, node)` - Remote notification about ring changes
- `remote_ping(target)` - Health check for remote nodes

## Communication Sequence Diagrams

### 1. Node Join Operation

```mermaid
sequenceDiagram
    participant NewNode as New Node
    participant ExistingNode as Existing Node
    participant Successor as Successor Node
    participant Predecessor as Predecessor Node

    NewNode->>NewNode: initialize()
    NewNode->>NewNode: generate node ID from address:port
    
    NewNode->>ExistingNode: find_successor(new_node_id)
    activate ExistingNode
    
    ExistingNode->>ExistingNode: check if responsible
    alt NewNode ID in range (self, successor]
        ExistingNode-->>NewNode: return successor
    else Need to route request
        ExistingNode->>ExistingNode: closest_preceding_node(new_node_id)
        ExistingNode->>Successor: find_successor(new_node_id)
        activate Successor
        Successor-->>ExistingNode: return responsible node
        deactivate Successor
        ExistingNode-->>NewNode: return responsible node
    end
    deactivate ExistingNode
    
    NewNode->>NewNode: set successor = responsible node
    NewNode->>NewNode: initialize_finger_table()
    NewNode->>NewNode: predecessor = null
    
    Note over NewNode: Start periodic maintenance
    NewNode->>NewNode: start stabilize_thread
    NewNode->>NewNode: start fix_fingers_thread
    NewNode->>NewNode: start failure_detection_thread
    
    Note over NewNode: Ring will self-organize through stabilization
```

### 2. Key Lookup (Find Successor) Operation

```mermaid
sequenceDiagram
    participant Client as Client
    participant NodeA as Node A
    participant NodeB as Node B
    participant NodeC as Responsible Node

    Client->>NodeA: PUT/GET/DELETE key
    activate NodeA
    
    NodeA->>NodeA: key_id = SHA1.hash(key)
    NodeA->>NodeA: is_responsible_for_key(key_id)?
    
    alt Node A is responsible
        NodeA->>NodeA: handle operation locally
        NodeA-->>Client: SUCCESS response
    else Node A not responsible
        NodeA->>NodeA: find_successor(key_id)
        
        alt Key in immediate successor range
            NodeA-->>Client: REDIRECT to successor
        else Need multi-hop lookup
            NodeA->>NodeA: closest_preceding_node(key_id)
            NodeA->>NodeB: find_successor(key_id) [remote call]
            activate NodeB
            
            NodeB->>NodeB: is_responsible_for_key(key_id)?
            alt NodeB is responsible
                NodeB-->>NodeA: return NodeB info
            else Continue routing
                NodeB->>NodeB: closest_preceding_node(key_id)
                NodeB->>NodeC: find_successor(key_id) [remote call]
                activate NodeC
                NodeC-->>NodeB: return NodeC info
                deactivate NodeC
                NodeB-->>NodeA: return NodeC info
            end
            deactivate NodeB
            
            NodeA-->>Client: REDIRECT to responsible node
        end
    end
    deactivate NodeA
    
    Note over Client: Client follows redirect
    Client->>NodeC: PUT/GET/DELETE key [redirected]
    activate NodeC
    NodeC->>NodeC: handle operation locally
    NodeC-->>Client: SUCCESS response
    deactivate NodeC
```

### 3. Ring Stabilization Process

```mermaid
sequenceDiagram
    participant NodeA as Node A
    participant NodeB as Node B (Successor)
    participant NodeC as Node C
    participant NodeX as Node X (New Predecessor)

    Note over NodeA, NodeX: Periodic stabilization (every 1 second)
    
    loop Every stabilize_interval
        NodeA->>NodeA: stabilize()
        NodeA->>NodeA: get current successor
        
        NodeA->>NodeB: get_predecessor() [remote call]
        activate NodeB
        NodeB-->>NodeA: return predecessor (NodeX)
        deactivate NodeB
        
        alt Predecessor X is between A and B
            NodeA->>NodeA: update successor = NodeX
            Note over NodeA: Successor changed to X
        end
        
        NodeA->>NodeX: notify(NodeA info) [remote call]
        activate NodeX
        NodeX->>NodeX: should A be my predecessor?
        
        alt NodeA is better predecessor
            NodeX->>NodeX: update predecessor = NodeA
            NodeX-->>NodeA: ACK
            Note over NodeX: Predecessor updated
        else Keep current predecessor
            NodeX-->>NodeA: ACK (no change)
        end
        deactivate NodeX
    end
    
    Note over NodeA, NodeX: Ring topology stabilizes over time
```

### 4. Data Operation with Replication

```mermaid
sequenceDiagram
    participant Client as Client
    participant PrimaryNode as Primary Node
    participant Replica1 as Replica Node 1
    participant Replica2 as Replica Node 2

    Client->>PrimaryNode: PUT key=value
    activate PrimaryNode
    
    PrimaryNode->>PrimaryNode: key_id = SHA1.hash(key)
    PrimaryNode->>PrimaryNode: is_responsible_for_key(key_id)?
    
    alt Primary is responsible
        PrimaryNode->>PrimaryNode: store in local storage
        
        PrimaryNode->>PrimaryNode: get_replica_nodes(key_id)
        Note over PrimaryNode: Identify successor nodes for replication
        
        par Replicate to Replica 1
            PrimaryNode->>Replica1: REPLICATE key=value
            activate Replica1
            Replica1->>Replica1: store replica
            Replica1-->>PrimaryNode: ACK
            deactivate Replica1
        and Replicate to Replica 2
            PrimaryNode->>Replica2: REPLICATE key=value
            activate Replica2
            Replica2->>Replica2: store replica
            Replica2-->>PrimaryNode: ACK
            deactivate Replica2
        end
        
        alt All replications successful
            PrimaryNode-->>Client: SUCCESS
        else Some replications failed
            PrimaryNode-->>Client: SUCCESS (with warning)
            Note over PrimaryNode: Log replication failures
        end
    else Primary not responsible
        PrimaryNode->>PrimaryNode: find_successor(key_id)
        PrimaryNode-->>Client: REDIRECT to responsible node
    end
    deactivate PrimaryNode
```

### 5. Failure Detection and Recovery

```mermaid
sequenceDiagram
    participant NodeA as Node A
    participant NodeB as Node B (Failed)
    participant NodeC as Node C
    participant NodeD as Node D

    Note over NodeA, NodeD: Periodic failure detection (every 5 seconds)
    
    loop Every failure_check_interval
        NodeA->>NodeB: ping() [remote call]
        activate NodeB
        Note over NodeB: Node B has failed
        NodeB--xNodeA: timeout/connection failed
        deactivate NodeB
        
        NodeA->>NodeA: mark NodeB as failed
        NodeA->>NodeA: get_failed_nodes()
        
        alt NodeB is immediate successor
            Note over NodeA: Need to update successor
            NodeA->>NodeA: get successor_list[1]
            NodeA->>NodeA: promote backup successor
            
            NodeA->>NodeC: ping() [verify new successor]
            activate NodeC
            NodeC-->>NodeA: pong
            deactivate NodeC
            
            NodeA->>NodeA: update successor = NodeC
            
            Note over NodeA: Trigger stabilization
            NodeA->>NodeA: stabilize()
        end
    end
    
    Note over NodeA, NodeD: Failed nodes are bypassed
    Note over NodeA, NodeD: Ring maintains connectivity
    
    NodeA->>NodeC: get_predecessor()
    activate NodeC
    alt NodeC still thinks B is predecessor
        NodeC->>NodeB: ping() [timeout]
        NodeC->>NodeC: mark B as failed
        NodeC->>NodeC: find new predecessor
    end
    NodeC-->>NodeA: return current predecessor
    deactivate NodeC
```

### 6. Finger Table Maintenance

```mermaid
sequenceDiagram
    participant NodeA as Node A
    participant NodeB as Node B
    participant NodeC as Node C
    participant NodeD as Finger Entry

    Note over NodeA, NodeD: Periodic finger table fixup (every 1 second)
    
    loop Every fix_fingers_interval
        NodeA->>NodeA: fix_fingers()
        NodeA->>NodeA: next_finger_to_fix++
        NodeA->>NodeA: calculate target = self_id + 2^i
        
        Note over NodeA: Fix finger[i] entry
        NodeA->>NodeA: find_successor(target)
        
        alt Target in immediate successor range
            NodeA->>NodeA: finger[i] = immediate successor
        else Need to route lookup
            NodeA->>NodeB: find_successor(target) [remote call]
            activate NodeB
            
            NodeB->>NodeB: closest_preceding_node(target)
            NodeB->>NodeC: find_successor(target) [remote call]
            activate NodeC
            NodeC-->>NodeB: return responsible node (NodeD)
            deactivate NodeC
            
            NodeB-->>NodeA: return NodeD
            deactivate NodeB
            
            NodeA->>NodeA: finger[i] = NodeD
        end
        
        NodeA->>NodeA: next_finger_to_fix = (next_finger_to_fix + 1) % 160
    end
    
    Note over NodeA, NodeD: Finger table provides O(log N) routing
```

## Communication Protocol Details

### Message Format
All inter-node communications use the binary protocol defined in `protocol.h`:

```
Request:  [OpCode:1][KeyLen:4][Key:variable][ValueLen:4][Value:variable]
Response: [Status:1][ValueLen:4][Value:variable]
```

### Key OpCodes for Chord Operations
- `FIND_SUCCESSOR (0x20)` - Find the node responsible for a given ID
- `FIND_PREDECESSOR (0x21)` - Find the predecessor node for a given ID  
- `GET_PREDECESSOR (0x22)` - Get the predecessor of the target node
- `GET_SUCCESSOR (0x23)` - Get the successor of the target node
- `CLOSEST_PRECEDING_NODE (0x24)` - Find closest preceding node in finger table
- `NODE_INFO (0x25)` - Get node information (ID, address, port)
- `NOTIFY (0x12)` - Notify a node of a potential new predecessor
- `PING (0x13)` - Health check for failure detection
- `REPLICATE (0x14)` - Replicate data to successor nodes

### Error Handling and Timeouts
- **Connection Timeouts**: 5 seconds for establishing connections
- **Request Timeouts**: 3 seconds for RPC operations  
- **Retry Logic**: Up to 3 retries for critical operations
- **Failure Detection**: Nodes marked as failed after 3 consecutive ping failures
- **Network Partitions**: Detected through consistent ping failures across multiple nodes

## Performance Characteristics

### Routing Efficiency
- **Average Hops**: O(log N) for key lookups using finger tables
- **Stabilization Overhead**: Each node contacts O(log N) nodes periodically
- **Failure Recovery Time**: 2-3 stabilization periods (2-6 seconds)

### Scalability
- **Ring Join Cost**: O(log^2 N) messages to initialize finger tables
- **Maintenance Overhead**: O(log N) messages per node per period
- **Storage Load**: Keys distributed uniformly across N nodes

### Network Communication Patterns
- **Chord Maintenance**: Background periodic messages (1-5 second intervals)
- **Data Operations**: On-demand routing with potential redirects
- **Failure Detection**: Periodic health checks between adjacent nodes
- **Replication**: Synchronous for consistency, asynchronous for performance

This communication architecture provides efficient distributed hash table operations while maintaining strong consistency and fault tolerance through carefully orchestrated node interactions.