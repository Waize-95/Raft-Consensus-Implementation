# RaftKV — Running Guide

A step-by-step guide to building, running, and testing the Raft-based Key-Value store.

---

## Prerequisites

- **OS:** Linux (tested on Ubuntu)
- **Compiler:** `g++` with C++17 support
- **Tools:** `make`, `nc` (netcat — for quick status checks), `bash`

---

## 1. Building

```bash
cd raftkv
make clean   # remove old binaries and data
make         # builds both raftkv (server) and raftkv-cli (client)
```

This produces two binaries:

| Binary | Description |
|--------|-------------|
| `raftkv` | The Raft server node |
| `raftkv-cli` | Interactive TCP client for issuing commands |

---

## 2. Server Command-Line Options

```
./raftkv --id <N> --port <PORT> --peer-port <PORT> --data <DIR> [--peers <host:port,...>]
```

| Flag | Required | Description |
|------|----------|-------------|
| `--id` | ✅ | Unique numeric node ID (1, 2, 3, …) |
| `--port` | ✅ | Client-facing TCP port (text protocol) |
| `--peer-port` | ✅ | Peer-facing TCP port (binary RPC protocol) |
| `--data` | ❌ | Directory for persistent state files (default: `./data`) |
| `--peers` | ❌ | Comma-separated list of **peer RPC** addresses (`host:port`) |

> **Important:** The `--peers` list should contain the **peer-port** of every *other* node, **not** the client port.

---

## 3. Running a 5-Node Cluster

### 3.1 Create data directories

```bash
mkdir -p d1 d2 d3 d4 d5
```

### 3.2 Start all 5 nodes

Open **5 separate terminals** (or use `&` to background them) and run:

```bash
# Terminal 1 — Node 1
./raftkv --id 1 --port 5001 --peer-port 6001 --data ./d1 \
  --peers 127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6004,127.0.0.1:6005

# Terminal 2 — Node 2
./raftkv --id 2 --port 5002 --peer-port 6002 --data ./d2 \
  --peers 127.0.0.1:6001,127.0.0.1:6003,127.0.0.1:6004,127.0.0.1:6005

# Terminal 3 — Node 3
./raftkv --id 3 --port 5003 --peer-port 6003 --data ./d3 \
  --peers 127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6004,127.0.0.1:6005

# Terminal 4 — Node 4
./raftkv --id 4 --port 5004 --peer-port 6004 --data ./d4 \
  --peers 127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6005

# Terminal 5 — Node 5
./raftkv --id 5 --port 5005 --peer-port 6005 --data ./d5 \
  --peers 127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6004
```

### 3.3 One-liner (background all nodes)

```bash
mkdir -p d{1,2,3,4,5}

./raftkv --id 1 --port 5001 --peer-port 6001 --data ./d1 \
  --peers 127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6004,127.0.0.1:6005 > d1/log.txt 2>&1 &

./raftkv --id 2 --port 5002 --peer-port 6002 --data ./d2 \
  --peers 127.0.0.1:6001,127.0.0.1:6003,127.0.0.1:6004,127.0.0.1:6005 > d2/log.txt 2>&1 &

./raftkv --id 3 --port 5003 --peer-port 6003 --data ./d3 \
  --peers 127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6004,127.0.0.1:6005 > d3/log.txt 2>&1 &

./raftkv --id 4 --port 5004 --peer-port 6004 --data ./d4 \
  --peers 127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6005 > d4/log.txt 2>&1 &

./raftkv --id 5 --port 5005 --peer-port 6005 --data ./d5 \
  --peers 127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6003,127.0.0.1:6004 > d5/log.txt 2>&1 &
```

Within ~500ms a leader will be elected. Check logs with `tail -f d*/log.txt`.

---

## 4. Port Layout Reference

| Node | Client Port | Peer Port | Data Dir |
|------|-------------|-----------|----------|
| 1 | 5001 | 6001 | `./d1` |
| 2 | 5002 | 6002 | `./d2` |
| 3 | 5003 | 6003 | `./d3` |
| 4 | 5004 | 6004 | `./d4` |
| 5 | 5005 | 6005 | `./d5` |

---

## 5. Using the Client

### 5.1 Interactive client (`raftkv-cli`)

```bash
./raftkv-cli 127.0.0.1 5001
```

You'll see a `>` prompt. Available commands:

