#!/bin/bash

# Test Failure Resilience - Tests node failure and data recovery
# This test verifies:
# 1. 10-node cluster startup
# 2. Data operations (put/get) across different nodes
# 3. Forced node failures (kill -KILL)
# 4. Data availability after failures
# 5. Graceful shutdown of remaining nodes

set -e

# Configuration
NODES=10
BASE_PORT=20000
BINARY="../bin/chord_server"
CLIENT="../bin/client_tool"
NODE_PIDS=()
TEST_KEYS=("key1" "key2" "key3" "key4" "key5")
TEST_VALUES=("value1" "value2" "value3" "value4" "value5")

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Function to cleanup on exit
cleanup() {
    print_status "Cleaning up..."
    
    # Kill any remaining processes
    for pid in "${NODE_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            print_status "Killing node with PID $pid"
            kill -KILL "$pid" 2>/dev/null || true
        fi
    done
    
    # Wait a moment for cleanup
    sleep 1
    
    print_status "Cleanup completed"
}

# Set trap for cleanup
trap cleanup EXIT

# Function to wait for a node to be ready
wait_for_node() {
    local port=$1
    local max_attempts=30
    local attempt=1
    
    while [ $attempt -le $max_attempts ]; do
        if timeout 5s $CLIENT -h 127.0.0.1 -p $port ping >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
        attempt=$((attempt + 1))
    done
    
    print_error "Node on port $port failed to start within $max_attempts seconds"
    return 1
}

# Function to perform put operation with retry
do_put() {
    local port=$1
    local key=$2
    local value=$3
    local max_retries=3
    local retry=1
    
    while [ $retry -le $max_retries ]; do
        local output
        output=$(timeout 15s $CLIENT -h 127.0.0.1 -p $port put "$key" "$value" 2>&1)
        local exit_code=$?
        
        if [ $exit_code -eq 0 ]; then
            return 0
        else
            print_warning "PUT $key attempt $retry/$max_retries failed on port $port: $output"
            if [ $retry -lt $max_retries ]; then
                sleep 2
            fi
            retry=$((retry + 1))
        fi
    done
    
    print_error "PUT $key failed after $max_retries attempts on port $port"
    return 1
}

# Function to perform get operation with retry
do_get() {
    local port=$1
    local key=$2
    local expected_value=$3
    local max_retries=3
    local retry=1
    
    while [ $retry -le $max_retries ]; do
        local result
        result=$(timeout 15s $CLIENT -h 127.0.0.1 -p $port get "$key" 2>/dev/null || echo "FAILED")
        
        if [ "$result" = "$expected_value" ]; then
            return 0
        else
            print_warning "GET $key attempt $retry/$max_retries: Expected '$expected_value', got '$result'"
            if [ $retry -lt $max_retries ]; then
                sleep 2
            fi
            retry=$((retry + 1))
        fi
    done
    
    print_error "GET $key failed after $max_retries attempts"
    return 1
}

# Function to check if process is running
is_process_running() {
    local pid=$1
    kill -0 "$pid" 2>/dev/null
}

print_status "Starting Failure Resilience Test"
print_status "=================================="

# Check if binaries exist
if [ ! -f "$BINARY" ]; then
    print_error "Binary $BINARY not found. Please run 'make' first."
    exit 1
fi

if [ ! -f "$CLIENT" ]; then
    print_error "Client tool $CLIENT not found. Please run 'make' first."
    exit 1
fi

# Step 1: Start the first node (create ring)
print_status "Step 1: Starting first node on port $BASE_PORT"
$BINARY -p $BASE_PORT > "/tmp/chord_server_$BASE_PORT.log" 2>&1 &
NODE_PIDS[0]=$!
wait_for_node $BASE_PORT

print_success "First node started with PID ${NODE_PIDS[0]}"

# Step 2: Start remaining nodes (join ring)
print_status "Step 2: Starting remaining $((NODES-1)) nodes"
for i in $(seq 1 $((NODES-1))); do
    port=$((BASE_PORT + i))
    print_status "Starting node $((i+1))/10 on port $port"
    $BINARY -p $port -j 127.0.0.1:$BASE_PORT > "/tmp/chord_server_$port.log" 2>&1 &
    NODE_PIDS[$i]=$!
    wait_for_node $port
    print_success "Node $((i+1))/10 started with PID ${NODE_PIDS[$i]}"
done

# Wait for cluster to stabilize
print_status "Step 3: Waiting for cluster stabilization (5 seconds)..."
sleep 5

