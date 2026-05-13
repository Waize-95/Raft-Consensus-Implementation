# Phase 2 Implementation Plan: Elections and Heartbeats

## 1. Overview and Goal
The goal of Phase 2 is to bring up a five-node cluster that perfectly elects a leader and maintains leadership through heartbeats. It does **not** replicate client commands yet. 
* A client connecting to a leader can issue `\status` and see the leader’s role.
* `PUT` or `DELETE` commands are rejected with an error saying log replication is not yet implemented.

---

## 2. Current State (Carried Over From Phase 1)
You already have a solid foundation built in Phase 1 that satisfies some core Phase 2 dependencies:
* **Storage and Persistence:** `metadata.bin` perfectly tracks and updates `currentTerm` and `votedFor` using `fsync()` before any replies are sent back.
* **Basic Structs:** You have `MetaData` and `LogEntry` structures needed to extract `lastLogTerm` and `lastLogIndex`.
* **TCP Foundations:** `server.cpp` event loop (`poll()`) provides a good basis for network connections.

---

## 3. What Needs to be Implemented (Step-by-Step)

### Step 1: Node Configuration & Peer List
* Modify `main.cpp` and `server.cpp` to accept a list of peer network addresses (e.g., via command-line arguments or a config file) so a node knows the IP/Ports of the other 4 nodes.

### Step 2: The Node State Machine & Concurrency
* **Roles:** Introduce an enumerator for roles: `enum Role { FOLLOWER, CANDIDATE, LEADER };`. Every node starts as a `FOLLOWER`.
* **Mutexes:** Introduce a `std::mutex` inside `RaftServer`. This must protect your internal state (`metadata`, `log_entries`, `role`, timers) from race conditions because you will now have multiple threads accessing them simultaneously (client threads, RPC sender threads, and RPC receiver threads).

### Step 3: The RPC Layer
Define binary message structures (or serialization functions) for server-to-server communication:
* **`RequestVote` Arguments:** `term`, `candidateId`, `lastLogIndex`, `lastLogTerm`.
* **`RequestVote` Results:** `term`, `voteGranted`.
* **`AppendEntries` Arguments:** (Used for heartbeats) `term`, `leaderId`, `prevLogIndex`, `prevLogTerm`, `entries` (empty for now), `leaderCommit`.
* **`AppendEntries` Results:** `term`, `success`.

### Step 4: Background Threads and Timers
* **RPC Receiver Thread:** A thread strictly for accepting incoming peer connections and receiving RPCs.
* **RPC Sender Threads:** Threads specifically for pushing outbound RPCs to avoiding blocking the rest of the application if a peer is slow/dead.
* **The Election Timer (Background Timer):** 
    * A background timer that fires after a random interval in **[300, 500] ms**.
    * **Reset condition:** Must be reset whenever a valid `AppendEntries` is received from a current leader or if this node grants a vote to a candidate.
    * **Action on Fire:** Transition to `CANDIDATE`, increment `currentTerm`, vote for self (`votedFor = node_id`), persist metadata, and broadcast `RequestVote` to all peers.
* **The Heartbeat Timer (Leader only):**
    * Fires every **100 ms**.
    * **Action on Fire:** Broadcasts an empty `AppendEntries` to every peer to suppress their election timers. Includes `prevLogIndex` and `prevLogTerm` matching the leader’s current last log entry.

### Step 5: Receiver Logic & Safety Checks
* **`RequestVote` Receiver:**
    * Reject (return false) if candidate's `term` < current `term`.
    * If `votedFor` is null/0 or matches the candidate ID, **AND** the candidate's log is at least as up-to-date as the receiver's log, grant the vote (`votedFor = candidateId`, return true).
    * **CRITICAL:** Call `updateMetaData()` (with `fsync`) to persist the term and vote before replying.
* **`AppendEntries` Receiver (Heartbeats):**
    * Reject (return false) if leader's `term` < current `term`.
    * If the receiver is a `CANDIDATE`, step down to `FOLLOWER` immediately upon receiving this.
    * Update `currentTerm` if the leader's term is higher.

### Step 6: Update the Client Handlers
* **`PUT` / `DELETE` Commands:** Immediately return `"ERR: log replication is not yet implemented\n"`.
* **`\status` Command:** Dynamically report the current role (`FOLLOWER`, `CANDIDATE`, or `LEADER`), the dynamic `currentTerm`, and if it is the leader, the list of peers.

---

## 4. Testing & Validation Checklist
Before touching Phase 3, you must thoroughly test adversarial conditions:
1. **Normal Election:** Start 5 nodes. Verify they elect exactly one leader within 300-500ms.
2. **Leader Crash:** Kill the leader. Verify a new leader is elected within another second.
3. **Repeated Leader Crashes:** Kill the leader repeatedly for 500+ election cycles. Ensure a single leader always emerges.
4. **Minority Partition:** Kill 3 nodes. The remaining 2 nodes should just spin as Candidates and never successfully elect a leader (since a majority of 3 is required).
5. **Network Delays:** Introduce artificial network delays (e.g., `sleep()` a few hundred milliseconds inside the RPC handler) to shake out concurrency/timing bugs.
