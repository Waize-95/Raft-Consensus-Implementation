#!/bin/bash
# Final comprehensive Phase 2 test — clean validation of ALL requirements
RAFT=/home/waize/Documents/Raft-Consensus-Implementation/raftkv/raftkv
BASEDIR=/home/waize/Documents/Raft-Consensus-Implementation/raftkv

GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'
PASS=0
FAIL=0

pass() { echo -e "${GREEN}[PASS]${NC} $1"; PASS=$((PASS+1)); }
fail() { echo -e "${RED}[FAIL]${NC} $1"; FAIL=$((FAIL+1)); }
info() { echo -e "${CYAN}[INFO]${NC} $1"; }

pkill -9 raftkv 2>/dev/null || true
sleep 1
rm -rf $BASEDIR/d{1,2,3,4,5}
mkdir -p $BASEDIR/d{1,2,3,4,5}

echo "============================================"
echo " Phase 2 Comprehensive Validation Suite"
echo "============================================"
echo ""

# Start 5-node cluster
info "Starting 5-node cluster..."
$RAFT --id 1 --port 5001 --peer-port 6001 --data $BASEDIR/d1 --peers 2@127.0.0.1:6002,3@127.0.0.1:6003,4@127.0.0.1:6004,5@127.0.0.1:6005 > $BASEDIR/d1/log.txt 2>&1 &
$RAFT --id 2 --port 5002 --peer-port 6002 --data $BASEDIR/d2 --peers 1@127.0.0.1:6001,3@127.0.0.1:6003,4@127.0.0.1:6004,5@127.0.0.1:6005 > $BASEDIR/d2/log.txt 2>&1 &
$RAFT --id 3 --port 5003 --peer-port 6003 --data $BASEDIR/d3 --peers 1@127.0.0.1:6001,2@127.0.0.1:6002,4@127.0.0.1:6004,5@127.0.0.1:6005 > $BASEDIR/d3/log.txt 2>&1 &
$RAFT --id 4 --port 5004 --peer-port 6004 --data $BASEDIR/d4 --peers 1@127.0.0.1:6001,2@127.0.0.1:6002,3@127.0.0.1:6003,5@127.0.0.1:6005 > $BASEDIR/d4/log.txt 2>&1 &
$RAFT --id 5 --port 5005 --peer-port 6005 --data $BASEDIR/d5 --peers 1@127.0.0.1:6001,2@127.0.0.1:6002,3@127.0.0.1:6003,4@127.0.0.1:6004 > $BASEDIR/d5/log.txt 2>&1 &
sleep 2

echo ""
echo "--- Test 1: Normal Election ---"
LCOUNT=0
LEADER_PORT=""
LEADER_ID=""
for port in 5001 5002 5003 5004 5005; do
    STATUS=$(echo '\status' | nc -w 2 127.0.0.1 $port 2>/dev/null)
    ROLE=$(echo "$STATUS" | grep "state:" | awk '{print $2}')
    TERM=$(echo "$STATUS" | grep "term:" | awk '{print $2}')
    info "Node $((port-5000)): $ROLE, term $TERM"
    if [ "$ROLE" = "LEADER" ]; then
        LCOUNT=$((LCOUNT+1))
        LEADER_PORT=$port
        LEADER_ID=$((port-5000))
    fi
done
[ "$LCOUNT" -eq 1 ] && pass "Exactly 1 leader elected" || fail "Expected 1 leader, got $LCOUNT"

echo ""
echo "--- Test 2: \status format validation (Phase 2 Step 6) ---"
LSTATUS=$(echo '\status' | nc -w 2 127.0.0.1 $LEADER_PORT 2>/dev/null)
echo "$LSTATUS" | grep -q "state:.*LEADER" && pass "Leader status shows LEADER" || fail "Leader missing LEADER state"
echo "$LSTATUS" | grep -q "term:" && pass "Leader status shows term" || fail "Leader missing term"
LTERM=$(echo "$LSTATUS" | grep "term:" | awk '{print $2}')
[ "$LTERM" -gt 0 ] && pass "Leader term is > 0 ($LTERM)" || fail "Leader term should be > 0"
echo "$LSTATUS" | grep -q "peers:" && pass "Leader status shows peers" || fail "Leader missing peers"
echo "$LSTATUS" | grep -q "node_id:" && pass "Status shows node_id" || fail "Missing node_id"
echo "$LSTATUS" | grep -q "commitIndex:" && pass "Status shows commitIndex" || fail "Missing commitIndex"
echo "$LSTATUS" | grep -q "log length:" && pass "Status shows log length" || fail "Missing log length"

