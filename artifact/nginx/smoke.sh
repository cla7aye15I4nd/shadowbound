#!/bin/bash
# Availability smoke test: start a given nginx build and verify it serves a
# request. Used by CI to guarantee the Nginx experiment stays runnable.
set -euo pipefail

: "${TARGET:?set TARGET to native|shadowbound|shadowbound-ffmalloc|shadowbound-markus}"

pushd nginx >/dev/null
./"$TARGET"/sbin/nginx
ok=0
for _ in $(seq 1 30); do
    if curl -fsS http://localhost:80/index.html >/dev/null 2>&1; then
        ok=1
        break
    fi
    sleep 0.5
done
./"$TARGET"/sbin/nginx -s stop || true
popd >/dev/null

if [ "$ok" -ne 1 ]; then
    echo "SMOKE FAIL: nginx ($TARGET) did not serve a request" >&2
    exit 1
fi
echo "SMOKE OK: nginx ($TARGET) served index.html"
