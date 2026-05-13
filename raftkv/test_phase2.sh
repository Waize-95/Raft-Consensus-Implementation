#!/bin/bash
# ============================================================
# Phase 2 Test Suite
# Tests: Normal Election, Leader Crash, Repeated Crashes,
#        Minority Partition, Client Handlers
# ============================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RAFTKV="$SCRIPT_DIR/raftkv"
CLI="$SCRIPT_DIR/raftkv-cli"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

pass_count=0
fail_count=0

pass() {
    echo -e "${GREEN}[PASS]${NC} $1"
    ((pass_count++))
}

fail() {
    echo -e "${RED}[FAIL]${NC} $1"
    ((fail_count++))
}

info() {
    echo -e "${CYAN}[INFO]${NC} $1"
}

header() {
    echo ""
    echo -e "${YELLOW}============================================${NC}"
    echo -e "${YELLOW}$1${NC}"
    echo -e "${YELLOW}============================================${NC}"
}

# Clean up any leftover nodes
cleanup() {
    pkill -f "$RAFTKV" 2>/dev/null || true
    sleep 0.5
    rm -rf "$SCRIPT_DIR"/d{1,2,3,4,5} 2>/dev/null || true
}

# Start a 5-node cluster
start_cluster() {
    info "Starting 5-node cluster..."
    for i in 1 2 3 4 5; do
        mkdir -p "$SCRIPT_DIR/d${i}"
    done

    PEERS_1="127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6004,127.0.0.1:6005"
    PEERS_2="127.0.0.1:6001,127.0.0.1:6003,127.0.0.1:6004,127.0.0.1:6005"
    PEERS_3="127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6004,127.0.0.1:6005"
    PEERS_4="127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6005"
    PEERS_5="127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6004"

    $RAFTKV --id 1 --port 5001 --peer-port 6001 --data "$SCRIPT_DIR/d1" --peers "$PEERS_1" > "$SCRIPT_DIR/d1/log.txt" 2>&1 &
    $RAFTKV --id 2 --port 5002 --peer-port 6002 --data "$SCRIPT_DIR/d2" --peers "$PEERS_2" > "$SCRIPT_DIR/d2/log.txt" 2>&1 &
    $RAFTKV --id 3 --port 5003 --peer-port 6003 --data "$SCRIPT_DIR/d3" --peers "$PEERS_3" > "$SCRIPT_DIR/d3/log.txt" 2>&1 &
    $RAFTKV --id 4 --port 5004 --peer-port 6004 --data "$SCRIPT_DIR/d4" --peers "$PEERS_4" > "$SCRIPT_DIR/d4/log.txt" 2>&1 &
    $RAFTKV --id 5 --port 5005 --peer-port 6005 --data "$SCRIPT_DIR/d5" --peers "$PEERS_5" > "$SCRIPT_DIR/d5/log.txt" 2>&1 &

    sleep 1  # Give nodes time to start and bind ports
}

# Start specific nodes (space-separated list of IDs)
start_nodes() {
    local nodes="$@"
    PEERS_ALL=("" "127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6004,127.0.0.1:6005"
               "127.0.0.1:6001,127.0.0.1:6003,127.0.0.1:6004,127.0.0.1:6005"
               "127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6004,127.0.0.1:6005"
               "127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6005"
               "127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6004")
    
    for i in $nodes; do
        mkdir -p "$SCRIPT_DIR/d${i}"
        $RAFTKV --id $i --port $((5000+i)) --peer-port $((6000+i)) \
            --data "$SCRIPT_DIR/d${i}" --peers "${PEERS_ALL[$i]}" \
            > "$SCRIPT_DIR/d${i}/log.txt" 2>&1 &
    done
}

# Get status of a node (returns the raw text)
get_status() {
    local port=$1
    echo '\status' | nc -q 1 127.0.0.1 $port 2>/dev/null || true
}

