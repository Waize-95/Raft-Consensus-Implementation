# Phase 2 Step 4: Background Threads and Timers

This document explains the threading architecture and the implementation of elections and heartbeats added in Step 4.

## 1. Threading Architecture

Raft requires a node to be constantly doing three things at once:
1. Responding to incoming RPCs (RequestVote, AppendEntries).
2. Ticking the election timer (to detect leader failure).
3. Maintaining active leadership via heartbeats (if elected).

To achieve this without blocking, `server.cpp` was updated to use **multithreading**. When a node starts, its `run()` method launches three background threads:

```cpp
std::thread rpc_thread(&RaftServer::rpcReceiverLoop, this);
std::thread election_thread(&RaftServer::electionTimerLoop, this);
std::thread heartbeat_thread(&RaftServer::heartbeatTimerLoop, this);
```

The main thread then runs the `poll`-based event loop for the **text-based client protocol**. Since multiple threads access and modify the same node state (`metadata`, `role`, `log_entries`), a single `std::mutex raft_mutex` is used. Thread safety is achieved by brief `std::lock_guard<std::mutex> lock(raft_mutex);` acquisitions at the start of any state change.

---

## 2. The RPC Receiver (`rpcReceiverLoop`)

To separate client traffic from server-to-server traffic, a **Two-Port Design** was introduced. Nodes take a `--peer-port` argument for binary RPCs.

The `rpcReceiverLoop` binds to the `peer_port` and waits for peer connections. When a TCP connection is accepted from a peer, it spawns a temporary **detached thread** (`handlePeerConnection`). 
This means an incoming RPC cannot block the `rpcReceiverLoop` from accepting other incoming requests. The detached thread:
1. Reads the 5-byte header to determine the `RpcType`.
2. Reads the expected body length.
3. Deserializes the arguments and grabs the `raft_mutex`.
4. Executes `handleRequestVote` or `handleAppendEntries`.
5. Serializes the reply and sends it back.

---

## 3. The Election Timer (`electionTimerLoop`)

The election timer runs in a continuous loop, waking up every 20ms to check the time against `election_deadline`. 
If `now() >= election_deadline` and the node is not a `LEADER`, the timeout has fired!

**Action on Fire:**
1. Transition to `CANDIDATE`.
2. Increment `currentTerm`.
3. Vote for self (`votedFor = node_id`).
4. Critically: call `updateMetaData` (which uses `fsync`) to durably persist the vote.
5. Launch `sendRequestVoteToAll()`.

**Resetting the Timer:**
The deadline is set to `now() + random(300, 500) ms`. The timer is forcefully reset via `resetElectionTimer()` under two conditions:
- We grant a vote to another candidate (`handleRequestVote`).
- We receive a valid heartbeat from a current leader (`handleAppendEntries`).

---

## 4. The Heartbeat Timer (`heartbeatTimerLoop`)

This thread runs continuously but spends most of its time sleeping for 100ms. If it wakes up and observes that `role == LEADER`, it triggers `sendAppendEntriesToAll()`.

**Action on Fire:**
It prepares an `AppendEntriesArgs` containing the leader's current term, `prevLogIndex`, and `prevLogCommit`. The `entries` array remains completely empty (this defines it as a heartbeat). It sends this to every peer. By sending this every 100ms, it ensures followers' election timers (set to 300-500ms) are continually suppressed.

---

## 5. Non-Blocking RPC Senders

When a network partition happens, or a peer crashes, trying to send a TCP message could block indefinitely or take seconds to time out. If the election or heartbeat thread sent RPCs synchronously, a single dead peer would freeze the entire Raft node.

To solve this, `sendRequestVoteToAll()` and `sendAppendEntriesToAll()` do not send data themselves. Instead, they loop over the `peers` list, and for each peer they launch a **detached thread**.

Example flow for a heartbeat:
1. Leader's heartbeat thread creates an `AppendEntriesArgs`.
2. Leader loops over peers => spawns a thread for Node B, spawns another for Node C.
3. Thread B connects to Node B, sends the binary data, and waits for a reply.
4. If Node B is dead, only Thread B hangs until the 1-second socket timeout clears it. The Leader's main heartbeat loop is already finished and asleep, ready for the next tick.

---

## 6. Verification Status

Phase 2 Step 4 is fully implemented and tested. A 3-node cluster successfully starts, instances time out to become CANDIDATE, exchange `RequestVote` RPCs, and correctly step up to `LEADER` when a majority is reached. All shared state mutations are correctly protected by mutex locks, preventing race conditions between the timer thread and incoming RPC threads.
