#!/bin/bash
# Repeated leader crash test
RAFT=/home/waize/Documents/Raft-Consensus-Implementation/raftkv/raftkv
BASEDIR=/home/waize/Documents/Raft-Consensus-Implementation/raftkv

PEERS_1="127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6004,127.0.0.1:6005"
PEERS_2="127.0.0.1:6001,127.0.0.1:6003,127.0.0.1:6004,127.0.0.1:6005"
PEERS_3="127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6004,127.0.0.1:6005"
PEERS_4="127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6005"
PEERS_5="127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6004"

get_peers() {
    case $1 in
        1) echo "$PEERS_1" ;;
        2) echo "$PEERS_2" ;;
        3) echo "$PEERS_3" ;;
        4) echo "$PEERS_4" ;;
        5) echo "$PEERS_5" ;;
    esac
}

echo "=== TEST 3: Repeated Leader Crashes (10 cycles) ==="
FAILURES=0

for cycle in $(seq 1 10); do
    echo "--- Cycle $cycle ---"
    
    # Find current leader
    LEADER_ID=""
    for port in 5001 5002 5003 5004 5005; do
        STATUS=$(echo '\status' | nc -w 1 127.0.0.1 $port 2>/dev/null)
        if echo "$STATUS" | grep -q "LEADER"; then
            LEADER_ID=$((port - 5000))
            TERM=$(echo "$STATUS" | grep "term:" | awk '{print $2}')
            echo "  Leader: node $LEADER_ID, term $TERM"
            break
        fi
    done
    
    if [ -z "$LEADER_ID" ]; then
        echo "  WARNING: No leader found, waiting 2s..."
        sleep 2
        for port in 5001 5002 5003 5004 5005; do
            STATUS=$(echo '\status' | nc -w 1 127.0.0.1 $port 2>/dev/null)
            if echo "$STATUS" | grep -q "LEADER"; then
                LEADER_ID=$((port - 5000))
                echo "  Found leader: node $LEADER_ID"
                break
            fi
        done
        if [ -z "$LEADER_ID" ]; then
            echo "  FAIL: No leader found after extra wait"
            FAILURES=$((FAILURES + 1))
            continue
        fi
    fi
    
    # Kill the leader
    PID=$(pgrep -f "raftkv.*--id $LEADER_ID " | head -1)
    if [ -n "$PID" ]; then
        kill -9 $PID 2>/dev/null
        wait $PID 2>/dev/null
    fi
    echo "  Killed node $LEADER_ID"
    
    # Wait for new election
    sleep 2
    
    # Restart the killed node
    PEERS=$(get_peers $LEADER_ID)
    $RAFT --id $LEADER_ID --port $((5000+LEADER_ID)) --peer-port $((6000+LEADER_ID)) \
        --data $BASEDIR/d${LEADER_ID} --peers "$PEERS" > $BASEDIR/d${LEADER_ID}/log.txt 2>&1 &
    sleep 1
    
    # Count leaders
    LCOUNT=0
    for port in 5001 5002 5003 5004 5005; do
        STATUS=$(echo '\status' | nc -w 1 127.0.0.1 $port 2>/dev/null)
        if echo "$STATUS" | grep -q "LEADER"; then
            LCOUNT=$((LCOUNT + 1))
        fi
    done
    
    if [ "$LCOUNT" -eq 1 ]; then
        echo "  OK: Exactly 1 leader after cycle $cycle"
    else
        echo "  FAIL: $LCOUNT leaders after cycle $cycle"
        FAILURES=$((FAILURES + 1))
    fi
done

echo ""
if [ "$FAILURES" -eq 0 ]; then
    echo "TEST 3: PASS - All 10 cycles had exactly 1 leader"
else
    echo "TEST 3: $FAILURES failures out of 10 cycles"
fi

echo ""
echo "=== TEST 4: Minority Partition ==="
echo "Killing nodes 3, 4, 5..."
for id in 3 4 5; do
    PID=$(pgrep -f "raftkv.*--id $id " | head -1)
    if [ -n "$PID" ]; then
        kill -9 $PID 2>/dev/null
        wait $PID 2>/dev/null
    fi
done

echo "Waiting 4s for surviving nodes to attempt elections..."
sleep 4

LCOUNT=0
for port in 5001 5002; do
    STATUS=$(echo '\status' | nc -w 1 127.0.0.1 $port 2>/dev/null)
    if echo "$STATUS" | grep -q "LEADER"; then
        LCOUNT=$((LCOUNT + 1))
    fi
    ROLE=$(echo "$STATUS" | grep "state:" | awk '{print $2}')
    TERM=$(echo "$STATUS" | grep "term:" | awk '{print $2}')
    echo "  Node $((port-5000)): role=$ROLE, term=$TERM"
done

if [ "$LCOUNT" -eq 0 ]; then
    echo "TEST 4: PASS - No leader with 2/5 nodes (minority cannot elect)"
else
    echo "TEST 4: FAIL - $LCOUNT leader(s) with only 2 nodes alive"
fi

echo ""
echo "=== Cleanup ==="
pkill -9 -f "raftkv" 2>/dev/null || true
echo "All nodes killed. Tests complete."
