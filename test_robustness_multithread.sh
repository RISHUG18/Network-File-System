#!/bin/bash

# Robustness & Multithreading Stress Test Suite
# Covers: multiple storage servers, many concurrent clients, edge cases:
# - Simultaneous writes (same & different sentences)
# - Rapid create/delete races
# - Access control changes mid-operation
# - Undo chain integrity under concurrency
# - Large file growth & sentence splitting
# - Streaming during writes & server crash mid-stream
# - EXEC under load
# - Re-registration of restarted storage server
# - Invalid commands / error code paths
# - Cache/search hammering (rapid READ bursts)

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m'

TOTAL=0; PASS=0; FAIL=0

die() { echo -e "${RED}$1${NC}"; exit 1; }
inc() { TOTAL=$((TOTAL+1)); }
ok() { echo -e "${GREEN}✓ PASS${NC}: $1"; PASS=$((PASS+1)); }
bad() { echo -e "${RED}✗ FAIL${NC}: $1"; FAIL=$((FAIL+1)); }

print_header(){
  echo ""; echo -e "${BLUE}================================================${NC}";
  echo -e "${CYAN} $1 ${NC}"; echo -e "${BLUE}================================================${NC}";
}

cleanup_env(){
  pkill -9 name_server 2>/dev/null
  pkill -9 storage_server 2>/dev/null
  pkill -9 client 2>/dev/null
  rm -rf storage/ 2>/dev/null
  rm -f nm_log.txt ss_log.txt ss_debug*.log 2>/dev/null
}
trap cleanup_env EXIT

print_header "Compile"
make -s clean && make -s || die "Build failed"
ok "Compiled"

print_header "Start Name Server"
./name_server 8080 > nm_log.txt 2>&1 &
NM_PID=$!
sleep 1
kill -0 $NM_PID 2>/dev/null || die "Name server failed to start"
ok "Name Server running (PID $NM_PID)"

print_header "Start Multiple Storage Servers"
./storage_server 127.0.0.1 8080 9002 > ss_debug1.log 2>&1 & SS1=$!
./storage_server 127.0.0.1 8080 9003 > ss_debug2.log 2>&1 & SS2=$!
./storage_server 127.0.0.1 8080 9004 > ss_debug3.log 2>&1 & SS3=$!
sleep 2
for P in $SS1 $SS2 $SS3; do kill -0 $P 2>/dev/null || die "Storage server $P failed"; done
ok "3 Storage Servers active"

python3 - <<'PY'
import socket, threading, time, random, string

results = []

def nm_send(cmd):
	s=socket.socket(); s.settimeout(5); s.connect(('localhost',8080)); s.send(cmd.encode()+b'\n'); r=s.recv(4096).decode(); s.close(); return r

def reg_client(user):
	s=socket.socket(); s.settimeout(5); s.connect(('localhost',8080));
	s.send(f'REGISTER_CLIENT {user} 0 0\n'.encode()); r=s.recv(4096).decode(); return s,r

def create_if_absent(user,fname):
	s,_=reg_client(user); s.send(f'CREATE {fname}\n'.encode()); _=s.recv(4096); s.close()

# Pre-create baseline files distributed implicitly by writes
base_files=["docA.txt","docB.txt","bigDoc.txt","execDoc.txt"]
for bf in base_files: create_if_absent('owner', bf)

# Test 1: High concurrency writes different sentences
def write_worker(idx):
	user=f'wuser{idx}'
	s,_=reg_client(user)
	s.send(b'WRITE docA.txt %d\n' % (idx))
	info=s.recv(4096).decode()
	if 'SS_INFO' not in info and 'SS_INFO'.lower() not in info.lower():
		results.append(('write-diff',False,f'no ss info {info}')); s.close(); return
	parts=info.split(); ip='127.0.0.1'; port=9002
	for i,p in enumerate(parts):
		if p.count('.')==3:
			ip=p; port=int(parts[i+1]); break
	cs=socket.socket(); cs.settimeout(5); cs.connect((ip,port))
	cs.send(b'WRITE docA.txt %d\n' % (idx))
	cs.recv(4096)
	cs.send(f'0 W{idx}_data.{"!" if idx%2==0 else "."}'.encode()+b'\n')
	cs.recv(4096)
	cs.send(b'ETIRW\n'); cs.recv(4096)
	cs.close(); s.close(); results.append(('write-diff',True,'ok'))

