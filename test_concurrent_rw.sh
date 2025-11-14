#!/bin/bash
# Concurrent multi-client read/write stress test
# Usage: SS_PORT=9030 NUM_WRITERS=8 NUM_READERS=4 RUN_SECONDS=5 ./test_concurrent_rw.sh

SS_PORT=${SS_PORT:-9030}
NUM_WRITERS=${NUM_WRITERS:-4}
NUM_READERS=${NUM_READERS:-2}
RUN_SECONDS=${RUN_SECONDS:-5}
NM_PORT=8080
FILE=conc.txt

set -e
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'

cleanup(){ pkill -9 name_server 2>/dev/null || true; pkill -9 storage_server 2>/dev/null || true; pkill -9 client 2>/dev/null || true; }
trap cleanup EXIT

printf "${CYAN}Building...${NC}\n"
make -s clean && make -s

printf "${CYAN}Starting servers (SS_PORT=$SS_PORT) ...${NC}\n"
./name_server $NM_PORT > nm_rw.log 2>&1 & NM=$!
sleep 1
./storage_server 127.0.0.1 $NM_PORT $SS_PORT > ss_rw.log 2>&1 & SS=$!
sleep 2
if ! ps -p $NM >/dev/null || ! ps -p $SS >/dev/null; then echo -e "${RED}Server start failed${NC}"; sed -n '1,120p' nm_rw.log; sed -n '1,120p' ss_rw.log; exit 1; fi

printf "${CYAN}Creating test file via single client...${NC}\n"
python3 - <<EOF
import socket
s=socket.socket(); s.connect(('localhost',${NM_PORT}))
s.send(b'REGISTER_CLIENT init 9300 9400\n'); s.recv(4096)
s.send(b'CREATE ${FILE}\n'); r=s.recv(4096).decode(); print('CREATE_RESP', r.strip()); s.close()
EOF

printf "${CYAN}Launching writer + reader swarm...${NC}\n"
python3 - <<EOF
import socket, threading, time, os, random
NM_PORT=${NM_PORT}; SS_PORT=${SS_PORT}; FILE='${FILE}'
stop_time=time.time()+${RUN_SECONDS}
REG_TIMEOUT=5
lock_fail=0; write_success=0; read_success=0; read_errors=0
sentences_written=set()
log_lock=threading.Lock()

def get_ip_port(resp, default):
    parts=resp.split(); ip='127.0.0.1'; port=default
    for i,p in enumerate(parts):
        if p.count('.')==3:
            ip=p
            if i+1 < len(parts):
                try: port=int(parts[i+1])
                except: pass
            break
    return ip,port

def writer(idx):
    global write_success, lock_fail
    while time.time() < stop_time:
        try:
            nm=socket.socket(); nm.settimeout(REG_TIMEOUT); nm.connect(('localhost',NM_PORT))
            nm.send(f'REGISTER_CLIENT w{idx} {9500+idx} {9600+idx}\n'.encode()); nm.recv(4096)
            target_sentence=random.randint(0, ${NUM_WRITERS}*2)
            nm.send(f'WRITE {FILE} {target_sentence}\n'.encode()); resp=nm.recv(4096).decode()
            if 'LOCKED' in resp or 'locked' in resp:
                lock_fail+=1; nm.close(); continue
            if 'SS_INFO' not in resp:
                lock_fail+=1; nm.close(); continue
            ip,port=get_ip_port(resp,SS_PORT)
            ss=socket.socket(); ss.settimeout(REG_TIMEOUT); ss.connect((ip,port))
            ss.send(f'WRITE {FILE} {target_sentence}\n'.encode()); ss.recv(4096)
            payload=f'{target_sentence} writer{idx}-{target_sentence}-{int(time.time()*1000)}\n'
            ss.send(payload.encode()); ss.recv(4096)
            ss.send(b'ETIRW\n'); ss.recv(4096); ss.close(); nm.close()
            with log_lock:
                sentences_written.add(target_sentence)
                write_success+=1
            time.sleep(0.05)
        except Exception as e:
            lock_fail+=1


def reader(idx):
    global read_success, read_errors
    while time.time() < stop_time:
        try:
            nm=socket.socket(); nm.settimeout(REG_TIMEOUT); nm.connect(('localhost',NM_PORT))
            nm.send(f'REGISTER_CLIENT r{idx} {9700+idx} {9800+idx}\n'.encode()); nm.recv(4096)
            nm.send(f'READ {FILE}\n'.encode()); resp=nm.recv(4096).decode()
            if 'SS_INFO' not in resp:
                read_errors+=1; nm.close(); continue
            ip,port=get_ip_port(resp,SS_PORT)
            ss=socket.socket(); ss.settimeout(REG_TIMEOUT); ss.connect((ip,port))
            ss.send(f'READ {FILE}\n'.encode()); data=ss.recv(8192).decode(); ss.close(); nm.close()
            if 'error' in data.lower():
                read_errors+=1
            else:
                read_success+=1
            time.sleep(0.05)
        except Exception as e:
            read_errors+=1

threads=[]
for i in range(${NUM_WRITERS}):
    t=threading.Thread(target=writer, args=(i,), daemon=True); threads.append(t); t.start()
for i in range(${NUM_READERS}):
    t=threading.Thread(target=reader, args=(i,), daemon=True); threads.append(t); t.start()
while time.time() < stop_time:
    alive=sum(1 for t in threads if t.is_alive())
    time.sleep(0.2)
for t in threads: t.join(timeout=0.5)

# Final read
try:
    nm=socket.socket(); nm.settimeout(8); nm.connect(('localhost',NM_PORT))
    nm.send(b'REGISTER_CLIENT final 9900 9910\n'); nm.recv(4096)
    nm.send(f'READ {FILE}\n'.encode()); r=nm.recv(4096).decode(); ip,port=get_ip_port(r,SS_PORT)
    ss=socket.socket(); ss.settimeout(8); ss.connect((ip,port)); ss.send(f'READ {FILE}\n'.encode()); content=ss.recv(16384).decode(); ss.close(); nm.close()
except Exception as e:
    print('FINAL_READ_FAIL', e); content=''

print('SUMMARY write_success', write_success, 'lock_fail', lock_fail, 'read_success', read_success, 'read_errors', read_errors, 'unique_sentences', len(sentences_written))
print('FINAL_CONTENT_START')
print(content.strip()[:800])
print('FINAL_CONTENT_END')
# Basic integrity check: at least half of writers succeeded and no total read failure
if write_success < max(1, ${NUM_WRITERS}//3):
    print('FAIL: too few write successes')
    exit(1)
if read_success == 0:
    print('FAIL: no successful reads')
    exit(1)
print('PASS')
EOF
STATUS=$?
if [ $STATUS -eq 0 ]; then echo -e "${GREEN}Concurrent RW test PASS${NC}"; else echo -e "${RED}Concurrent RW test FAIL${NC}"; fi
exit $STATUS