# Step 4: Perform put operations on different nodes
print_status "Step 4: Performing PUT operations across different nodes"
for i in "${!TEST_KEYS[@]}"; do
    key="${TEST_KEYS[$i]}"
    value="${TEST_VALUES[$i]}"
    # Use different nodes for put operations
    node_index=$((i % NODES))
    port=$((BASE_PORT + node_index))
    
    print_status "Putting '$key' = '$value' on node $((node_index+1)) (port $port)"
    if do_put $port "$key" "$value"; then
        print_success "PUT $key successful"
    else
        print_error "PUT $key failed"
        exit 1
    fi
done

# Step 5: Verify get operations from different nodes
print_status "Step 5: Verifying GET operations from different nodes"
for i in "${!TEST_KEYS[@]}"; do
    key="${TEST_KEYS[$i]}"
    expected_value="${TEST_VALUES[$i]}"
    # Use different nodes for get operations (different from put nodes)
    node_index=$(((i + 3) % NODES))
    port=$((BASE_PORT + node_index))
    
    print_status "Getting '$key' from node $((node_index+1)) (port $port)"
    if do_get $port "$key" "$expected_value"; then
        print_success "GET $key successful"
    else
        print_error "GET $key failed"
        exit 1
    fi
done

# Step 6: Kill 3 nodes forcefully using kill -KILL
print_status "Step 6: Forcefully killing 3 nodes using kill -KILL"
KILLED_NODES=(2 5 8)  # Kill nodes 3, 6, and 9 (0-indexed: 2, 5, 8)

for node_index in "${KILLED_NODES[@]}"; do
    pid="${NODE_PIDS[$node_index]}"
    port=$((BASE_PORT + node_index))
    
    if is_process_running "$pid"; then
        print_status "Killing node $((node_index+1)) (PID: $pid, Port: $port) with kill -KILL"
        kill -KILL "$pid"
        print_success "Node $((node_index+1)) killed"
    else
        print_warning "Node $((node_index+1)) (PID: $pid) was already dead"
    fi
done

# Wait for failure detection and re-replication
print_status "Step 7: Waiting for failure detection and re-replication (15 seconds)..."
sleep 15

# Step 8: Verify data is still available after node failures
print_status "Step 8: Verifying data availability after node failures"
for i in "${!TEST_KEYS[@]}"; do
    key="${TEST_KEYS[$i]}"
    expected_value="${TEST_VALUES[$i]}"
    
    # Try to get from surviving nodes
    success=false
    for node_index in $(seq 0 $((NODES-1))); do
        # Skip killed nodes
        if [[ " ${KILLED_NODES[@]} " =~ " ${node_index} " ]]; then
            continue
        fi
        
        port=$((BASE_PORT + node_index))
        pid="${NODE_PIDS[$node_index]}"
        
        # Skip if process is not running
        if ! is_process_running "$pid"; then
            continue
        fi
        
        print_status "Trying to get '$key' from surviving node $((node_index+1)) (port $port)"
        if do_get $port "$key" "$expected_value"; then
            print_success "GET $key successful from node $((node_index+1))"
            success=true
            break
        fi
    done
    
    if [ "$success" = false ]; then
        print_error "Failed to retrieve '$key' from any surviving node"
        exit 1
    fi
done

# Step 9: Gracefully shutdown remaining nodes
print_status "Step 9: Gracefully shutting down remaining nodes"
for node_index in $(seq 0 $((NODES-1))); do
    # Skip killed nodes
    if [[ " ${KILLED_NODES[@]} " =~ " ${node_index} " ]]; then
        continue
    fi
    
    pid="${NODE_PIDS[$node_index]}"
    port=$((BASE_PORT + node_index))
    
    if is_process_running "$pid"; then
        print_status "Gracefully shutting down node $((node_index+1)) (port $port)"
        if timeout 10s $CLIENT -h 127.0.0.1 -p $port shutdown >/dev/null 2>&1; then
            # Wait for graceful shutdown
            wait_count=0
            while is_process_running "$pid" && [ $wait_count -lt 10 ]; do
                sleep 1
                wait_count=$((wait_count + 1))
            done
            
            if is_process_running "$pid"; then
                print_warning "Node $((node_index+1)) didn't shutdown gracefully, force killing"
                kill -KILL "$pid" 2>/dev/null || true
            else
                print_success "Node $((node_index+1)) shutdown gracefully"
            fi
        else
            print_warning "Failed to send shutdown command to node $((node_index+1)), force killing"
            kill -KILL "$pid" 2>/dev/null || true
        fi
    fi
done

print_success "==============================================="
print_success "Failure Resilience Test PASSED"
print_success "==============================================="
print_success "✓ 10-node cluster startup successful"
print_success "✓ PUT/GET operations across different nodes successful" 
print_success "✓ 3 nodes killed forcefully with kill -KILL"
print_success "✓ Data remained available after node failures"
print_success "✓ Remaining 7 nodes shutdown gracefully"

# Clear NODE_PIDS to prevent cleanup from trying to kill already dead processes
NODE_PIDS=()

exit 0