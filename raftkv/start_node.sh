#!/bin/bash
# eval_start_node.sh <ID>
# Starts a single raft node using the exact ports and structure shown in the evaluation guide.
# Usage for Terminal 1: ./eval_start_node.sh 1
# run_server.sh improved
if [ -z "$1" ] || [[ ! "$1" =~ ^[1-5]$ ]]; then
    echo "Usage: ./eval_start_node.sh <id>"
    echo "ID must be between 1 and 5"
    exit 1
fi

ID=$1
PORT=$((6000 + ID))
PEER_PORT=$((7000 + ID))
DATA_DIR="./d$ID"

# Create the data directory if it does not exist
mkdir -p "$DATA_DIR"

# Generate the peers string dynamically (excluding the current node itself)
PEERS=""
for i in 1 2 3 4 5; do
  if [ "$i" != "$ID" ]; then
    PEERS="$PEERS$i@127.0.0.1:$((7000 + i)),"
  fi
done
PEERS=${PEERS%,} # Remove trailing comma

echo ""
echo "=== TERMINAL $ID ==="
echo "Executing: ./raftkv --id $ID --port $PORT --peer-port $PEER_PORT --peers \"$PEERS\" --data $DATA_DIR"
echo ""

# Run the server
./raftkv --id $ID --port $PORT --peer-port $PEER_PORT --peers "$PEERS" --data "$DATA_DIR"
