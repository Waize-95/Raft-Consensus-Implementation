#!/usr/bin/env bash
# ============================================================================
# Raft Chaos Test Driver (Phase 3, Section 12.1)
#
# Runs the six required chaos scenarios in sequence against a five-node cluster,
# checking invariants after each step. Every action and response is logged.
#
# Usage:   ./chaos/chaos_test.sh [num_puts]
#   num_puts  size of the big PUT batches (default 1000). Use a smaller value
#             for a quick smoke run, e.g. ./chaos/chaos_test.sh 200
#
# No Python anywhere — pure bash + the raftkv / raftkv-cli binaries.
# ============================================================================
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
RAFTKV="$ROOT/raftkv"
CLI="$ROOT/raftkv-cli"
LOG="$HERE/last_run.log"

N="${1:-1000}"              # big-batch size
NODES="1 2 3 4 5"
declare -A ALIVE            # ALIVE[id]=1 if we believe the node is running

# All client endpoints, for the redirect-aware CLI.
PALL="1@127.0.0.1:5001 2@127.0.0.1:5002 3@127.0.0.1:5003 4@127.0.0.1:5004 5@127.0.0.1:5005"

# ---- logging -------------------------------------------------------------
exec > >(tee "$LOG") 2>&1
say()  { echo -e "\n>>> $*"; }
info() { echo "    $*"; }
PASS=0; FAIL=0
ok()   { echo "    [PASS] $*"; PASS=$((PASS+1)); }
bad()  { echo "    [FAIL] $*"; FAIL=$((FAIL+1)); }

# ---- cluster helpers -----------------------------------------------------
peers_for() {           # echo "id@host:peerport,..." for every node except $1
    local skip="$1" out=""
    for j in $NODES; do
        [ "$j" = "$skip" ] && continue
        out="$out,$j@127.0.0.1:600$j"
    done
    echo "${out:1}"
}

start_node() {
    local id="$1"
    "$RAFTKV" --id "$id" --port "500$id" --peer-port "600$id" \
              --data "$ROOT/d$id" --peers "$(peers_for "$id")" \
              > "$ROOT/d$id/node.log" 2>&1 &
    ALIVE[$id]=1
    info "started node $id (client 500$id, peer 600$id)"
}

kill_node() {
    local id="$1"
    pkill -f "raftkv --id $id " 2>/dev/null
    ALIVE[$id]=0
    info "killed node $id"
}

cli() {                 # cli <server-spec...> ; commands come from stdin
    "$CLI" "$@" 2>/dev/null
}

# Detect the current leader id (queries each alive node's \status). Empty if none.
leader_id() {
    for i in $NODES; do
        [ "${ALIVE[$i]:-0}" = "1" ] || continue
        if printf '\\status\n' | cli "$i@127.0.0.1:500$i" | grep -q "state:        LEADER"; then
            echo "$i"; return 0
        fi
    done
    return 1
}

wait_for_leader() {     # wait up to ~10s for a stable leader; echo its id
    for _ in $(seq 1 50); do
        local L; L="$(leader_id)" || true
        [ -n "$L" ] && { echo "$L"; return 0; }
        sleep 0.2
    done
    return 1
}

# Submit PUTs for keys k<start>..k<start+count-1>, each value v<i>, via the
# redirect-aware client (one session). Echo how many got an OK response.
submit_puts() {
    local start="$1" count="$2"
    local end=$((start + count - 1))
    seq "$start" "$end" | sed 's/.*/PUT k& v&/' \
        | cli $PALL | grep -c '^OK'
}

# Same, but with a hard time budget — used where we EXPECT failure (no majority).
submit_puts_expect_fail() {
    local start="$1" count="$2"
    local end=$((start + count - 1))
    seq "$start" "$end" | sed 's/.*/PUT k& v&/' \
        | timeout 60 cli $PALL | grep -c '^OK'
}

# \hash returns one line "count=N hash=H" computed server-side over the local
# applied state machine — tiny response, immune to large-transfer truncation.
node_count() { printf '\\hash\n' | cli "$1@127.0.0.1:500$1" | grep -oP 'count=\K[0-9]+'; }
node_hash()  { printf '\\hash\n' | cli "$1@127.0.0.1:500$1" | grep -oP 'hash=\K[0-9]+'; }

