# Raft-Consensus-Implementation

A replicated, fault-tolerant key-value store built on the **Raft consensus
algorithm**, implemented from scratch in C++17 over plain TCP sockets (no Raft
library, no RPC/serialization framework). A five-node cluster agrees on a single
totally-ordered log of commands and tolerates the failure of any minority (up to
two of five nodes) without losing an acknowledged write.

Implements leader election, log replication with the commitment rule, follower
catch-up via backtracking, durable persistent state, and a key-value state
machine on top — i.e. the full base project (Phases 1–3).

## Build

Requires a recent g++ (C++17) on Linux.

```bash
cd raftkv
make            # builds raftkv (server), raftkv-cli (client), unit_tests
```

## Run a cluster

Each node needs a client port (text protocol), a peer/raft port (binary RPC),
a data directory, and the list of peers as `id@host:peerport`.

### Option A — five processes on one machine (recommended / grader default)

```bash
cd raftkv
./run_all.sh          # starts nodes 1..5 (client 5001-5005, peer 6001-6005)
```

…or start one node at a time in separate terminals:

```bash
./run_server.sh 1
./run_server.sh 2     # etc.
```

Manual invocation of a single node:

```bash
./raftkv --id 1 --port 5001 --peer-port 6001 --data ./d1 \
         --peers 2@127.0.0.1:6002,3@127.0.0.1:6003,4@127.0.0.1:6004,5@127.0.0.1:6005
```

### Option B — Docker Compose (network-isolated containers)

```bash
cd raftkv
docker compose up --build       # five services raft1..raft5
docker compose stop raft1       # clean kill for chaos testing
docker compose start raft1      # restart
```

### Option C — five EC2 instances

Run one node per instance; put each instance's **private IP** in the others'
`--peers` list and open inbound TCP on both the client and peer ports within the
security group. Scale the election/heartbeat timing up if cross-region.

> The cluster size is fixed at five. There is no dynamic reconfiguration.

## Client

The client is leader-aware: give it every node's client endpoint and it follows
`NOT_LEADER` redirects automatically, retrying until it reaches the leader.

```bash
./raftkv-cli 1@127.0.0.1:5001 2@127.0.0.1:5002 3@127.0.0.1:5003 \
             4@127.0.0.1:5004 5@127.0.0.1:5005
```

Legacy single-node form (`./raftkv-cli <host> <port>`) is also accepted.

Commands are read one per line (interactive prompt or piped from a script).

| Command          | Meaning                                                            |
|------------------|-------------------------------------------------------------------|
| `PUT k v`        | Replicate & commit a write, then `OK (committed at index I in term T)` |
| `GET k`          | Linearizable read (routed through the log); returns value or `NOT_FOUND` |
| `DELETE k`       | Replicate & commit a delete                                       |
| `\status`        | This node's role, term, leader, commitIndex, log length, per-peer match/next |
| `\dump`          | This node's **local** applied key=value set (testing aid; never redirected) |
| `QUIT`           | Disconnect                                                        |

Non-leaders answer `PUT/GET/DELETE` with `NOT_LEADER leader=N` (or
`UNKNOWN_LEADER` during an election); the client reconnects accordingly.

## Tests

```bash
cd raftkv
make test                 # unit tests: serializer, RequestVote, AppendEntries, commit rule
./chaos/chaos_test.sh     # full six-scenario chaos test (default 1000 PUTs/batch)
./chaos/chaos_test.sh 200 # quicker run with smaller batches
make chaos N=200          # same via make
```

The chaos driver (pure bash, no Python) runs the six required scenarios in
sequence, logs every action and response to `chaos/last_run.log`, and checks
that every alive node has identical applied state after each step.

## Layout

```
raftkv/
  src/        server: command, log, storage, statemachine, rcp (RPC), server, main
  cli/        leader-aware client
  tests/      unit_tests.cpp
  chaos/      chaos_test.sh + logs of runs
  run_all.sh, run_server.sh, Makefile, docker-compose.yml, Dockerfile
Phase_Requirements/   the assignment specs and the Raft manual
design.md             architecture / RPC / persistence / safety write-up
```

## Known limitations

- Follower catch-up uses the simple one-entry-at-a-time backtracking from
  Figure 2 (the fast term-skipping optimization is a bonus and not implemented).
- No log compaction / snapshotting; the log grows unbounded (bonus, not done).
- No pre-vote protocol (bonus, not done).
- Linearizable reads always go through the log (the no-lease fast-path read
  optimization is intentionally not used, per the spec).
- The chaos test's network-partition scenario isolates the minority by stopping
  those nodes (kill-based isolation) rather than with `iptables`; this still
  exercises the invariants (majority makes progress, minority makes none, the
  minority rejoins and catches up on heal). True bidirectional partitions need
  `iptables`/root.
