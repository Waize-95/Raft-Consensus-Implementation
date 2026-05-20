#!/bin/bash
# Script to run an individual raft server
# Usage: ./run_server.sh <node_id>

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <node_id>"
    echo "node_id should be between 1 and 5"
    exit 1
fi

ID=$1
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RAFTKV="$SCRIPT_DIR/raftkv"

if [ ! -f "$RAFTKV" ]; then
    echo "Error: raftkv binary not found. Please compile using 'make' first."
    exit 1
fi

if [[ ! "$ID" =~ ^[1-5]$ ]]; then
    echo "Error: node_id must be between 1 and 5."
    exit 1
fi

PORT=$((5000 + ID))
PEER_PORT=$((6000 + ID))
DATA_DIR="$SCRIPT_DIR/d${ID}"

PEERS_ALL=("" "2@127.0.0.1:6002,3@127.0.0.1:6003,4@127.0.0.1:6004,5@127.0.0.1:6005"
           "1@127.0.0.1:6001,3@127.0.0.1:6003,4@127.0.0.1:6004,5@127.0.0.1:6005"
           "1@127.0.0.1:6001,2@127.0.0.1:6002,4@127.0.0.1:6004,5@127.0.0.1:6005"
           "1@127.0.0.1:6001,2@127.0.0.1:6002,3@127.0.0.1:6003,5@127.0.0.1:6005"
           "1@127.0.0.1:6001,2@127.0.0.1:6002,3@127.0.0.1:6003,4@127.0.0.1:6004")

PEERS="${PEERS_ALL[$ID]}"

mkdir -p "$DATA_DIR"

echo "Checking for any rogue instances of Server $ID..."
pkill -f "$RAFTKV.*--id $ID " 2>/dev/null || true
sleep 0.5

echo "Starting Server $ID..."
echo "Command: $RAFTKV --id $ID --port $PORT --peer-port $PEER_PORT --data \"$DATA_DIR\" --peers \"$PEERS\""
$RAFTKV --id $ID --port $PORT --peer-port $PEER_PORT --data "$DATA_DIR" --peers "$PEERS"