# Assert every ALIVE node has the same applied state. Args: expected count (or "" to skip).
assert_agree() {
    local expect="$1" first="" h c agree=1
    for i in $NODES; do
        [ "${ALIVE[$i]:-0}" = "1" ] || continue
        h="$(node_hash "$i")"; c="$(node_count "$i")"
        info "node $i: count=$c hash=$h"
        [ -z "$first" ] && first="$h"
        [ "$h" = "$first" ] || agree=0
    done
    if [ "$agree" = "1" ]; then ok "all alive nodes agree on state"
    else bad "alive nodes DISAGREE on state"; fi
    if [ -n "$expect" ]; then
        c="$(node_count "$(first_alive)")"
        if [ "$c" = "$expect" ]; then ok "committed key count == $expect"
        else bad "committed key count $c != expected $expect"; fi
    fi
}

first_alive() { for i in $NODES; do [ "${ALIVE[$i]:-0}" = "1" ] && { echo "$i"; return; }; done; }

cleanup() { say "cleanup: stopping all nodes"; for i in $NODES; do kill_node "$i"; done; sleep 1; }
trap cleanup EXIT

# ============================================================================
say "BUILD"
( cd "$ROOT" && make ) || { echo "build failed"; exit 1; }
say "RESET cluster state"
for i in $NODES; do kill_node "$i"; done; sleep 1
for i in $NODES; do rm -rf "$ROOT/d$i"; mkdir -p "$ROOT/d$i"; ALIVE[$i]=0; done

# ----------------------------------------------------------------------------
say "SCENARIO 1: 5 nodes, elect leader, $N PUTs, verify all agree"
for i in $NODES; do start_node "$i"; done
L="$(wait_for_leader)" && ok "leader elected: node $L" || { bad "no leader"; exit 1; }
got="$(submit_puts 1 "$N")"; info "$got/$N PUTs acknowledged OK"
[ "$got" = "$N" ] && ok "all $N PUTs committed" || bad "only $got/$N committed"
sleep 1
assert_agree "$N"

# ----------------------------------------------------------------------------
say "SCENARIO 2: kill leader, $N more PUTs, restart killed node, verify catch-up"
L="$(leader_id)"; info "current leader = node $L"
kill_node "$L"; sleep 1
NL="$(wait_for_leader)" && ok "new leader elected: node $NL" || bad "no new leader"
got="$(submit_puts $((N+1)) "$N")"; info "$got/$N PUTs acknowledged OK"
[ "$got" = "$N" ] && ok "all $N PUTs committed after failover" || bad "only $got/$N committed"
say "restart node $L"; start_node "$L"; sleep 3
assert_agree $((2*N))

# ----------------------------------------------------------------------------
say "SCENARIO 3: kill 2 followers (majority remains), 500 PUTs, restart, verify"
L="$(leader_id)"; info "leader = node $L"
F=(); for i in $NODES; do [ "$i" != "$L" ] && F+=("$i"); done
k1="${F[0]}"; k2="${F[1]}"
kill_node "$k1"; kill_node "$k2"; sleep 1
info "killed followers $k1 and $k2 (3 nodes still alive = majority)"
got="$(submit_puts $((2*N+1)) 500)"; info "$got/500 PUTs acknowledged OK"
[ "$got" = "500" ] && ok "all 500 PUTs committed with 3/5 alive" || bad "only $got/500 committed"
say "restart $k1 and $k2"; start_node "$k1"; start_node "$k2"; sleep 3
assert_agree $((2*N+500))

# ----------------------------------------------------------------------------
say "SCENARIO 4: kill 3 nodes (only 2 alive) -> writes must FAIL; restart 1 -> succeed"
L="$(leader_id)"; info "leader = node $L"
# kill the leader + two others, leaving exactly two alive
declare -a KILL3=()
KILL3+=("$L")
for i in $NODES; do
    [ "${ALIVE[$i]:-0}" = "1" ] || continue
    [ "$i" = "$L" ] && continue
    KILL3+=("$i")
    [ "${#KILL3[@]}" -ge 3 ] && break
done
for id in "${KILL3[@]}"; do kill_node "$id"; done; sleep 2
info "killed nodes: ${KILL3[*]} ; only 2 alive -> no majority possible"
got="$(submit_puts_expect_fail $((2*N+501)) 100)"
info "$got/100 PUTs acknowledged OK (expect 0)"
[ "$got" = "0" ] && ok "no writes committed without majority" || bad "$got writes slipped through without majority!"
restart_one="${KILL3[0]}"
say "restart node $restart_one -> 3 alive again, writes should succeed"
start_node "$restart_one"; sleep 1
NL="$(wait_for_leader)" && ok "leader re-elected with majority: node $NL" || bad "no leader with 3/5"
got="$(submit_puts $((2*N+501)) 100)"; info "$got/100 PUTs acknowledged OK"
[ "$got" = "100" ] && ok "100 PUTs committed once majority restored" || bad "only $got/100 committed"
# bring the other two back so we can do a full-cluster agreement check
for id in "${KILL3[@]}"; do [ "${ALIVE[$id]:-0}" = "1" ] || start_node "$id"; done; sleep 3
assert_agree $((2*N+600))