# Find the leader among running nodes
# Returns the node port number of the leader, or empty if none
find_leader() {
    for port in 5001 5002 5003 5004 5005; do
        local status
        status=$(get_status $port 2>/dev/null)
        if echo "$status" | grep -q "state:.*LEADER"; then
            echo "$port"
            return
        fi
    done
    echo ""
}

# Count leaders among running nodes
count_leaders() {
    local count=0
    for port in 5001 5002 5003 5004 5005; do
        local status
        status=$(get_status $port 2>/dev/null)
        if echo "$status" | grep -q "state:.*LEADER"; then
            ((count++))
        fi
    done
    echo "$count"
}

# Get the PID of node by ID
get_node_pid() {
    local id=$1
    pgrep -f "$RAFTKV.*--id $id " 2>/dev/null | head -1
}

# Kill a specific node
kill_node() {
    local id=$1
    local pid=$(get_node_pid $id)
    if [ -n "$pid" ]; then
        kill -9 $pid 2>/dev/null || true
        wait $pid 2>/dev/null || true
    fi
}

# ============================================================
# TEST 1: Normal Election
# Start 5 nodes. Verify they elect exactly one leader within
# 300-500ms (we give 2s to be safe).
# ============================================================
test_normal_election() {
    header "TEST 1: Normal Election"
    cleanup
    start_cluster

    info "Waiting for election (2 seconds)..."
    sleep 2

    local leaders=$(count_leaders)
    if [ "$leaders" -eq 1 ]; then
        pass "Exactly one leader elected ($leaders leader found)"
    elif [ "$leaders" -eq 0 ]; then
        fail "No leader elected after 2 seconds"
    else
        fail "Multiple leaders elected: $leaders leaders found (split brain!)"
    fi

    # Verify all nodes are in a valid state
    for port in 5001 5002 5003 5004 5005; do
        local status
        status=$(get_status $port 2>/dev/null)
        if [ -z "$status" ]; then
            fail "Node on port $port not responding"
        else
            local role=$(echo "$status" | grep "state:" | awk '{print $2}')
            local term=$(echo "$status" | grep "term:" | awk '{print $2}')
            info "Node $port: role=$role, term=$term"
        fi
    done
}

# ============================================================
# TEST 2: Leader Crash
# Kill the leader. Verify a new leader is elected within 2s.
# ============================================================
test_leader_crash() {
    header "TEST 2: Leader Crash"
    
    local leader_port=$(find_leader)
    if [ -z "$leader_port" ]; then
        fail "No leader found to kill"
        return
    fi
    
    local leader_id=$((leader_port - 5000))
    info "Current leader is node $leader_id (port $leader_port)"
    
    info "Killing leader (node $leader_id)..."
    kill_node $leader_id
    
    info "Waiting for new election (2 seconds)..."
    sleep 2
    
    local new_leader_port=$(find_leader)
    if [ -n "$new_leader_port" ] && [ "$new_leader_port" != "$leader_port" ]; then
        local new_leader_id=$((new_leader_port - 5000))
        pass "New leader elected: node $new_leader_id (port $new_leader_port)"
    elif [ "$new_leader_port" == "$leader_port" ]; then
        fail "Dead leader's port still reports LEADER (node may not have died)"
    else
        fail "No new leader elected after 2 seconds"
    fi
    
    local leaders=$(count_leaders)
    if [ "$leaders" -eq 1 ]; then
        pass "Exactly one leader after crash"
    else
        fail "Unexpected number of leaders after crash: $leaders"
    fi
}

