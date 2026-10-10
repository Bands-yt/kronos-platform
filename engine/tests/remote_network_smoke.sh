#!/bin/bash
# Runs a windowless server and client on the Remote Check test place and checks
# that RemoteEvents, RemoteFunctions, ModuleScripts, replication and DataStores
# work over a real connection.
# Usage: engine/tests/remote_network_smoke.sh [path/to/engine_runtime] [port]
set -u
here="$(cd "$(dirname "$0")" && pwd)"
runtime="$(realpath "${1:-$here/../build/src/engine_runtime}")"
port="${2:-17895}"
work="$(mktemp -d)"
trap 'kill $server 2>/dev/null; wait $server 2>/dev/null; rm -rf "$work"' EXIT
mkdir -p "$work/server/datastores" "$work/client"
# A save from an "earlier run": the server must load 41 and save 42.
echo '{"Visits":{"global":{"count":41}}}' > "$work/server/datastores/remote-check.json"
# A dead API address keeps the check offline (the backend is asked first).
export KRONOS_SILENT_AUDIO=1 KRONOS_GAMES_DIR="$here/fixtures/games" KRONOS_API_URL=http://127.0.0.1:9

(cd "$work/server" && exec timeout 30 stdbuf -oL "$runtime" --server "$port" --game remote-check --headless) \
    > "$work/server.log" 2>&1 &
server=$!
for _ in $(seq 50); do
    grep -q "headless mode started" "$work/server.log" && break
    sleep 0.2
done
(cd "$work/client" && timeout 8 stdbuf -oL "$runtime" --client 127.0.0.1 "$port" --game remote-check --headless) \
    > "$work/client.log" 2>&1

failed=0
expect() {
    if grep -qF "$2" "$work/$1.log"; then
        echo "ok   $1: $2"
    else
        echo "FAIL $1: $2"
        failed=1
    fi
}
expect server 'dedicated server now hosting "Remote Check"'
expect client 'CLIENT asked, server said Teal, module says Teal, hidden=0'
expect server 'SERVER got hello from Player, IsServer=true'
expect client 'CLIENT heard painted'
expect client 'CLIENT sees PaintedByPlayer green=170'
expect server 'SERVER visits 41 -> 42'
if grep -q '"count": *42' "$work/server/datastores/remote-check.json"; then
    echo "ok   server: DataStore file holds 42"
else
    echo "FAIL server: DataStore file holds 42"
    failed=1
fi
if grep -h "Replication" "$work/server.log" "$work/client.log"; then
    echo "FAIL replication warnings (above)"
    failed=1
fi
if [ $failed -ne 0 ]; then
    echo "--- server log"; grep -v "Renderer:\|Profiler" "$work/server.log" | tail -20
    echo "--- client log"; grep -v "Renderer:\|Profiler" "$work/client.log" | tail -20
fi
exit $failed
