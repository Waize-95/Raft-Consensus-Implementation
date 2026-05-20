13 Deliverables and Submission
13.1 What to Submit
A single zip archive containing:
•The complete source tree of your implementation, including the server (raftkv), the client (raftkv-cli),
and the chaos test driver.
•A Makefile (or CMakeLists.txt) that builds all binaries with a single command on a recent Ubuntu
LTS.
•A README.md with build instructions, cluster startup instructions for each deployment option from
Section 3, the list of supported client commands, and any known limitations.
•A design.pdf document of at most 15 pages describing: the server architecture, the RPC layer,
the persistent state file format, the state machine implementation, the handling of each step of the
two RPCs including which fields are persisted when, the commitment rule and why the current-term
restriction is necessary, the NOOP trick, the catch-up backtracking logic, and a detailed walkthrough
of each of the six chaos test scenarios including the observed behavior and any edge cases discovered
during testing.
•A chaos/ directory with the chaos test driver, the scripts that run it against the five-node cluster,
and the logs from your final successful run.
•A docker-compose.yml or equivalent for whichever deployment option you tested with.
•A tests/ directory with unit tests for: the log entry serializer and deserializer, the RequestVote
receiver logic (feed in hand-crafted RPCs and verify responses), the AppendEntries receiver logic
including the log matching check and the conflict-delete behavior, and the commitment rule.
13.2 Archive Naming
Group[Number]_Project02_Raft.zip, for example Group17_Project02_Raft.zip.
13.3 Demonstration
Each group will present a 30-minute live demonstration during the week following the submission dead-
line. The demonstration consists of three parts. First, a walkthrough of the architecture and the most
subtle parts of the implementation (the commitment rule, the persistence ordering, the backtracking
logic) using the design document. Second, a live startup of the five-node cluster followed by the full
chaos test, with the instructor free to propose additional scenarios on the spot (“kill the leader, wait 200
ms, kill another node, what happens?”). Third, a short oral examination in which each group member
will be asked to explain, without notes, a specific component of the implementation, with particular
focus on the three safety properties (election safety, leader completeness, log matching) and how each
is enforced by specific lines of code. All group members must be present. Missing the demonstration
results in a zero for Phase 3.
14 Bonus Opportunities
Each of the following is independent. Bonus credit is awarded on top of the base grade up to a maximum
of 20 percentage points. To claim bonus credit, your design document must include a dedicated section
explaining which items you attempted and how you verified them.
14.1 Log Compaction by Snapshotting (up to 10%)
Implement the snapshot mechanism described in Section 7 of the Raft paper. When the log grows
beyond a threshold, the state machine is serialized into a snapshot file, the log is truncated to discard
entries covered by the snapshot, and a new lastIncludedIndex / lastIncludedTerm are recorded. The
leader sends snapshots to followers that have fallen so far behind that the leader no longer has their
required entries, via a new InstallSnapshot RPC. Your chaos test must include a scenario where a
follower is killed, the remaining cluster submits enough entries to trigger log truncation, and the killed
follower is restarted; the follower must receive a snapshot and catch up.
14.2 Pre-vote Protocol (up to 5%)
Implement the pre-vote optimization (Section 9.6 of the paper). Before a follower increments its term
and becomes a candidate, it first sends a PreVote RPC to check whether it would win an election.
This prevents a partitioned node from disrupting the cluster by repeatedly incrementing its term during the partition. Demonstrate the improvement with a chaos test scenario that partitions a single node,
waits several seconds, heals the partition, and shows that the isolated node’s term has not advanced to
artificially high values.
14.3 Fast Log Backtracking (up to 5%)
The base project uses the simple one-entry-at-a-time backtracking from Figure 2. Implement the opti-
mized backtracking described at the end of Section 5.3 of the paper, where the follower’s AppendEntriesResponse
includes the term of the conflicting entry and the first index in its log for that term, allowing the leader
to skip back by a whole term at a time. Demonstrate in your benchmark that catch-up of a very-behind
follower is significantly faster with this optimization.
15 Constraints and Prohibitions
The Following Will Result in Loss of Credit
•Use of any existing Raft library or consensus framework. This includes but is not limited to
hashicorp/raft, canonical/raft, willemt/raft, etcd/raft, logcabin, braft, nuraft, and
their language bindings. The point of the project is to implement Raft yourself.
•Use of any existing RPC or serialization framework (gRPC, Thrift, Protobuf, Cap’n Proto, Flat-
Buffers). Raft RPCs must be implemented as direct reads and writes on plain TCP sockets using
the binary formats you define.
•Skipping the persistence of currentTerm, votedFor, or new log entries before responding to an
RPC. Every state change mandated by the algorithm to be “persisted before responding” must
in fact be written to disk and fsync’d. Violating this can cause the cluster to elect two leaders
for the same term after restarts, which is a textbook safety bug.
•Skipping the commitment rule’s current-term restriction (Section 9.3). A leader that commits
entries from previous terms based solely on majority replication is exhibiting the bug described
in Figure 8 of the paper. A chaos test scenario will attempt to trigger this bug.
•Using a fixed, non-randomized election timeout. Liveness is at stake.
•Use of Python at any layer, including the chaos test driver. Tests and scripts must be written in
C, C++, shell, or JavaScript.
•Submitting code whose commit history shows large, infrequent commits. Raft is notoriously
difficult to implement in a single sitting; a git history showing incremental development is the
expected shape of an honest attempt.
16 Required Reading
The first item is required reading before you begin Phase 1 and must be re-read before Phase 3. Every-
thing else is reference material.
1. Ongaro, D. and Ousterhout, J. “In Search of an Understandable Consensus Algorithm.” Proceedings
of USENIX Annual Technical Conference, 2014. The Raft paper. Read Sections 1 through 5 carefully
before Phase 1; Figure 2 is the specification you will implement. Reread Sections 5.4 (the safety
argument) and 5.4.2 (the Figure 8 scenario) before Phase 3. If you read only one thing for this
project, read this paper.
2. Ongaro, D. Consensus: Bridging Theory and Practice, PhD dissertation, Stanford University, 2014.
The dissertation version of the Raft paper, with more detail on issues that the conference paper
glosses over. Chapter 3 covers everything in the paper but at greater length; Chapter 4 covers cluster
membership changes (out of scope here but useful background); Chapter 5 covers log compaction
(relevant to the bonus).
3. “The Secret Lives of Data: Raft,” interactive visualization at http://thesecretlivesofdata.com/
raft/. Walk through it before starting. It takes about 15 minutes and gives an intuition for term
numbers, election behavior, and log replication that the paper alone will not.
4. Howard, H. and Mortier, R. “Paxos vs Raft: Have We Reached Consensus on Distributed Consensus?”
Proceedings of the 7th Workshop on Principles and Practice of Consistency, 2020. Optional. A short
paper arguing that Raft and Paxos are more similar than the Raft paper suggests. Useful perspective
after you have finished the implementation.
5. Kleppmann, M. Designing Data-Intensive Applications, Chapter 9 (“Consistency and Consensus”).
Background reading on the broader context of consensus protocols. Useful as a big-picture refresher
if you get lost in the details.
