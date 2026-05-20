# How to run RaftKV

Everything below assumes you are in the `raftkv/` directory on a recent Ubuntu
with `g++` (C++17) and `make`. The single entry point is `./run.sh`.

```
./run.sh build           build server, client, and unit tests
./run.sh cluster         start a 5-node cluster (foreground; Ctrl+C stops)
./run.sh client          open the leader-aware CLI against the cluster
./run.sh test            build + run the unit tests
./run.sh chaos [N]       build + run the 6-scenario chaos test (default N=1000)
./run.sh stop            kill any running raftkv nodes
./run.sh clean           stop nodes and remove binaries + data dirs
```

First time: `chmod +x run.sh chaos/chaos_test.sh`.

## 1. Build

```bash
./run.sh build
```

Produces three binaries: `raftkv` (server), `raftkv-cli` (client),
`unit_tests`.

## 2. Start the cluster

```bash
./run.sh cluster
```

Starts five nodes on this machine — client ports `5001-5005`, peer/Raft ports
`6001-6005`, data dirs `d1`..`d5`. Each node's output goes to `dN/node.log`. The
command stays in the foreground; press `Ctrl+C` to stop all five. Within a few
hundred milliseconds one node logs `became LEADER`.

(Equivalent lower-level scripts still exist: `./run_all.sh` for all five,
`./run_server.sh <id>` for one node in its own terminal.)

## 3. Talk to the cluster

In another terminal:

```bash
./run.sh client
```

This opens the leader-aware CLI pointed at all five nodes; it follows
`NOT_LEADER` redirects automatically. Try:

```
> PUT x 1
OK (committed at index 2 in term 1)
> GET x
1
> DELETE x
OK (committed at index 3 in term 1)
> \status        # this node's role, term, commitIndex, per-peer match/next
> \dump          # this node's local applied key=value set
> \hash          # one-line digest (count + CRC) of local state — used by the chaos test
> QUIT
```

Commands: `PUT k v`, `GET k`, `DELETE k`, `\status`, `\dump`, `\hash`, `QUIT`.
`PUT/GET/DELETE` are routed to the leader; `\status/\dump/\hash` report the node
you are connected to.

You can also pipe commands non-interactively:

```bash
printf 'PUT a 1\nGET a\n' | ./raftkv-cli 1@127.0.0.1:5001 2@127.0.0.1:5002 \
                                         3@127.0.0.1:5003 4@127.0.0.1:5004 5@127.0.0.1:5005
```

## 4. Unit tests

```bash
./run.sh test
```

Checks the log serializer/CRC, the RequestVote election restriction, the
AppendEntries log-matching + conflict-delete logic, and the commitment rule's
current-term restriction (the Figure 8 case).

## 5. Chaos test (the main Phase 3 deliverable)

```bash
./run.sh chaos          # full run, 1000 PUTs per big batch
./run.sh chaos 200      # quicker smoke run
```

Runs the six required scenarios in sequence — steady-state replication, leader
failover + catch-up, follower loss with majority, loss of majority (writes must
fail), partition + heal, and a mid-batch leader crash — asserting after each step
that every alive node holds identical applied state. Every action and response is
logged to `chaos/last_run.log`; it prints `ALL CHAOS SCENARIOS PASSED` on success.

## 6. Docker (optional, real network isolation)

```bash
docker compose up --build       # five containers raft1..raft5
docker compose stop raft1       # kill a node
docker compose start raft1      # bring it back; it catches up
# host CLI still works: ./raftkv-cli 1@127.0.0.1:5001 ... 5@127.0.0.1:5005
```

## Cleanup

```bash
./run.sh stop      # just stop the nodes
./run.sh clean     # stop + remove binaries and d1..d5 data dirs
```