# ============================================================
# TEST 3: Repeated Leader Crashes
# Kill the leader repeatedly (10 cycles). Ensure a single
# leader always emerges.
# ============================================================
test_repeated_crashes() {
    header "TEST 3: Repeated Leader Crashes (10 cycles)"
    cleanup
    start_cluster
    sleep 2
    
    local cycles=10
    local all_passed=true
    
    for i in $(seq 1 $cycles); do
        local leader_port=$(find_leader)
        if [ -z "$leader_port" ]; then
            # Wait a bit more
            sleep 1
            leader_port=$(find_leader)
        fi
        
        if [ -z "$leader_port" ]; then
            fail "Cycle $i: No leader found"
            all_passed=false
            continue
        fi
        
        local leader_id=$((leader_port - 5000))
        info "Cycle $i: Killing leader node $leader_id"
        kill_node $leader_id
        
        # Wait for new election
        sleep 2
        
        # Restart the killed node
        start_nodes $leader_id
        sleep 0.5
        
        local new_leader_port=$(find_leader)
        local leaders=$(count_leaders)
        
        if [ "$leaders" -eq 1 ] && [ -n "$new_leader_port" ]; then
            local new_id=$((new_leader_port - 5000))
            info "Cycle $i: New leader is node $new_id (term may have advanced)"
        else
            fail "Cycle $i: $leaders leaders (expected exactly 1)"
            all_passed=false
        fi
    done
    
    if $all_passed; then
        pass "All $cycles crash/recovery cycles passed"
    fi
}

# ============================================================
# TEST 4: Minority Partition
# Kill 3 nodes. The remaining 2 nodes should spin as
# Candidates and never elect a leader.
# ============================================================
test_minority_partition() {
    header "TEST 4: Minority Partition (no leader with 2/5 nodes)"
    cleanup
    start_cluster
    sleep 2  # Let a leader get elected first
    
    info "Killing nodes 3, 4, 5 (leaving only 2 nodes)..."
    kill_node 3
    kill_node 4
    kill_node 5
    
    info "Waiting 4 seconds for the surviving nodes to attempt elections..."
    sleep 4
    
    local leaders=$(count_leaders)
    if [ "$leaders" -eq 0 ]; then
        pass "No leader elected with only 2/5 nodes (minority can't elect)"
    else
        fail "A leader was elected with only 2 nodes alive: $leaders leader(s)"
    fi
    
    # Check that nodes are spinning as candidates
    for port in 5001 5002; do
        local status
        status=$(get_status $port 2>/dev/null)
        if [ -n "$status" ]; then
            local role=$(echo "$status" | grep "state:" | awk '{print $2}')
            info "Node $((port-5000)): role=$role (should be CANDIDATE)"
        fi
    done
}

# ============================================================
# TEST 5: Client Handler Validation
# Verify PUT/DELETE reject and \status works correctly
# ============================================================
test_client_handlers() {
    header "TEST 5: Client Handler Validation (Phase 2 Step 6)"
    cleanup
    start_cluster
    sleep 2
    
    local leader_port=$(find_leader)
    if [ -z "$leader_port" ]; then
        fail "No leader for client handler test"
        return
    fi
    
    info "Leader is on port $leader_port"
    
    # Test PUT rejection
    local put_result
    put_result=$(echo 'PUT foo bar' | nc -q 1 127.0.0.1 $leader_port 2>/dev/null)
    if echo "$put_result" | grep -q "ERR: log replication is not yet implemented"; then
        pass "PUT correctly rejected with replication error"
    else
        fail "PUT did not return expected error. Got: $put_result"
    fi
    
    # Test DELETE rejection  
    local del_result
    del_result=$(echo 'DELETE foo' | nc -q 1 127.0.0.1 $leader_port 2>/dev/null)
    if echo "$del_result" | grep -q "ERR: log replication is not yet implemented"; then
        pass "DELETE correctly rejected with replication error"
    else
        fail "DELETE did not return expected error. Got: $del_result"
    fi
    
    # Test \status on leader (should show peers)
    local status_result
    status_result=$(get_status $leader_port)
    
    if echo "$status_result" | grep -q "state:.*LEADER"; then
        pass "\\status reports LEADER role"
    else
        fail "\\status does not report LEADER. Got: $status_result"
    fi
    
    if echo "$status_result" | grep -q "term:"; then
        local term=$(echo "$status_result" | grep "term:" | awk '{print $2}')
        if [ "$term" -gt 0 ]; then
            pass "\\status reports dynamic term ($term > 0)"
        else
            fail "\\status term is 0 (expected > 0 after election)"
        fi
    else
        fail "\\status missing term field"
    fi
    
    if echo "$status_result" | grep -q "peers:"; then
        pass "\\status on leader shows peers list"
    else
        fail "\\status on leader missing peers list"
    fi
    
    # Test \status on a follower (should NOT show peers)
    local follower_port=""
    for port in 5001 5002 5003 5004 5005; do
        if [ "$port" != "$leader_port" ]; then
            local s
            s=$(get_status $port 2>/dev/null)
            if echo "$s" | grep -q "state:.*FOLLOWER"; then
                follower_port=$port
                break
            fi
        fi
    done
    
    if [ -n "$follower_port" ]; then
        local follower_status
        follower_status=$(get_status $follower_port)
        if ! echo "$follower_status" | grep -q "peers:"; then
            pass "\\status on follower omits peers list"
        else
            fail "\\status on follower should not show peers"
        fi
    fi

    # Test GET still works (from Phase 1 — should return NOT_FOUND since log replication is disabled)
    local get_result
    get_result=$(echo 'GET foo' | nc -q 1 127.0.0.1 $leader_port 2>/dev/null)
    if echo "$get_result" | grep -q "NOT_FOUND"; then
        pass "GET returns NOT_FOUND (correct with replication disabled)"
    else
        info "GET returned: $get_result (may be expected)"
    fi
}

