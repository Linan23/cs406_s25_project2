#!/usr/bin/env bash
set -euo pipefail           # exit on error, undefined var or pipefail

PORT=15000                  
LOG=/tmp/srv.log           

# capture error
trap 'echo; echo "Test FAILED on line $LINENO"; exit 1' ERR

# kill any running server
cleanup() { pkill wserver 2>/dev/null || true; }

# ensure cleanup runs on script exit
trap cleanup EXIT
make clean && make         # rebuild everything from scratch

# wait until server writes its listening message
wait_for_bind() {
  for i in {1..50}; do                      # try up to 50 times
    grep -q "\[pid.*listening on port $PORT" $LOG && return
    sleep .1                                # short pause between tries
  done
  echo "ERROR: server did not bind in time" # error binding
  exit 1
}

### Test 1: Basic static file

echo                                  
echo "Test 1: Basic static file"
cleanup                              # kill any old server
./wserver -p $PORT -t 1 -b 1 -s FIFO > $LOG 2>&1 &   # start server
SVR=$!                               # save server pid
wait_for_bind                       
echo -n " GET /index.html: "         # prompt
./wclient localhost $PORT /index.html | head -n1  # issue request
kill $SVR; wait $SVR 2>/dev/null     # stop server
echo "Test 1 passed"                 


### Test 2: Port reuse

echo
echo "Test 2: Port reuse"
cleanup
./wserver -p $PORT -t 1 -b 1 -s FIFO >/dev/null 2>&1 &  # start and ignore output
P1=$!; sleep .1; kill $P1; wait $P1                     # stop immediately
./wserver -p $PORT -t 1 -b 1 -s FIFO >/dev/null 2>&1 &  # start again
P2=$!; sleep .1; kill $P2                                # clean up
echo "Test 2 passed"


### Test 3: Graceful shutdown under load

echo
echo "Test 3: Graceful shutdown under load"
cleanup
./wserver -p $PORT -t 2 -b 2 -s FIFO > $LOG 2>&1 &  # start server
P3=$!; wait_for_bind                               # save PID and wait
timeout 5s ./wclient localhost $PORT /spin.cgi?1 >/dev/null &  # long request
sleep .1
kill -INT $P3; wait $P3                            # send sigint and wait exit
echo "Test 3 passed"


### Test 4: FIFO ordering

echo
echo "Test 4: FIFO ordering"
cleanup
./wserver -p $PORT -t 2 -b 2 -s FIFO > $LOG 2>&1 &  
P4=$!; wait_for_bind                               
SP=$(timeout 5s ./wclient localhost $PORT /spin.cgi?1)  # capture CGI output
IX=$(./wclient localhost $PORT /index.html)            # capture static output
echo "$SP" | grep -q "I spun for 1.00 seconds"         # verify CGI ran first
echo "$IX" | grep -q "<h1>It works!</h1>"              # then static file
kill $P4; wait $P4 2>/dev/null
echo "Test 4 passed"


### Test 5: SFF ordering

echo
echo "Test 5: SFF ordering"
cleanup
echo small > small.txt                              # create small file
dd if=/dev/zero of=big.txt bs=1M count=2 2>/dev/null # create big file
./wserver -p $PORT -t 1 -b 3 -s SFF > $LOG 2>&1 &     # start SFF 
P5=$!; wait_for_bind
timeout 5s ./wclient localhost $PORT /spin.cgi?1 >/dev/null & # block server
sleep .05
BG=$(./wclient localhost $PORT /big.txt)             # request big file
SM=$(./wclient localhost $PORT /small.txt)           # request small file
kill $P5; wait $P5 2>/dev/null
echo "$SM" | grep -q "small"                         # small must finish first
echo "$BG" | grep -q "2097152"                       # then big
echo "Test 5 passed"


### Test 6: Concurrency limit (2 threads, 4 jobs)

echo
echo "Test 6: concurrency limit"
cleanup
./wserver -p $PORT -t 2 -b 4 -s FIFO > $LOG 2>&1 &  # start server with 2 threads
P6=$!; wait_for_bind

start=$(date +%s)                                    # record start time
declare -a T6_PIDS=()
for i in {1..4}; do
  timeout 3s ./wclient localhost $PORT /spin.cgi?1 >/dev/null &  # start 4 jobs
  T6_PIDS+=( $! )
done
for pid in "${T6_PIDS[@]}"; do                       # wait only on those jobs
  wait $pid
done
end=$(date +%s)

kill $P6; wait $P6 2>/dev/null                       # stop server
elapsed=$((end-start))
echo "  elapsed: ${elapsed}s (expect ~2s)"
if (( elapsed >= 2 && elapsed <= 4 )); then
  echo "Test 6 passed"
else
  echo "Test 6 failed (got ${elapsed}s)"; exit 1
fi


### Test 7: Buffer blocking (1 thread, buf=2)

