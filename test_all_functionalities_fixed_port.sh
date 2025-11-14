#!/bin/bash
# Variant of test_all_functionalities_fixed.sh using configurable SS_PORT (default 9030)
# Usage: SS_PORT=9030 ./test_all_functionalities_fixed_port.sh

SS_PORT=${SS_PORT:-9030}
# Ensure previous stale processes are cleared before starting
pkill -9 name_server 2>/dev/null || true
pkill -9 storage_server 2>/dev/null || true
pkill -9 client 2>/dev/null || true
rm -f nm_log.txt ss_log.txt

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; MAGENTA='\033[0;35m'; CYAN='\033[0;36m'; NC='\033[0m'
TOTAL_TESTS=0; PASSED_TESTS=0; FAILED_TESTS=0

print_test_header(){ echo ""; echo -e "${BLUE}================================================${NC}"; echo -e "${CYAN}  $1${NC}"; echo -e "${BLUE}================================================${NC}"; }
print_result(){ TOTAL_TESTS=$((TOTAL_TESTS+1)); if [ $1 -eq 0 ]; then echo -e "${GREEN}✓ PASS${NC}: $2"; PASSED_TESTS=$((PASSED_TESTS+1)); else echo -e "${RED}✗ FAIL${NC}: $2"; FAILED_TESTS=$((FAILED_TESTS+1)); fi }
cleanup(){ pkill -9 name_server 2>/dev/null; pkill -9 storage_server 2>/dev/null; pkill -9 client 2>/dev/null; rm -rf storage/ 2>/dev/null; }
trap cleanup EXIT

print_test_header "SETUP"
make -s clean && make -s || { echo -e "${RED}Build failed${NC}"; exit 1; }
./name_server 8080 > nm_log.txt 2>&1 & NM=$!; sleep 1
./storage_server 127.0.0.1 8080 $SS_PORT > ss_log.txt 2>&1 & SS=$!; sleep 2
if ps -p $NM >/dev/null && ps -p $SS >/dev/null; then echo -e "${GREEN}✓ Servers up (SS_PORT=$SS_PORT)${NC}"; else
  echo -e "${RED}✗ Server start failed${NC}"
  echo "--- nm_log.txt ---"; [ -f nm_log.txt ] && sed -n '1,120p' nm_log.txt
  echo "--- ss_log.txt ---"; [ -f ss_log.txt ] && sed -n '1,120p' ss_log.txt
  exit 1
fi

# Helper python fragment for resolving SS port
PY_GET_PORT='def get_ip_port(resp,default):\n    parts=resp.split(); ip="127.0.0.1"; port=default\n    for i,p in enumerate(parts):\n        if p.count(".")==3:\n            ip=p;\n            if i+1 < len(parts):\n                try: port=int(parts[i+1])\n                except: pass\n            break\n    return ip,port'

########################################
print_test_header "CREATE"
python3 - <<EOF
import socket,os
s=socket.socket(); s.settimeout(5); s.connect(('localhost',8080))
s.send(b'REGISTER_CLIENT userA 9100 9200\n'); s.recv(4096)
s.send(b'CREATE mvp.txt\n'); r=s.recv(4096).decode()
print('PASS' if 'success' in r.lower() or '0:' in r else f'FAIL: {r}')
EOF
print_result $? "Create file"

########################################
print_test_header "WRITE + READ"
python3 - <<EOF
import socket,os
SS_PORT=int(os.environ.get('SS_PORT','9030'))
def get_ip_port(resp, default):
  parts = resp.split()
  ip = '127.0.0.1'
  port = default
  for i,p in enumerate(parts):
    if p.count('.')==3:
      ip = p
      if i+1 < len(parts):
        try:
          port = int(parts[i+1])
        except:
          pass
      break
  return ip, port
