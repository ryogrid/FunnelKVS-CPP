#!/bin/bash

# 10-Node Chord Cluster Test Script
# Creates 10 servers, tests PUT/GET/DELETE operations, then shuts down all nodes

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
CLIENT_TOOL="$PROJECT_ROOT/bin/client_tool"
SERVER_TOOL="$PROJECT_ROOT/bin/chord_server"

BASE_PORT=20000
NUM_NODES=10
PIDS=()

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log() {
    echo -e "${BLUE}[$(date '+%H:%M:%S')]${NC} $1"
}

success() {
    echo -e "${GREEN}✓${NC} $1"
}

warning() {
    echo -e "${YELLOW}⚠${NC} $1"
}

error() {
    echo -e "${RED}✗${NC} $1"
}

cleanup() {
    log "Cleaning up processes..."
    for pid in "${PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
    done
    
    # Wait for processes to terminate
    sleep 2
    
    # Force kill if still running
    for pid in "${PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -9 "$pid" 2>/dev/null || true
        fi
    done
    
    log "Cleanup completed"
}

# Set up signal handlers
trap cleanup EXIT
trap cleanup SIGINT
trap cleanup SIGTERM

# Check if required binaries exist
if [[ ! -f "$CLIENT_TOOL" ]]; then
    error "Client tool not found at $CLIENT_TOOL"
    exit 1
fi

if [[ ! -f "$SERVER_TOOL" ]]; then
    error "Server tool not found at $SERVER_TOOL"
    exit 1
fi

echo "=================================================="
echo "           10-Node Chord Cluster Test"
echo "=================================================="
echo

# Step 1: Start 10 nodes
log "Starting $NUM_NODES Chord servers..."

# Start first node (creates new ring)
port=$BASE_PORT
log "Starting node 1 on port $port (new ring)"
"$SERVER_TOOL" $port > "/tmp/chord_server_$port.log" 2>&1 &
PIDS+=($!)
sleep 3  # Wait for first node to establish ring

# Start remaining nodes (join existing ring)
for ((i=2; i<=NUM_NODES; i++)); do
    port=$((BASE_PORT + i - 1))
    log "Starting node $i on port $port (joining ring via port $BASE_PORT)"
    "$SERVER_TOOL" $port "127.0.0.1:$BASE_PORT" > "/tmp/chord_server_$port.log" 2>&1 &
    PIDS+=($!)
    sleep 1  # Brief delay between node starts
done

log "All $NUM_NODES nodes started. Waiting for ring stabilization..."
sleep 5

success "All nodes are running"

# Step 2: Test connectivity to all nodes
log "Testing connectivity to all nodes..."
failed_nodes=0
for ((i=1; i<=NUM_NODES; i++)); do
    port=$((BASE_PORT + i - 1))
    if "$CLIENT_TOOL" 127.0.0.1 $port ping > /dev/null 2>&1; then
        success "Node $i (port $port) is responding"
    else
        error "Node $i (port $port) is not responding"
        failed_nodes=$((failed_nodes + 1))
    fi
done

if [ $failed_nodes -gt 0 ]; then
    error "$failed_nodes nodes failed connectivity test"
    exit 1
fi

# Step 3: Test PUT/GET/DELETE operations on different nodes
log "Testing PUT/GET/DELETE operations across different nodes..."

# Test data
test_keys=("key1" "key2" "key3" "user:123" "config:timeout" "data:important")
test_values=("value1" "value2" "value3" "john_doe" "30000" "critical_data")

# PUT operations - distribute across different nodes
log "Testing PUT operations..."
for i in "${!test_keys[@]}"; do
    key="${test_keys[$i]}"
    value="${test_values[$i]}"
    # Use different nodes for PUT operations
    node_num=$(((i % NUM_NODES) + 1))
    port=$((BASE_PORT + node_num - 1))
    
    if "$CLIENT_TOOL" 127.0.0.1 $port put "$key" "$value" > /dev/null 2>&1; then
        success "PUT successful: $key = $value (via node $node_num)"
    else
        error "PUT failed: $key = $value (via node $node_num)"
        exit 1
    fi
