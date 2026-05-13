Phase 2 — Elections and Heartbeats
The goal of Phase 2 is to bring up a five-node cluster that elects a leader and maintains leadership
through heartbeats, but does not yet replicate client commands. A client that connects to a leader can
issue \status and see the leader’s role, but PUTs are rejected with an error saying log replication is not
yet implemented.
Build these components:
• The RPC layer: a TCP connection between every pair of nodes, used for RequestVote and AppendEntries.
Each RPC is a binary message with a header (RPC type, length) and a type-specific body. Use the
message formats in Section 7.
• Background threads: one for each outgoing peer connection (sending RPCs) and one accepting
incoming peer connections (receiving RPCs). Shared state is protected by a single mutex, held only
briefly during each update.
• The election timer: a background timer that fires after a random interval in [300, 500] ms. Reset by
any valid AppendEntries from the current leader or any granted RequestVote.
• RequestVote receiver logic, implemented per Section 7.1. Every transition of currentTerm or
votedFor must be persisted (with fsync) before the reply is sent.
• The election procedure from Section 9.1: transition to candidate, increment term, vote for self, send
RequestVote to peers, count responses.
• The leader’s heartbeat loop: every 100 ms, send an empty AppendEntries to every peer. The
heartbeat AppendEntries carries prevLogIndex and prevLogTerm matching the leader’s current last
log entry, so that followers can still detect divergence.
• AppendEntries receiver logic restricted to the heartbeat case (empty entries). Steps 1, 2, and 3 from
Section 7.2 must be implemented and persisted; steps 4, 5, 6 are trivial when entries is empty.
• \status reporting the node’s current role, term, and (if leader) the list of peers.
At the end of Phase 2, you should be able to start five nodes, watch them elect a leader within a few
hundred milliseconds, kill the leader, watch a new leader be elected within another second or so, and kill
nodes arbitrarily as long as a majority remains — a new leader will always emerge. Client commands
are rejected but the protocol is visibly working.
11.1 Testing Phase 2 Thoroughly
Before moving on to Phase 3, spend real time testing Phase 2 under adversarial conditions. Run the
cluster with 500+ election cycles by repeatedly killing the leader. Run it with artificial network delays
(a few hundred milliseconds of sleep inside the RPC handler) to shake out timing-dependent bugs. Your
election logic will have bugs; find them now, not after you have added log replication on top.