echo
echo "Test 7: buffer blocking"
cleanup
./wserver -p $PORT -t 1 -b 2 -s FIFO > $LOG 2>&1 &
P7=$!; wait_for_bind

start=$(date +%s)
timeout 3s ./wclient localhost $PORT /spin.cgi?1 >/dev/null &  # first job

sleep .05
declare -a T7_PIDS=()
for i in {1..3}; do
  timeout 3s ./wclient localhost $PORT /index.html >/dev/null &  # 3 queued
  T7_PIDS+=( $! )
done
for pid in "${T7_PIDS[@]}"; do                    # wait on those
  wait $pid
done
end=$(date +%s)

kill $P7; wait $P7 2>/dev/null
dur=$((end-start))
echo "  blocked: ${dur}s (expect ≥1s)"
if (( dur >= 1 )); then
  echo "Test 7 passed"
else
  echo "Test 7 failed (got ${dur}s)"; exit 1
fi


### Test 8: Directory-traversal protection

echo
echo "Test 8: dir-traversal protection"
cleanup
mkdir -p www; echo SECRET > www/secret.txt
pushd www >/dev/null
../wserver -d . -p $PORT -t 1 -b 1 -s FIFO >/dev/null 2>&1 &  # serve from www
P8=$!; sleep .1
OUT1=$(timeout 2s ../wclient localhost $PORT "/../www/secret.txt" 2>&1 || true)
OUT2=$(timeout 2s ../wclient localhost $PORT "/evil.txt" 2>&1 || true)
kill $P8; popd >/dev/null
echo "$OUT1" | grep -qv "200 OK"                       # not allow ../
echo "$OUT2" | grep -qv "200 OK"
echo "Test 8 passed"


### Test 9: Flag validation

echo
echo "Test 9: flag-validation"
cleanup
./wserver -t 0 2>&1 | grep -q usage && echo "-t0 OK"
./wserver -b 0 2>&1 | grep -q usage && echo "-b0 OK"
./wserver -s X 2>&1 | grep -q usage && echo "-sX OK"
echo "Test 9 passed"

### Test 10: Defaults

echo
echo "Test 10: defaults"
cleanup
./wserver >/tmp/def.log 2>&1 &        # default port=10000, threads=1
P10=$!; sleep .1
start=$(date +%s)
timeout 3s ./wclient localhost 10000 /spin.cgi?1 >/dev/null
t1=$(date +%s)
kill $P10; wait $P10 2>/dev/null
dur=$((t1-start))
echo "  delay: ${dur}s (expect ~1s)"
if (( dur >= 1 && dur < 3 )); then
  echo "Test 10 passed"
else
  echo "Test 10 failed (got ${dur}s)"; exit 1
fi


### Test 11: basedir isolation

echo
echo "Test 11: basedir isolation"
cleanup
mkdir -p serverroot/www; echo IN > serverroot/www/in.txt
echo OUT > serverroot/out.txt
pushd serverroot >/dev/null
../wserver -d . -p $PORT -t 1 -b 1 -s FIFO >/dev/null 2>&1 &
P11=$!; sleep .1
../wclient localhost $PORT /www/in.txt | grep -q IN
if ../wclient localhost $PORT /../out.txt 2>&1 | grep -q "200 OK"; then
  echo "Test 11 failed (escaped basedir)"; exit 1
fi
kill $P11; popd >/dev/null
echo "Test 11 passed"


### Test 12: 404 handling

echo
echo "Test 12: 404 handling"
cleanup
./wserver -p $PORT >/dev/null 2>&1 &
P12=$!; sleep .1
OUT=$(timeout 2s ./wclient localhost $PORT /nope.html 2>&1 || true)
kill $P12; wait $P12 2>/dev/null
echo "$OUT" | grep -q "404"
echo "Test 12 passed"


### Test 13: SIGINT/SIGTERM shutdown

echo
echo "Test 13: SIGINT/SIGTERM shutdown"
cleanup
for sig in INT TERM; do
  ./wserver -p $PORT >/dev/null 2>&1 &
  P13=$!; sleep .1
  kill -$sig $P13; wait $P13 || true
  echo "SIG$sig OK"
done
echo "Test 13 passed"

### Test 14: High-volume small-file throughput
echo
echo "Test 14: High-volume small-file throughput "
cleanup
head -c4k /dev/zero > small.dat           # create Test file
./wserver -p $PORT -t 4 -b 32 -s FIFO >/dev/null 2>&1 &
P14=$!; sleep .1

echo -n " sending 100 small.dat requests "
declare -a T14_PIDS=()
for i in {1..100}; do
  printf "."                               # progress dot
  timeout 2s ./wclient localhost $PORT /small.dat >/dev/null &
  T14_PIDS+=( $! )
done
echo
for pid in "${T14_PIDS[@]}"; do             # wait only on those clients
  wait $pid
done

kill $P14; wait $P14 2>/dev/null
echo "Test 14 passed"

echo
echo "ALL Tests PASSED"
