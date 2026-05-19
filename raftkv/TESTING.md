# Raft Consensus Testing Guide

This guide describes how to run and test the Raft Consensus implementation, both manually and using the automated test scripts.

## Building the Project

Before running any tests or servers, make sure you compile the source code:
```bash
make clean
make
```
This will compile the `raftkv` server binary and the `raftkv-cli` client binary.

## Running the Servers

We have provided convenient scripts to run the servers. The typical cluster size is 5 nodes, with IDs `1` to `5`.

### 1. Run All 5 Servers Simultaneously

To spin up an entire 5-node cluster at once, simply run:
```bash
./run_all.sh
```
- This script cleans up any old state directories (`d1` to `d5`).
- It starts 5 server instances in the background.
- They will automatically discover each other using static peer configurations and form a Raft cluster, eventually electing a leader.
- The output will be written to `d1/log.txt`, `d2/log.txt`, and so on.
- Press `Ctrl+C` to stop all instances.

### 2. Run an Individual Server

You can start a specific node by its ID (1 to 5) using the single-server script:
```bash
./run_server.sh <node_id>
```
Example:
```bash
./run_server.sh 1
```
- This allows you to simulate crashes by terminating a specific node and running it again to see how the cluster recovers.

---

## Testing Methods

### Method 1: Automated Test Suite

A comprehensive automated shell script is available that tests various Raft edge cases, including normal elections, leader crashes, minority partitions, and client handlers validation.

Run the Phase 2 test suite:
```bash
./test_phase2.sh
```

What the test script covers:
- **Test 1:** Verify the cluster reliably elects exactly one leader.
- **Test 2:** Crash the leader and check if a new leader is elected.
- **Test 3:** Repeatedly crash leaders and check for seamless recovery.
- **Test 4:** Create a minority partition (kill 3 nodes) to ensure the 2 surviving nodes do not incorrectly elect a leader.
- **Test 5:** Validate client rejections (PUT/DELETE currently rejected until log replication is implemented).

### Method 2: Manual Testing

You can use `netcat` (`nc`) to manually send commands to the servers and monitor their states.

First, start all 5 servers:
```bash
./run_all.sh
```

In a separate terminal, test the endpoints:

**1. Querying Node Status**

Raft servers support a special `\status` command. You can send this command to any node's port (5001-5005) to check its state.

```bash
echo '\status' | nc 127.0.0.1 5001
```
Expected output:
```text
node_id: 1
state: LEADER (or FOLLOWER / CANDIDATE)
term: 1
commitIndex: 0
log length: 0
peers: 127.0.0.1:6002, 127.0.0.1:6003, 127.0.0.1:6004, 127.0.0.1:6005
```
*Note: Only the leader outputs the list of peers.*

**2. Identifying the Leader**

Send `\status` to different ports (5001 to 5005) until you find the node whose state says `LEADER`.
```bash
for port in {5001..5005}; do echo '\status' | nc 127.0.0.1 $port; done
```

**3. Simulating Crashes & Elections**

To test leader elections manually:
1. Identify the current leader (e.g., node 4 on port 5004).
2. Kill the leader process:
   ```bash
   pkill -f "raftkv --id 4"
   ```
3. Wait about 1-2 seconds.
4. Check the status of the remaining nodes (ports 5001, 5002, 5003, 5005). You should observe that a new leader was elected.
5. Bring the node back up:
   ```bash
   ./run_server.sh 4
   ```
6. The node will rejoin as a `FOLLOWER`.

**4. Rejecting Put/Delete (Based on Phase 2)**
For Phase 2, log replication is disabled, so PUT and DELETE should be explicitly rejected.

```bash
echo "PUT mykey myvalue" | nc 127.0.0.1 <leader_port>
```
Expected Response: `ERR: log replication is not yet implemented`

```bash
echo "GET mykey" | nc 127.0.0.1 <leader_port>
```
Expected Response: `NOT_FOUND`