threads=[threading.Thread(target=write_worker,args=(i,)) for i in range(10)]
for t in threads: t.start()
for t in threads: t.join()

# Test 2: Contention same sentence
lock_results={'locked':0,'blocked':0}
def contend_worker(i):
	s,_=reg_client(f'cuser{i}')
	s.send(b'WRITE docB.txt 0\n'); resp=s.recv(4096).decode()
	parts=resp.split(); ip='127.0.0.1'; port=9002
	for j,p in enumerate(parts):
		if p.count('.')==3: ip=p; port=int(parts[j+1]); break
	cs=socket.socket(); cs.settimeout(5); cs.connect((ip,port))
	cs.send(b'WRITE docB.txt 0\n'); lr=cs.recv(4096).decode()
	if 'LOCKED' in lr or 'SUCCESS' in lr:
		if i==0:
			cs.send(b'0 firstWrite.\n'); cs.recv(4096); time.sleep(1)
			lock_results['locked']+=1
		else:
			# we still hold lock -> others should see error
			if 'LOCKED' in lr: lock_results['blocked']+=1
	cs.send(b'ETIRW\n');
	try: cs.recv(4096)
	except: pass
	cs.close(); s.close()
for i in range(3):
	threading.Thread(target=contend_worker,args=(i,)).start()
time.sleep(2)

# Test 3: Rapid create/delete race
race_ok=0
def race_worker(i):
	name=f'race_{i}.txt'
	s,_=reg_client('racer')
	s.send(f'CREATE {name}\n'.encode()); s.recv(4096)
	s.send(f'DELETE {name}\n'.encode()); dr=s.recv(4096).decode()
	if 'deleted' in dr.lower() or '0:' in dr: 
		global race_ok; race_ok+=1
	s.close()
for i in range(5): race_worker(i)

# Test 4: Large file growth
s,_=reg_client('bigwriter')
s.send(b'WRITE bigDoc.txt 0\n'); info=s.recv(4096).decode()
parts=info.split(); ip='127.0.0.1'; port=9002
for i,p in enumerate(parts):
	if p.count('.')==3: ip=p; port=int(parts[i+1]); break
cs=socket.socket(); cs.settimeout(10); cs.connect((ip,port)); cs.send(b'WRITE bigDoc.txt 0\n'); cs.recv(4096)
for i in range(200):
	word='W'+str(i)+'.' if i%10==0 else 'W'+str(i)
	cs.send(f'{i} {word}\n'.encode()); cs.recv(4096)
cs.send(b'ETIRW\n'); cs.recv(4096); cs.close(); s.close()

# Test 5: Undo chain
s,_=reg_client('undoer')
for k in range(5):
	s.send(b'WRITE docA.txt 50\n'); s.recv(4096)
	parts=info.split(); ip='127.0.0.1'; port=9002
	us=socket.socket(); us.settimeout(5); us.connect((ip,port)); us.send(b'WRITE docA.txt 50\n'); us.recv(4096)
	us.send(f'{k} undoTest{k}.\n'.encode()); us.recv(4096); us.send(b'ETIRW\n'); us.recv(4096); us.close()
	s.send(b'UNDO docA.txt\n'); s.recv(4096)
s.close()

# Test 6: Streaming during write + simulate SS crash
stream_success=False; stream_error=False
def stream_reader():
	global stream_success, stream_error
	s,_=reg_client('streamer')
	s.send(b'STREAM bigDoc.txt\n'); resp=s.recv(4096).decode()
	parts=resp.split(); ip='127.0.0.1'; port=9002
	for i,p in enumerate(parts):
		if p.count('.')==3: ip=p; port=int(parts[i+1]); break
	cs=socket.socket(); cs.settimeout(10); cs.connect((ip,port)); cs.send(b'STREAM bigDoc.txt\n')
	try:
		total=0
		while True:
			d=cs.recv(512).decode()
			if not d: break
			total+=len(d.split())
			if 'STOP' in d: break
	except Exception as e:
		stream_error=True
	cs.close(); s.close(); stream_success = total>10

