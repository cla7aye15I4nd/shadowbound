#!/bin/bash
# Nginx end-to-end test for CI.
#
# Builds the latest nginx twice with the ShadowBound toolchain, natively and
# with -fsanitize=shadowbound (both with -flto, so the comparison isolates the
# instrumentation), then:
#   1. runs the official nginx test suite (nginx-tests) against both builds and
#      fails if the ShadowBound build fails any test the native build passes
#      (a false positive or a crash under instrumentation);
#   2. runs a short wrk benchmark against both and reports requests/second.
#
# Usage: SHADOWBOUND_BUILD=<llvm build dir> test/nginx/run.sh [workdir]
set -euo pipefail
HERE="$(dirname "$(realpath "$0")")"

: "${SHADOWBOUND_BUILD:?set SHADOWBOUND_BUILD to the LLVM build directory}"
NGINX_VERSION="${NGINX_VERSION:-1.31.6}"   # latest mainline release
WRK_VERSION="${WRK_VERSION:-4.2.0}"        # latest wrk release
DURATION="${DURATION:-10s}"
JOBS="${JOBS:-$(nproc)}"
WORK="$(realpath -m "${1:-$PWD/nginx-work}")"
CLANG="$SHADOWBOUND_BUILD/bin/clang"
SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/null}"

mkdir -p "$WORK"
cd "$WORK"

# --- sources --------------------------------------------------------------
if [ ! -d "nginx-$NGINX_VERSION" ]; then
  curl -fsSL "https://nginx.org/download/nginx-$NGINX_VERSION.tar.gz" | tar xz
fi
if [ ! -d nginx-tests ]; then
  git clone -q --depth 1 https://github.com/nginx/nginx-tests.git
fi
if [ ! -x wrk/wrk ]; then
  rm -rf wrk
  git -c advice.detachedHead=false clone -q --depth 1 --branch "$WRK_VERSION" https://github.com/wg/wrk.git
  # Use the system OpenSSL instead of wrk's bundled 1.1.1 tarball.
  make -C wrk -j"$JOBS" WITH_OPENSSL=/usr >/dev/null
fi
TESTS_REV="$(git -C nginx-tests rev-parse --short HEAD)"

# --- builds ---------------------------------------------------------------
build() { # name, extra compiler flags
  local name="$1" flags="$2"
  [ -x "$WORK/$name/sbin/nginx" ] && return
  rm -rf "build-$name"
  cp -r "nginx-$NGINX_VERSION" "build-$name"
  (
    cd "build-$name"
    ./configure --prefix="$WORK/$name" \
      --with-cc="$CLANG $flags -flto -fuse-ld=lld" \
      --with-cc-opt="-O2 -g -Wno-unused-command-line-argument" \
      --with-threads --with-http_ssl_module --with-http_v2_module \
      --with-http_realip_module --with-http_addition_module \
      --with-http_sub_module --with-http_dav_module \
      --with-http_gunzip_module --with-http_gzip_static_module \
      --with-http_auth_request_module --with-http_random_index_module \
      --with-http_secure_link_module --with-http_slice_module \
      --with-http_stub_status_module --with-stream \
      --with-stream_ssl_module --with-stream_realip_module >configure.log
    make -j"$JOBS" >make.log 2>&1 || { tail -40 make.log; exit 1; }
    make install >/dev/null
  )
}
build native ""
build shadowbound "-fsanitize=shadowbound"

# --- 1. nginx test suite --------------------------------------------------
run_suite() { # name
  local name="$1"
  (
    cd nginx-tests
    TEST_NGINX_BINARY="$WORK/$name/sbin/nginx" \
      prove -j"$JOBS" . >"$WORK/tests-$name.log" 2>&1 || true
  )
  # Test files with any failure, from prove's summary report.
  sed -n '/^Test Summary Report/,$p' "tests-$name.log" |
    grep -oE '^[^ ]+\.t ' | sort -u >"failed-$name.txt" || true
  echo "$name: $(tail -1 "tests-$name.log")"
}
run_suite native
run_suite shadowbound

# Failures that only happen under ShadowBound, minus the known false positives
# listed (with their cause) in known-failures.txt, are regressions.
KNOWN="$HERE/known-failures.txt"
sed 's|^\./||; s/ *$//' failed-native.txt | sort -u >failed-native.n
sed 's|^\./||; s/ *$//' failed-shadowbound.txt | sort -u >failed-shadowbound.n
grep -vE '^(#|$)' "$KNOWN" | sort -u >known.n
comm -13 failed-native.n failed-shadowbound.n >sb-only.txt
comm -23 sb-only.txt known.n >regressions.txt
comm -13 sb-only.txt known.n >fixed.txt
{
  echo "### nginx $NGINX_VERSION, nginx-tests @ $TESTS_REV"
  echo
  echo "| build | result |"
  echo "|---|---|"
  echo "| native | $(tail -1 tests-native.log) |"
  echo "| shadowbound | $(tail -1 tests-shadowbound.log) |"
  echo
  echo "ShadowBound-only failures: $(wc -l <sb-only.txt) test files," \
       "$(wc -l <regressions.txt) not in known-failures.txt."
  if [ -s fixed.txt ]; then
    echo "Listed in known-failures.txt but now passing (remove them):" \
         "$(tr '\n' ' ' <fixed.txt)"
  fi
  echo
} | tee -a "$SUMMARY"

# --- 2. benchmark ---------------------------------------------------------
cat >bench.conf <<'EOF'
worker_processes 1;
daemon on;
error_log logs/error.log;
events { worker_connections 1024; }
http {
  access_log off;
  server { listen 127.0.0.1:18080; location / { root html; } }
}
EOF
bench() { # name -> requests/sec
  local name="$1" rps
  cp bench.conf "$WORK/$name/conf/bench.conf"
  "$WORK/$name/sbin/nginx" -p "$WORK/$name" -c conf/bench.conf
  sleep 1
  rps=$(wrk/wrk -t2 -c50 -d"$DURATION" http://127.0.0.1:18080/index.html |
    awk '/Requests\/sec/ {print $2}')
  "$WORK/$name/sbin/nginx" -p "$WORK/$name" -c conf/bench.conf -s stop || true
  sleep 1
  echo "$rps"
}
NATIVE_RPS=$(bench native)
SB_RPS=$(bench shadowbound)
{
  echo "| wrk $WRK_VERSION, $DURATION | requests/s |"
  echo "|---|---:|"
  echo "| native | $NATIVE_RPS |"
  echo "| shadowbound | $SB_RPS ($(awk -v a="$SB_RPS" -v b="$NATIVE_RPS" 'BEGIN{printf "%+.1f%%", (a/b-1)*100}')) |"
  echo
} | tee -a "$SUMMARY"

if [ -s regressions.txt ]; then
  echo "FAIL: new tests that pass natively but fail under ShadowBound:" >&2
  cat regressions.txt >&2
  for t in $(sed 's|^|./|' regressions.txt); do
    echo "::group::$t"
    (cd nginx-tests && TEST_NGINX_BINARY="$WORK/shadowbound/sbin/nginx" \
       TEST_NGINX_VERBOSE=1 prove -v "$t" 2>&1 | tail -40) || true
    echo "::endgroup::"
  done
  exit 1
fi
echo "OK: no ShadowBound-only test failures beyond known-failures.txt"
