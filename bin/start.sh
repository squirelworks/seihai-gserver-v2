#!/bin/sh
# Seihai lab world: start gs2emu on 127.0.0.1 (Linux and macOS).
# Part of the Seihai fork of GServer-v2 (GPL-3.0): see CHANGES-SEIHAI.md.
#
#   ./start.sh            start both lab servers:
#                           servers/default  2.00 to 3.153  127.0.0.1:14802
#                           servers/classic  1.33 to 1.41r1 127.0.0.1:14803
#   ./start.sh classic    start one server folder, in this terminal
#
# Stop with Ctrl+C. With more than one server, each one's console output goes
# to servers/<name>/logs/console.txt.

cd "$(dirname "$0")" || exit 1

EXE=./gs2emu
if [ ! -x "$EXE" ]; then
	echo "start.sh: no gs2emu next to this script. Build it or unpack a release first (see README.md)." >&2
	exit 1
fi

[ $# -eq 0 ] && set -- default classic

for s in "$@"; do
	if [ ! -f "servers/$s/config/serveroptions.txt" ]; then
		echo "start.sh: no server folder servers/$s" >&2
		exit 1
	fi
done

# Print "name on ip:port" from a folder's serveroptions.txt.
describe() {
	opts="servers/$1/config/serveroptions.txt"
	ip=$(sed -n 's/^serverinterface *= *\([^ 	]*\).*/\1/p' "$opts")
	port=$(sed -n 's/^serverport *= *\([^ 	]*\).*/\1/p' "$opts")
	echo "$1 on ${ip:-AUTO}:${port:-?}"
}

# One server: run it in the foreground.
if [ $# -eq 1 ]; then
	echo "Starting $(describe "$1"). Stop with Ctrl+C."
	exec "$EXE" --server "$1"
fi

# Several servers: run each in the background and stop them all together.
pids=""
stop() {
	trap '' INT TERM
	echo "Stopping..."
	[ -n "$pids" ] && kill $pids 2>/dev/null
	wait
	exit 0
}
trap stop INT TERM

for s in "$@"; do
	mkdir -p "servers/$s/logs"
	"$EXE" --server "$s" > "servers/$s/logs/console.txt" 2>&1 &
	pids="$pids $!"
	echo "Started $(describe "$s"), pid $!, console in servers/$s/logs/console.txt"
done

# Report a server that fails to start, with the end of its console output.
sleep 2
i=0
for pid in $pids; do
	i=$((i + 1))
	eval "s=\${$i}"
	if ! kill -0 "$pid" 2>/dev/null; then
		echo "start.sh: $s stopped right away. The end of servers/$s/logs/console.txt:" >&2
		tail -n 15 "servers/$s/logs/console.txt" >&2
	fi
done

echo "Lab world running. Stop with Ctrl+C."
wait
