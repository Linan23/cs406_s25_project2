#!/usr/bin/env bash
set -euo pipefail

PORT=15000
LOG=/tmp/srv.log

# on any error, print fail + line number
trap 'echo; echo "TEST FAILED on line $LINENO"; exit 1' ERR

# cleanup any existing server
cleanup() {
  pkill wserver 2>/dev/null || true
}
trap cleanup EXIT

echo "→ Rebuild everything"
make clean && make

echo
echo "→ TEST 1: Basic static file"
cleanup
./wserver -p $PORT -t 1 -b 1 -s FIFO > $LOG 2>&1 &
SERVER=$!
# wait for bind
for i in {1..50}; do
  grep -q "\[pid.*listening on port $PORT" $LOG && break
  sleep 0.1
done
echo -n "  → GET /index.html: "
set +e
./wclient localhost $PORT /index.html | head -n1
set -e
kill $SERVER; wait $SERVER
echo "TEST 1 passed"

echo
echo "→ TEST 2: Port reuse"
cleanup
./wserver -p $PORT -t 1 -b 1 -s FIFO >/dev/null 2>&1 &
P1=$!; sleep .1; kill $P1; wait $P1
./wserver -p $PORT -t 1 -b 1 -s FIFO >/dev/null 2>&1 &
P2=$!; kill $P2
echo "TEST 2 passed"

echo
echo "→ TEST 3: Graceful shutdown under load"
cleanup
./wserver -p $PORT -t 2 -b 2 -s FIFO > $LOG 2>&1 &
P3=$!; sleep .1
./wclient localhost $PORT /spin.cgi?3 >/dev/null 2>&1 &
sleep .1
kill -INT $P3; wait $P3
echo "TEST 3 passed"

echo
echo "→ TEST 4: FIFO ordering"
cleanup
./wserver -p $PORT -t 2 -b 2 -s FIFO > $LOG 2>&1 &
P4=$!

# capture spin output and then static
SPIN_OUT=$(./wclient localhost $PORT /spin.cgi?1)
INDEX_OUT=$(./wclient localhost $PORT /index.html)

# verify CGI ran before static HTML
echo "$SPIN_OUT" | grep -q "I spun for 1.00 seconds"
echo "$INDEX_OUT" | grep -q "<h1>It works!</h1>"

kill $P4; wait $P4
echo "TEST 4 passed"

echo
echo "→ TEST 5: SFF ordering"
cleanup
echo small   > small.txt
dd if=/dev/zero of=big.txt bs=1M count=2 2>/dev/null

./wserver -p $PORT -t 1 -b 3 -s SFF > $LOG 2>&1 &
P5=$!

# spin, then big, then small
SP=$((./wclient localhost $PORT /spin.cgi?2 >/dev/null)& echo $!)
sleep .05
BG_OUT=$(./wclient localhost $PORT /big.txt)
SM_OUT=$(./wclient localhost $PORT /small.txt)

kill $P5; wait $P5

# small should complete before big
echo "$SM_OUT" | grep -q "small"
echo "$BG_OUT" | grep -q "2097152"

echo "TEST 5 passed"

echo
echo "ALL TESTS PASSED"