| Command | Description | Phase 2 Behavior |
|---------|-------------|------------------|
| `\status` | Show node status (role, term, peers) | ✅ Works |
| `GET <key>` | Retrieve a value | ✅ Works (returns `NOT_FOUND` if empty) |
| `PUT <key> <value>` | Store a key-value pair | ❌ Returns `ERR: log replication is not yet implemented` |
| `DELETE <key>` | Remove a key | ❌ Returns `ERR: log replication is not yet implemented` |
| `QUIT` | Disconnect | ✅ Works |

### 5.2 Quick status check with `nc`

```bash
# Check status of node on port 5001
echo '\status' | nc -w 2 127.0.0.1 5001
```

### 5.3 Check all nodes at once

```bash
for port in 5001 5002 5003 5004 5005; do
  echo "=== Port $port ==="
  echo '\status' | nc -w 2 127.0.0.1 $port
  echo ""
done
```

### 5.4 Example `\status` output

**On a LEADER:**
```
node_id:      3
state:        LEADER
term:         1
commitIndex:  0
lastApplied:  0
log length:   0
peers:        127.0.0.1:6001, 127.0.0.1:6002, 127.0.0.1:6004, 127.0.0.1:6005
```

**On a FOLLOWER:**
```
node_id:      1
state:        FOLLOWER
term:         1
commitIndex:  0
lastApplied:  0
log length:   0
```

> Note: Followers do not show the peers list.

---

## 6. Testing Election Behavior

### 6.1 Kill the leader

Find which node is the leader (via `\status`), then kill it:

```bash
# If node 3 is the leader:
kill -9 $(pgrep -f "raftkv.*--id 3")
```

Within ~1 second, a new leader will be elected. Verify:

```bash
for port in 5001 5002 5004 5005; do
  echo "=== Port $port ==="
  echo '\status' | nc -w 1 127.0.0.1 $port
done
```

### 6.2 Restart a killed node

```bash
./raftkv --id 3 --port 5003 --peer-port 6003 --data ./d3 \
  --peers 127.0.0.1:6001,127.0.0.1:6002,127.0.0.1:6004,127.0.0.1:6005 > d3/log.txt 2>&1 &
```

The node will recover its persisted `currentTerm` and `votedFor`, rejoin as a follower, and start receiving heartbeats from the current leader.

### 6.3 Minority partition test

Kill 3 out of 5 nodes. The remaining 2 will continuously attempt elections but **never elect a leader** (a majority of 3 is required).

```bash
kill -9 $(pgrep -f "raftkv.*--id 3")
kill -9 $(pgrep -f "raftkv.*--id 4")
kill -9 $(pgrep -f "raftkv.*--id 5")

# Wait and check — no leader should exist
sleep 3
echo '\status' | nc -w 1 127.0.0.1 5001
echo '\status' | nc -w 1 127.0.0.1 5002
```

---

## 7. Automated Test Scripts

Three test scripts are provided in the `raftkv/` directory:

| Script | What it tests |
|--------|--------------|
| `test_final.sh` | Comprehensive 15-point validation (elections, status format, PUT/DELETE rejection, crash recovery, persistence, QUIT) |
| `test_crash.sh` | 10 repeated leader kill/restart cycles + minority partition |
| `test_minority.sh` | Fresh-start minority partition (3/5 killed, no leader) |

Run any of them:

```bash
bash test_final.sh
```

---

## 8. Viewing Logs

Each node writes to its data directory's `log.txt` when backgrounded:

```bash
# Live-follow all node logs
tail -f d*/log.txt

# Check a specific node's election history
cat d3/log.txt
```

Example log output:
```
[node 3] starting (client port=5003, peer port=6003)
[node 3] persistent state loaded: currentTerm=0 votedFor=none log=[0 entries]
[node 3] entering FOLLOWER state, term 0
[node 3] listening for peer RPCs on port 6003
[node 3] ready for client commands on port 5003
[node 3] election timeout -> CANDIDATE term 1
[node 3] became LEADER for term 1 with 3 votes
```

---

## 9. Stopping Everything

```bash
pkill -9 raftkv
```

To also clean persistent data:

```bash
rm -rf d1 d2 d3 d4 d5
# or
make clean   # also removes binaries
```

---

## 10. Persistent Data Files

Each node's data directory contains:

| File | Contents |
|------|----------|
| `metadata.bin` | `currentTerm`, `votedFor`, `commitIndex`, `lastApplied` (32 bytes, binary) |
| `logs.bin` | Append-only log entries (binary, CRC32-protected) |

These files survive crashes. On restart, the node reads them to restore its state and replays committed log entries into the KV state machine.
