Phase 3 — Log Replication and Consistency
The goal of Phase 3 is full Raft: client commands are replicated via AppendEntries, committed via the
majority rule, and applied to the state machine. Failover preserves all acknowledged commands. At the
end of Phase 3 the full example session in Section 2 should work.
Build these components:
•The full AppendEntries receiver, implementing all six steps from Section 7.2.
•Per-peer nextIndex and matchIndex, initialized when a node becomes leader.
•The leader’s send logic: for each peer, construct an AppendEntries with entries starting from
nextIndex[peer], send it, and handle the response. A success=false response with no higher
term means decrement nextIndex and retry. A higher-term response means step down.
•The commitment rule from Section 9.3: after every matchIndex update, scan for the highest index
replicated on a majority, and if that index’s entry is in the current term, advance commitIndex.
•The apply loop: a background thread (or a synchronous call at the end of every AppendEntries
receive and every commit advancement) that applies entries from lastApplied + 1 to commitIndex
into the state machine.
•The NOOP trick from Section 7.4: immediately on becoming leader, append a NOOP entry to the log.
•The leader-aware client redirect: a follower that receives a client PUT/GET replies with NOT_LEADER
leader=N (or UNKNOWN_LEADER during an election), and the client reconnects.
•Log-based reads: a GET from a client is processed by the leader appending a read entry (or just a
NOOP) to the log, waiting for it to commit, and then returning the value from the state machine at
the time the entry is applied.
12.1 The Chaos Test
Phase 3 is evaluated primarily by a chaos test, which is the single most important deliverable. Write
a test driver that runs the following scenarios in sequence, checking invariants after each step. Every
scenario must pass.
1. Start all five nodes. Wait for a leader. Submit 1000 PUTs through the leader. Verify that all five
nodes have exactly the same state machine content (connect to each and compare using \status or
by reading every key).
2. Kill the leader. Wait for a new leader. Submit 1000 more PUTs. Restart the killed node. Verify that
the restarted node catches up and ends with the same state as the others.
3. Kill two followers (keeping three alive, still a majority). Submit 500 PUTs. Restart the killed followers.
Verify they catch up and all five nodes agree.
4. Kill three nodes at once (leaving only two alive). Attempt 100 PUTs; all should time out or fail,
because no majority is possible. Restart one killed node, making three again; submit 100 PUTs; they
should succeed.
5. Simulate a network partition by making two nodes refuse connections from the other three (and vice
versa). The majority side should continue to accept writes. The minority side should have no leader
and reject writes. Heal the partition: the minority side should re-elect (or re-join whoever is leader
on the majority side) and catch up.
6. Repeat scenario 1 but kill the leader partway through the 1000 PUTs, then verify that the count of
committed entries after recovery is within a small margin of 1000 and that there are no gaps.
The test driver must run as a shell script or separate binary (not Python). Log every action it takes
and every response. Include the logs from your final successful run with your submission.