#!/bin/bash
# Test 4: Minority Partition — start fresh, kill leader + 2 followers
RAFT=/home/waize/Documents/Raft-Consensus-Implementation/raftkv/raftkv
BASEDIR=/home/waize/Documents/Raft-Consensus-Implementation/raftkv

rm -rf $BASEDIR/d{1,2,3,4,5}
mkdir -p $BASEDIR/d{1,2,3,4,5}

echo "Starting fresh 5-node cluster..."
$RAFT --id 1 --port 5001 --peer-port 6001 --data $BASEDIR/d1 --peers 2@127.0.0.1:6002,3@127.0.0.1:6003,4@127.0.0.1:6004,5@127.0.0.1:6005 > $BASEDIR/d1/log.txt 2>&1 &
$RAFT --id 2 --port 5002 --peer-port 6002 --data $BASEDIR/d2 --peers 1@127.0.0.1:6001,3@127.0.0.1:6003,4@127.0.0.1:6004,5@127.0.0.1:6005 > $BASEDIR/d2/log.txt 2>&1 &
$RAFT --id 3 --port 5003 --peer-port 6003 --data $BASEDIR/d3 --peers 1@127.0.0.1:6001,2@127.0.0.1:6002,4@127.0.0.1:6004,5@127.0.0.1:6005 > $BASEDIR/d3/log.txt 2>&1 &
$RAFT --id 4 --port 5004 --peer-port 6004 --data $BASEDIR/d4 --peers 1@127.0.0.1:6001,2@127.0.0.1:6002,3@127.0.0.1:6003,5@127.0.0.1:6005 > $BASEDIR/d4/log.txt 2>&1 &
$RAFT --id 5 --port 5005 --peer-port 6005 --data $BASEDIR/d5 --peers 1@127.0.0.1:6001,2@127.0.0.1:6002,3@127.0.0.1:6003,4@127.0.0.1:6004 > $BASEDIR/d5/log.txt 2>&1 &

sleep 2
echo "Cluster up. Finding leader..."

LEADER_ID=""
for port in 5001 5002 5003 5004 5005; do
    STATUS=$(echo '\status' | nc -w 1 127.0.0.1 $port 2>/dev/null)
    if echo "$STATUS" | grep -q "LEADER"; then
        LEADER_ID=$((port - 5000))
        echo "  Leader: node $LEADER_ID"
        break
    fi
done

if [ -z "$LEADER_ID" ]; then
    echo "FAIL: No leader elected"
    pkill -9 raftkv 2>/dev/null
    exit 1
fi

# Kill the leader + 2 other nodes (= 3 dead)
KILL_LIST=()
KILL_LIST+=($LEADER_ID)
for id in 1 2 3 4 5; do
    if [ $id -ne $LEADER_ID ] && [ ${#KILL_LIST[@]} -lt 3 ]; then
        KILL_LIST+=($id)
    fi
done

echo "Killing nodes: ${KILL_LIST[*]} (leader + 2 followers)"
for id in "${KILL_LIST[@]}"; do
    PID=$(pgrep -f "raftkv.*--id $id " | head -1)
    if [ -n "$PID" ]; then
        kill -9 $PID 2>/dev/null
        wait $PID 2>/dev/null
    fi
done

echo "Surviving nodes:"
ALIVE=()
for id in 1 2 3 4 5; do
    FOUND=0
    for dead in "${KILL_LIST[@]}"; do
        if [ $id -eq $dead ]; then FOUND=1; break; fi
    done
    if [ $FOUND -eq 0 ]; then
        ALIVE+=($id)
    fi
done
echo "  Alive: ${ALIVE[*]}"

echo "Waiting 5 seconds for the surviving nodes to attempt elections..."
sleep 5

echo "Checking status of surviving nodes..."
LCOUNT=0
for id in "${ALIVE[@]}"; do
    port=$((5000+id))
    STATUS=$(echo '\status' | nc -w 1 127.0.0.1 $port 2>/dev/null)
    ROLE=$(echo "$STATUS" | grep "state:" | awk '{print $2}')
    TERM=$(echo "$STATUS" | grep "term:" | awk '{print $2}')
    echo "  Node $id: role=$ROLE, term=$TERM"
    if [ "$ROLE" = "LEADER" ]; then
        LCOUNT=$((LCOUNT + 1))
    fi
done

if [ "$LCOUNT" -eq 0 ]; then
    echo "TEST 4: PASS - No leader with ${#ALIVE[@]}/5 nodes (minority cannot elect)"
else
    echo "TEST 4: FAIL - $LCOUNT leader(s) with only ${#ALIVE[@]} nodes alive"
fi

pkill -9 raftkv 2>/dev/null || true
echo "Done."
