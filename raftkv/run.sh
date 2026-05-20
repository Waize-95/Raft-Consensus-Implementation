#!/usr/bin/env bash
# ============================================================================
# RaftKV one-stop runner.
#
#   ./run.sh build           build server, client, and unit tests
#   ./run.sh cluster         start a 5-node cluster (foreground; Ctrl+C stops)
#   ./run.sh client          open the leader-aware CLI against the cluster
#   ./run.sh test            build + run the unit tests
#   ./run.sh chaos [N]       build + run the 6-scenario chaos test (default N=1000)
#   ./run.sh stop            kill any running raftkv nodes
#   ./run.sh clean           stop nodes and remove binaries + data dirs
#
# See HOW_TO_RUN.md for the full walkthrough.
# ============================================================================
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

CLIENT_SPECS="1@127.0.0.1:5001 2@127.0.0.1:5002 3@127.0.0.1:5003 4@127.0.0.1:5004 5@127.0.0.1:5005"

peers_for() {            # id@host:peerport list excluding $1
    local skip="$1" out=""
    for j in 1 2 3 4 5; do
        [ "$j" = "$skip" ] && continue
        out="$out,$j@127.0.0.1:600$j"
    done
    echo "${out:1}"
}

cmd_build() { make; }

cmd_stop() {
    for p in $(ps -eo pid,cmd | grep "[r]aftkv --id" | awk '{print $1}'); do
        kill "$p" 2>/dev/null
    done
    echo "stopped any running nodes"
}

cmd_cluster() {
    cmd_build || exit 1
    cmd_stop; sleep 1
    for i in 1 2 3 4 5; do mkdir -p "d$i"; done
    echo "starting 5-node cluster (client 5001-5005, peer 6001-6005)"
    pids=()
    for i in 1 2 3 4 5; do
        ./raftkv --id "$i" --port "500$i" --peer-port "600$i" \
                 --data "d$i" --peers "$(peers_for "$i")" \
                 > "d$i/node.log" 2>&1 &
        pids+=("$!")
        echo "  node $i -> d$i/node.log"
    done
    echo "cluster up. Logs in d1..d5/node.log. Ctrl+C to stop."
    trap 'kill "${pids[@]}" 2>/dev/null; echo; echo stopped; exit 0' INT TERM
    wait
}

cmd_client() {
    cmd_build >/dev/null 2>&1
    echo "connecting client to cluster (auto-redirects to leader)"
    exec ./raftkv-cli $CLIENT_SPECS
}

cmd_test()  { make test; }
cmd_chaos() { cmd_build || exit 1; ./chaos/chaos_test.sh "${1:-1000}"; }

cmd_clean() { cmd_stop; make clean; echo "cleaned"; }

case "${1:-}" in
    build)   cmd_build ;;
    cluster) cmd_cluster ;;
    client)  cmd_client ;;
    test)    cmd_test ;;
    chaos)   cmd_chaos "${2:-1000}" ;;
    stop)    cmd_stop ;;
    clean)   cmd_clean ;;
    *) echo "usage: $0 {build|cluster|client|test|chaos [N]|stop|clean}"; exit 1 ;;
esac
