Phase 1 — Single Node with Persistent Log and State Machine
The goal of Phase 1 is to build the substrate on which everything else will run. A single-process Raft
node that maintains its persistent state on disk, accepts client commands, applies them to the key-value
state machine, and survives a crash. No elections, no RPCs, no network. This phase is small and its
purpose is to get the persistence, serialization, and state machine out of the way before the protocol
logic lands on top.
Build these components:
• The persistent state file: a binary file containing currentTerm, votedFor, and the log. Define a
format that supports appending to the log without rewriting the entire file (append-only log file,
plus a small metadata file that holds the non-log fields). Use fsync after every update.
• The log entry serializer: convert a log entry struct to and from bytes. Include a CRC32 over each
entry.
• The key-value state machine: a hash map with apply(command) and get(key) methods. The com-
mand serialization format is in Section 6.
• A TCP server that accepts client connections and handles PUT, GET, DELETE, \status, and QUIT.
Since there is only one node and no Raft yet, the server simply appends commands to the log,
applies them immediately to the state machine, and responds.
• Startup recovery: on launch, read the persistent state file, rebuild the log, replay committed entries
into the state machine, and begin accepting clients.
• The client binary raftkv-cli.
At the end of Phase 1, the \status command should print the current term (always 0 in Phase 1), the
log length, and commitIndex. Crashes and restarts should preserve all data