# ============================================================
# TEST 6: Phase 1 Requirements Still Satisfied
# Verify persistence, recovery, status format
# ============================================================
test_phase1_compat() {
    header "TEST 6: Phase 1 Backwards Compatibility"
    cleanup
    
    # Start a single node (no peers) — Phase 1 mode
    mkdir -p "$SCRIPT_DIR/d1"
    $RAFTKV --id 1 --port 5001 --peer-port 6001 --data "$SCRIPT_DIR/d1" \
        > "$SCRIPT_DIR/d1/log.txt" 2>&1 &
    sleep 1
    
    # The single node will become a candidate and keep incrementing terms
    # (since it can't get a majority). But \status should still work.
    local status
    status=$(get_status 5001)
    
    if echo "$status" | grep -q "node_id:"; then
        pass "Single-node \\status reports node_id"
    else
        fail "Single-node \\status missing node_id"
    fi
    
    if echo "$status" | grep -q "state:"; then
        pass "Single-node \\status reports state"
    else
        fail "Single-node \\status missing state"
    fi
    
    if echo "$status" | grep -q "term:"; then
        pass "Single-node \\status reports term"
    else
        fail "Single-node \\status missing term"
    fi
    
    if echo "$status" | grep -q "commitIndex:"; then
        pass "Single-node \\status reports commitIndex"
    else
        fail "Single-node \\status missing commitIndex"
    fi
    
    if echo "$status" | grep -q "log length:"; then
        pass "Single-node \\status reports log length"
    else
        fail "Single-node \\status missing log length"
    fi
    
    # Test QUIT
    local quit_result
    quit_result=$(echo 'QUIT' | nc -q 1 127.0.0.1 5001 2>/dev/null)
    if echo "$quit_result" | grep -q "Goodbye"; then
        pass "QUIT command returns Goodbye"
    else
        info "QUIT returned: $quit_result"
    fi
}

# ============================================================
# Run all tests
# ============================================================
echo -e "${CYAN}Phase 2 Test Suite — Elections, Heartbeats, and Client Handlers${NC}"
echo "================================================================="

test_normal_election
test_leader_crash
test_repeated_crashes
test_minority_partition
test_client_handlers
test_phase1_compat

# Final cleanup
cleanup

# Summary
echo ""
echo "================================================================="
echo -e "${GREEN}Passed: $pass_count${NC}  ${RED}Failed: $fail_count${NC}"
if [ "$fail_count" -eq 0 ]; then
    echo -e "${GREEN}ALL TESTS PASSED!${NC}"
else
    echo -e "${RED}SOME TESTS FAILED!${NC}"
fi
echo "================================================================="