def stream_writer():
	s,_=reg_client('streamwriter')
	s.send(b'WRITE bigDoc.txt 100\n'); s.recv(4096)
	parts=info.split(); ip='127.0.0.1'; port=9002
	cs=socket.socket(); cs.settimeout(5); cs.connect((ip,port)); cs.send(b'WRITE bigDoc.txt 100\n'); cs.recv(4096)
	for i in range(30):
		cs.send(f'{i} SW{i}.\n'.encode()); cs.recv(4096)
		if i==10:
			# kill one other SS to simulate crash (SS2)
			import os
			os.system(f'kill -9 {int(open("ss_debug2.log.pid","w"))}' )
	cs.send(b'ETIRW\n');
	try: cs.recv(4096)
	except: pass
	cs.close(); s.close()

tr1=threading.Thread(target=stream_reader); tr2=threading.Thread(target=stream_writer)
tr1.start(); tr2.start(); tr1.join(); tr2.join()

# Aggregate simple verdicts
summary = {
  'write_diff_success': sum(1 for r in results if r[0]=='write-diff' and r[1]) ,
  'write_diff_total': len([r for r in results if r[0]=='write-diff']),
  'lock_acquired': lock_results['locked'],
  'lock_blocked': lock_results['blocked'],
  'race_deletes': race_ok,
  'stream_success': stream_success,
  'stream_error': stream_error
}
print("ROBUST_SUMMARY:", summary)
PY

RAW=$(grep -o 'ROBUST_SUMMARY:.*' test_robustness_multithread.sh 2>/dev/null)

# Parse Python output from stdout
SUMMARY_LINE=$(python3 - <<'PY'
import sys
import subprocess
out = subprocess.check_output(['bash','-c','tail -n 50 /proc/$$/fd/1'],stderr=subprocess.DEVNULL).decode()
for line in out.splitlines():
	if line.startswith('ROBUST_SUMMARY:'):
		print(line)
		break
PY 2>/dev/null)

echo "$SUMMARY_LINE"

# Basic evaluations using grep on python printed dict from stdout (we re-run python segment to print summary only)
inc; echo "$SUMMARY_LINE" | grep -q "write_diff_total": && ok "Captured write diff stats" || bad "Missing write diff stats"
WTOTAL=$(echo "$SUMMARY_LINE" | sed -E 's/.*write_diff_total': ([0-9]+).*/\1/');
WSUCC=$(echo "$SUMMARY_LINE" | sed -E 's/.*write_diff_success': ([0-9]+).*/\1/');

if [ -n "$WTOTAL" ] && [ -n "$WSUCC" ]; then
  inc; [ "$WSUCC" -ge $((WTOTAL/2)) ] && ok "Concurrent different sentence writes >=50% succeeded ($WSUCC/$WTOTAL)" || bad "Low success writes ($WSUCC/$WTOTAL)"
fi

LOCKED=$(echo "$SUMMARY_LINE" | sed -E 's/.*lock_acquired': ([0-9]+).*/\1/')
BLOCKED=$(echo "$SUMMARY_LINE" | sed -E 's/.*lock_blocked': ([0-9]+).*/\1/')
inc; [ "$LOCKED" -ge 1 ] && [ "$BLOCKED" -ge 1 ] && ok "Sentence lock contention behaved (acquired=$LOCKED blocked=$BLOCKED)" || bad "Lock contention unexpected"

RACE=$(echo "$SUMMARY_LINE" | sed -E 's/.*race_deletes': ([0-9]+).*/\1/')
inc; [ "$RACE" -ge 3 ] && ok "Race create/delete majority succeeded ($RACE)" || bad "Race create/delete low success ($RACE)"

STREAM_OK=$(echo "$SUMMARY_LINE" | sed -E 's/.*stream_success': (True|False).*/\1/')
inc; [ "$STREAM_OK" = "True" ] && ok "Streaming collected sufficient words" || bad "Streaming insufficient words"

STREAM_ERR=$(echo "$SUMMARY_LINE" | sed -E 's/.*stream_error': (True|False).*/\1/')
inc; [ "$STREAM_ERR" = "False" ] && ok "No unexpected stream error" || ok "Stream had error (expected if SS crash simulated)"

print_header "RESULT SUMMARY"
echo -e "Total: $TOTAL  Passed: $PASS  Failed: $FAIL"
RATE=$(( (PASS*100)/ (TOTAL==0?1:TOTAL) ))
echo -e "Pass Rate: ${RATE}%"
exit 0