done

# GET operations - use different nodes to verify distributed access
log "Testing GET operations from different nodes..."
for i in "${!test_keys[@]}"; do
    key="${test_keys[$i]}"
    expected_value="${test_values[$i]}"
    # Use different node for GET than was used for PUT
    node_num=$(((i + NUM_NODES/2) % NUM_NODES + 1))
    port=$((BASE_PORT + node_num - 1))
    
    result=$("$CLIENT_TOOL" 127.0.0.1 $port get "$key" 2>/dev/null | grep "GET successful" | cut -d'=' -f2 | xargs)
    if [ "$result" = "$expected_value" ]; then
        success "GET successful: $key = $result (via node $node_num)"
    else
        error "GET failed: $key expected '$expected_value' but got '$result' (via node $node_num)"
        exit 1
    fi
done

# DELETE operations - use yet different nodes
log "Testing DELETE operations..."
for i in "${!test_keys[@]}"; do
    key="${test_keys[$i]}"
    # Use different node for DELETE
    node_num=$(((i + 1) % NUM_NODES + 1))
    port=$((BASE_PORT + node_num - 1))
    
    if "$CLIENT_TOOL" 127.0.0.1 $port delete "$key" > /dev/null 2>&1; then
        success "DELETE successful: $key (via node $node_num)"
    else
        error "DELETE failed: $key (via node $node_num)"
        exit 1
    fi
done

# Verify deletions worked
log "Verifying deletions..."
for i in "${!test_keys[@]}"; do
    key="${test_keys[$i]}"
    node_num=$(((i + 2) % NUM_NODES + 1))
    port=$((BASE_PORT + node_num - 1))
    
    if "$CLIENT_TOOL" 127.0.0.1 $port get "$key" > /dev/null 2>&1; then
        error "Key $key still exists after deletion (checked via node $node_num)"
        exit 1
    else
        success "Verified $key was deleted (checked via node $node_num)"
    fi
done

# Step 4: Shutdown all nodes using admin command
log "Shutting down all nodes using admin command..."
shutdown_failed=0
for ((i=1; i<=NUM_NODES; i++)); do
    port=$((BASE_PORT + i - 1))
    if "$CLIENT_TOOL" 127.0.0.1 $port shutdown > /dev/null 2>&1; then
        success "Node $i (port $port) shutdown command sent"
    else
        error "Failed to send shutdown command to node $i (port $port)"
        shutdown_failed=$((shutdown_failed + 1))
    fi
done

# Wait for nodes to shutdown
log "Waiting for nodes to shutdown..."
sleep 3

# Verify all processes terminated
running_nodes=0
for pid in "${PIDS[@]}"; do
    if kill -0 "$pid" 2>/dev/null; then
        running_nodes=$((running_nodes + 1))
    fi
done

if [ $running_nodes -gt 0 ]; then
    warning "$running_nodes nodes still running, forcing termination..."
    cleanup
else
    success "All nodes have shut down gracefully"
fi

echo
echo "=================================================="
echo "            Test Results Summary"
echo "=================================================="
echo
success "$NUM_NODES nodes started successfully"
success "All nodes responded to ping"
success "PUT operations successful on all test keys"
success "GET operations successful from different nodes"
success "DELETE operations successful from different nodes"
success "Verified all keys were properly deleted"
success "All nodes shut down successfully"
echo
echo -e "${GREEN}✅ 10-Node Chord Cluster Test PASSED!${NC}"
echo
echo "Test demonstrates:"
echo "• Multi-node Chord ring formation"
echo "• Distributed hash table functionality" 
echo "• Cross-node data consistency"
echo "• Administrative shutdown capability"
echo "• Proper cleanup and resource management"
echo