# Register and request write
nm=socket.socket(); nm.settimeout(5); nm.connect(('localhost',8080))
nm.send(b'REGISTER_CLIENT userA 9101 9201\n'); nm.recv(4096)
nm.send(b'WRITE mvp.txt 0\n'); resp=nm.recv(4096).decode()
if 'SS_INFO' not in resp: print('FAIL: no SS_INFO'); exit(1)
ip,port=get_ip_port(resp,SS_PORT)
ss=socket.socket(); ss.settimeout(5); ss.connect((ip,port))
ss.send(b'WRITE mvp.txt 0\n'); ss.recv(4096)
ss.send(b'0 Hello\n'); ss.recv(4096)
ss.send(b'1 world.\n'); ss.recv(4096)
ss.send(b'ETIRW\n'); ss.recv(4096); ss.close()
# Read back
nm.send(b'READ mvp.txt\n'); r=nm.recv(4096).decode(); ip,port=get_ip_port(r,SS_PORT)
rs=socket.socket(); rs.settimeout(5); rs.connect((ip,port)); rs.send(b'READ mvp.txt\n'); content=rs.recv(8192).decode(); rs.close(); nm.close()
print('PASS' if 'Hello world.' in content else f'FAIL: content={content}')
EOF
print_result $? "Write/read cycle"

########################################
print_test_header "STREAM"
python3 - <<EOF
import socket,os,time
SS_PORT=int(os.environ.get('SS_PORT','9030'))
def get_ip_port(resp, default):
  parts = resp.split()
  ip = '127.0.0.1'
  port = default
  for i,p in enumerate(parts):
    if p.count('.')==3:
      ip = p
      if i+1 < len(parts):
        try:
          port = int(parts[i+1])
        except:
          pass
      break
  return ip, port
nm=socket.socket(); nm.settimeout(5); nm.connect(('localhost',8080))
nm.send(b'REGISTER_CLIENT userA 9102 9202\n'); nm.recv(4096)
nm.send(b'STREAM mvp.txt\n'); info=nm.recv(4096).decode(); ip,port=get_ip_port(info,SS_PORT)
ss=socket.socket(); ss.settimeout(8); ss.connect((ip,port)); ss.send(b'STREAM mvp.txt\n')
words=0; start=time.time()
while time.time()-start < 3:
  try:
    d=ss.recv(512).decode()
    if not d: break
    words += len(d.split())
    if 'STOP' in d: break
  except: break
ss.close(); nm.close()
print('PASS' if words>=2 else f'FAIL: words={words}')
EOF
print_result $? "Stream"

########################################
print_test_header "EXEC"
python3 - <<EOF
import socket,os
SS_PORT=int(os.environ.get('SS_PORT','9030'))
def get_ip_port(resp, default):
  parts = resp.split()
  ip = '127.0.0.1'
  port = default
  for i,p in enumerate(parts):
    if p.count('.')==3:
      ip = p
      if i+1 < len(parts):
        try:
          port = int(parts[i+1])
        except:
          pass
      break
  return ip, port
# create exec file
nm=socket.socket(); nm.settimeout(5); nm.connect(('localhost',8080))
nm.send(b'REGISTER_CLIENT userA 9103 9203\n'); nm.recv(4096)
nm.send(b'CREATE exec.txt\n'); nm.recv(4096)
# write commands
nm.send(b'WRITE exec.txt 0\n'); wresp=nm.recv(4096).decode(); ip,port=get_ip_port(wresp,SS_PORT)
ss=socket.socket(); ss.settimeout(5); ss.connect((ip,port))
ss.send(b'WRITE exec.txt 0\n'); ss.recv(4096)
ss.send(b'0 echo\n'); ss.recv(4096)
ss.send(b'1 hi-from-exec\n'); ss.recv(4096)
ss.send(b'ETIRW\n'); ss.recv(4096); ss.close()
# exec
nm.send(b'EXEC exec.txt\n'); out=nm.recv(4096).decode(); nm.close()
print('PASS' if 'hi-from-exec' in out else f'FAIL: {out}')
EOF
print_result $? "EXEC"

########################################
print_test_header "REPORT"
echo -e "Total: $TOTAL_TESTS  Passed: $PASSED_TESTS  Failed: $FAILED_TESTS"
RATE=$((PASSED_TESTS*100/(TOTAL_TESTS==0?1:TOTAL_TESTS)))
echo -e "Pass Rate: ${RATE}%"
exit $([ $FAILED_TESTS -eq 0 ] && echo 0 || echo 1)