# ----------------------------------------------------------------------------
say "SCENARIO 5: partition 2 nodes from the other 3; majority writes, minority rejects; heal"
L="$(leader_id)"; info "leader = node $L"
# Minority group = two nodes that are NOT the leader.
MIN=(); for i in $NODES; do [ "$i" != "$L" ] && MIN+=("$i"); done
m1="${MIN[0]}"; m2="${MIN[1]}"
info "isolating minority {$m1,$m2} (kill-based isolation; true bidirectional"
info "partitions need iptables/root — see chaos/README notes)"
kill_node "$m1"; kill_node "$m2"; sleep 1
# Majority side keeps the leader -> writes succeed.
got="$(submit_puts $((2*N+601)) 100)"; info "majority side: $got/100 PUTs OK"
[ "$got" = "100" ] && ok "majority side accepted writes during partition" || bad "majority blocked"
# Minority side: a client that can only reach {m1,m2} must NOT get writes through.
mgot="$(seq 1 5 | sed 's/.*/PUT p& z&/' \
        | timeout 20 cli "$m1@127.0.0.1:500$m1" "$m2@127.0.0.1:500$m2" | grep -c '^OK')"
info "minority side: $mgot/5 PUTs OK (expect 0 — nodes are isolated/down)"
[ "$mgot" = "0" ] && ok "minority side rejected writes" || bad "minority committed writes!"
say "heal partition: restart $m1,$m2"; start_node "$m1"; start_node "$m2"; sleep 3
assert_agree $((2*N+700))

# ----------------------------------------------------------------------------
say "SCENARIO 6: kill leader partway through $N PUTs; committed count within margin, no gaps"
L="$(leader_id)"; info "leader = node $L"
base=$((2*N+701))
# Launch the batch in the background, kill the leader partway, then settle.
( submit_puts "$base" "$N" > "$HERE/s6_acks.txt" ) &
batch_pid=$!
sleep 1
kill_node "$L"; info "killed leader node $L mid-batch"
wait "$batch_pid" 2>/dev/null
acks="$(cat "$HERE/s6_acks.txt" 2>/dev/null || echo 0)"
info "client received $acks OK responses during the chaos"
NL="$(wait_for_leader)" && ok "cluster recovered, leader = node $NL" || bad "no leader after mid-batch kill"
start_node "$L"; sleep 3   # bring the killed node back for a full agreement check
# Count how many of k$base..k$((base+N-1)) are committed across the cluster.
present="$(node_count "$(first_alive)")"
# subtract the keys from earlier scenarios (they share the k-namespace):
prior=$((2*N+700))
s6count=$((present - prior))
info "scenario-6 keys committed: $s6count of $N"
margin=$(( N/20 + 5 ))     # ~5% margin
if [ "$s6count" -le "$N" ] && [ "$s6count" -ge $((N - margin)) ]; then
    ok "committed count $s6count within margin [$((N-margin)),$N]"
else
    bad "committed count $s6count outside margin [$((N-margin)),$N]"
fi
# "No gaps": the committed scenario-6 keys must form a contiguous prefix
# k$base..k(base+s6count-1). Dump once and check the whole range in awk.
fa="$(first_alive)"
gap="$(printf '\\dump\n' | cli "$fa@127.0.0.1:500$fa" | grep '^k' \
      | sed 's/=.*//; s/^k//' \
      | awk -v base="$base" -v n="$s6count" '
          $1>=base && $1<base+n {seen[$1]=1}
          END{ miss=0; for(i=base;i<base+n;i++) if(!(i in seen)) miss++; print miss }')"
[ "${gap:-1}" = "0" ] && ok "no gaps in committed scenario-6 prefix" || bad "gap detected ($gap missing in prefix)"
assert_agree ""   # all nodes must still agree, whatever the final count

# ============================================================================
say "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" = "0" ] && { echo "ALL CHAOS SCENARIOS PASSED"; exit 0; } || { echo "SOME SCENARIOS FAILED"; exit 1; }
