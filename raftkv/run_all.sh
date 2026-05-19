#!/bin/bash
# Script to run 5 raft servers simultaneously

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RAFTKV="$SCRIPT_DIR/raftkv"

# Ensure binary exists
if [ ! -f "$RAFTKV" ]; then
    echo "Error: raftkv binary not found. Please compile using 'make' first."
    exit 1
fi

echo "Cleaning up old data directories and processes..."
pkill -f "$RAFTKV" 2>/dev/null || true
sleep 1
rm -rf "$SCRIPT_DIR"/d{1,2,3,4,5} 2>/dev/null || true

echo "Starting 5-node raft cluster..."
for i in 1 2 3 4 5; do
    mkdir -p "$SCRIPT_DIR/d${i}"
done

PEERS_1="127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6004,127.0.0.1:6005"
PEERS_2="127.0.0.1:6001,127.0.0.1:6003,127.0.0.1:6004,127.0.0.1:6005"
PEERS_3="127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6004,127.0.0.1:6005"
PEERS_4="127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6005"
PEERS_5="127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6004"

$RAFTKV --id 1 --port 5001 --peer-port 6001 --data "$SCRIPT_DIR/d1" --peers "$PEERS_1" &
echo "Server 1 started on port 5001 (peer port 6001)"
$RAFTKV --id 2 --port 5002 --peer-port 6002 --data "$SCRIPT_DIR/d2" --peers "$PEERS_2" &
echo "Server 2 started on port 5002 (peer port 6002)"
$RAFTKV --id 3 --port 5003 --peer-port 6003 --data "$SCRIPT_DIR/d3" --peers "$PEERS_3" &
echo "Server 3 started on port 5003 (peer port 6003)"
$RAFTKV --id 4 --port 5004 --peer-port 6004 --data "$SCRIPT_DIR/d4" --peers "$PEERS_4" &
echo "Server 4 started on port 5004 (peer port 6004)"
$RAFTKV --id 5 --port 5005 --peer-port 6005 --data "$SCRIPT_DIR/d5" --peers "$PEERS_5" &
echo "Server 5 started on port 5005 (peer port 6005)"

echo "All 5 servers are running in the background. Press Ctrl+C to stop them."
trap "kill 0" SIGINT SIGTERM
wait
