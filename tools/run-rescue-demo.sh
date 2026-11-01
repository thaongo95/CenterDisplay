#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
docker compose -f docker-compose.rescue.yml up -d --wait
if [[ ! -x build/map-check/bin/centerDisplay ]]; then
    echo 'Build CenterDisplay first; see docs/RESCUE_DEMO.md.' >&2
    exit 1
fi
demo_dir=$(mktemp -d /tmp/center-rescue-demo.XXXXXX)
mock_pid=''
cleanup() {
    if [[ -n "$mock_pid" ]]; then kill "$mock_pid" 2>/dev/null || true; wait "$mock_pid" 2>/dev/null || true; fi
    rm -rf "$demo_dir"
}
trap cleanup EXIT
export CENTER_DEMO_SCENARIO="$demo_dir/scenario.json"
export CENTER_DEMO_MISSIONS_DIR="$demo_dir"
export CENTER_DB_SCHEMA="rescue_demo_$(date +%Y%m%d_%H%M%S)_$$"
python3 tools/rescue_scenario.py
python3 -u tools/mock_gcs.py &
mock_pid=$!
# Wait for the listener, while detecting an occupied port or early startup failure.
python3 - "$mock_pid" <<'PY'
import os, socket, sys, time
pid=int(sys.argv[1])
for _ in range(30):
    os.kill(pid,0)
    try:
        with socket.create_connection(('127.0.0.1',8766),timeout=0.2):
            break
    except OSError:
        time.sleep(0.1)
else:
    raise SystemExit('Demo GCS failed to start')
PY
QT_QUICK_BACKEND=software python3 tools/simulate_uavs.py --launch --rescue