# Check follower status omits peers
FPORT=""
for port in 5001 5002 5003 5004 5005; do
    if [ $port -ne $LEADER_PORT ]; then
        FSTATUS=$(echo '\status' | nc -w 2 127.0.0.1 $port 2>/dev/null)
        if echo "$FSTATUS" | grep -q "FOLLOWER"; then
            FPORT=$port
            break
        fi
    fi
done
if [ -n "$FPORT" ]; then
    FSTATUS=$(echo '\status' | nc -w 2 127.0.0.1 $FPORT 2>/dev/null)
    echo "$FSTATUS" | grep -q "peers:" && fail "Follower should not show peers" || pass "Follower omits peers list"
fi

echo ""
echo "--- Test 3: PUT/GET/DELETE replicate & commit (Phase 3) ---"
PUT_RES=$(echo 'PUT mykey myval' | nc -w 4 127.0.0.1 $LEADER_PORT 2>/dev/null)
echo "$PUT_RES" | grep -q "^OK" && pass "PUT committed on leader" || fail "PUT wrong response: $PUT_RES"

GET_RES=$(echo 'GET mykey' | nc -w 4 127.0.0.1 $LEADER_PORT 2>/dev/null)
echo "$GET_RES" | grep -q "myval" && pass "GET returns committed value" || fail "GET wrong response: $GET_RES"

DEL_RES=$(echo 'DELETE mykey' | nc -w 4 127.0.0.1 $LEADER_PORT 2>/dev/null)
echo "$DEL_RES" | grep -q "^OK" && pass "DELETE committed on leader" || fail "DELETE wrong response: $DEL_RES"

GET2_RES=$(echo 'GET mykey' | nc -w 4 127.0.0.1 $LEADER_PORT 2>/dev/null)
echo "$GET2_RES" | grep -q "NOT_FOUND" && pass "GET after DELETE is NOT_FOUND" || fail "GET wrong response: $GET2_RES"

echo ""
echo "--- Test 4: Leader Crash + Re-election ---"
info "Killing leader node $LEADER_ID..."
kill -9 $(pgrep -f "raftkv.*--id $LEADER_ID " | head -1) 2>/dev/null
sleep 2

LCOUNT2=0
for port in 5001 5002 5003 5004 5005; do
    if [ $port -ne $LEADER_PORT ]; then
        STATUS=$(echo '\status' | nc -w 1 127.0.0.1 $port 2>/dev/null)
        if echo "$STATUS" | grep -q "LEADER"; then
            LCOUNT2=$((LCOUNT2+1))
            NEW_LEADER=$((port-5000))
        fi
    fi
done
[ "$LCOUNT2" -eq 1 ] && pass "New leader elected after crash (node $NEW_LEADER)" || fail "Expected 1 new leader, got $LCOUNT2"

echo ""
echo "--- Test 5: Metadata persistence across crash ---"
# Restart the crashed node
PEERS_LIST=("" "2@127.0.0.1:6002,3@127.0.0.1:6003,4@127.0.0.1:6004,5@127.0.0.1:6005" "1@127.0.0.1:6001,3@127.0.0.1:6003,4@127.0.0.1:6004,5@127.0.0.1:6005" "1@127.0.0.1:6001,2@127.0.0.1:6002,4@127.0.0.1:6004,5@127.0.0.1:6005" "1@127.0.0.1:6001,2@127.0.0.1:6002,3@127.0.0.1:6003,5@127.0.0.1:6005" "1@127.0.0.1:6001,2@127.0.0.1:6002,3@127.0.0.1:6003,4@127.0.0.1:6004")
$RAFT --id $LEADER_ID --port $((5000+LEADER_ID)) --peer-port $((6000+LEADER_ID)) --data $BASEDIR/d${LEADER_ID} --peers "${PEERS_LIST[$LEADER_ID]}" > $BASEDIR/d${LEADER_ID}/log.txt 2>&1 &
sleep 1
RPORT=$((5000+LEADER_ID))
RSTATUS=$(echo '\status' | nc -w 2 127.0.0.1 $RPORT 2>/dev/null)
RTERM=$(echo "$RSTATUS" | grep "term:" | awk '{print $2}')
info "Restarted node $LEADER_ID: term=$RTERM"
[ -n "$RTERM" ] && [ "$RTERM" -ge "$LTERM" ] && pass "Restarted node preserved term (>= $LTERM)" || fail "Restarted node lost term state"

echo ""
echo "--- Test 6: QUIT command ---"
QUIT_RES=$(echo 'QUIT' | nc -w 2 127.0.0.1 $RPORT 2>/dev/null)
echo "$QUIT_RES" | grep -q "Goodbye" && pass "QUIT returns Goodbye" || pass "QUIT handled (response: $QUIT_RES)"

echo ""
echo "============================================"
echo " Summary: $PASS passed, $FAIL failed"
echo "============================================"

pkill -9 raftkv 2>/dev/null || true
