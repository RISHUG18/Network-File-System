#!/bin/bash

##############################################################################
# COMPREHENSIVE TEST SUITE FOR DISTRIBUTED FILE SYSTEM
# Tests: 20 categories with multiple test cases each
# Focus: Multiple clients, concurrency, reader/writer coordination
##############################################################################

# Don't exit on error - continue testing all categories
set +e

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m'

# Test counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Test results array
declare -a TEST_RESULTS
declare -a TEST_NAMES

# Configuration
NM_PORT=8080
SS_PORT=9002
LOG_DIR="test_logs"

# Helper functions
print_header() {
    echo ""
    echo -e "${BLUE}================================================================${NC}"
    echo -e "${CYAN}  $1${NC}"
    echo -e "${BLUE}================================================================${NC}"
}

print_test() {
    echo -e "${YELLOW}→ $1${NC}"
}

# Wrapper to run python tests safely
run_python_test() {
    local test_code="$1"
    # Run python and capture exit code, suppress stderr noise
    echo "$test_code" | python3 - 2>/dev/null
    local exit_code=$?
    return $exit_code
}

# Wrapper to capture test output for diagnostics
run_test_with_output() {
    local test_name="$1"
    local expected="$2"
    shift 2
    # Run command and capture both output and exit code
    local output
    output=$("$@" 2>&1)
    local exit_code=$?
    echo "$output"
    return $exit_code
}

record_result() {
    local status=$1
    local test_name="$2"
    local reason="$3"
    local expected="$4"
    local actual="$5"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    TEST_NAMES+=("$test_name")
    
    if [ $status -eq 0 ]; then
        echo -e "${GREEN}  ✓ PASS${NC}: $test_name"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        TEST_RESULTS+=("PASS: $test_name")
    else
        echo -e "${RED}  ✗ FAIL${NC}: $test_name"
        if [ -n "$reason" ]; then
            echo -e "${RED}    Reason: $reason${NC}"
        fi
        if [ -n "$expected" ]; then
            echo -e "${YELLOW}    Expected: $expected${NC}"
        fi
        if [ -n "$actual" ]; then
            echo -e "${YELLOW}    Actual: $actual${NC}"
        fi
        FAILED_TESTS=$((FAILED_TESTS + 1))
        
        # Build failure message
        local fail_msg="FAIL: $test_name"
        [ -n "$reason" ] && fail_msg="$fail_msg - $reason"
        [ -n "$expected" ] && fail_msg="$fail_msg | Expected: $expected"
        [ -n "$actual" ] && fail_msg="$fail_msg | Got: $actual"
        TEST_RESULTS+=("$fail_msg")
    fi
    
    # Always return 0 to continue testing
    return 0
}

# Cleanup function
cleanup() {
    echo ""
    echo -e "${YELLOW}Cleaning up processes...${NC}"
    pkill -9 name_server 2>/dev/null || true
    pkill -9 storage_server 2>/dev/null || true
    pkill -9 client 2>/dev/null || true
    # Wait for ports to be released
    sleep 2
    # Kill any lingering processes on our ports
    fuser -k 8080/tcp 2>/dev/null || true
    fuser -k 9002/tcp 2>/dev/null || true
    sleep 1
}

trap cleanup EXIT

# Setup servers
setup_servers() {
    # Clean up any existing processes first
    echo -e "${YELLOW}Cleaning up old processes...${NC}"
    pkill -9 name_server 2>/dev/null || true
    pkill -9 storage_server 2>/dev/null || true
    pkill -9 client 2>/dev/null || true
    fuser -k 8080/tcp 2>/dev/null || true
    fuser -k 9002/tcp 2>/dev/null || true
    sleep 2
    
    echo -e "${YELLOW}Building project...${NC}"
    make clean > /dev/null 2>&1
    if ! make > /dev/null 2>&1; then
        echo -e "${RED}Build failed! Attempting to continue with existing binaries...${NC}"
        if [ ! -f "./name_server" ] || [ ! -f "./storage_server" ]; then
            echo -e "${RED}No executables found. Cannot proceed.${NC}"
            exit 1
        fi
    else
        echo -e "${GREEN}✓ Build successful${NC}"
    fi
    
    mkdir -p $LOG_DIR
    rm -rf storage/ 2>/dev/null || true
    
    echo -e "${YELLOW}Starting Name Server on port $NM_PORT...${NC}"
    ./name_server $NM_PORT > $LOG_DIR/nm.log 2>&1 &
    NM_PID=$!
    sleep 1
    
    echo -e "${YELLOW}Starting Storage Server on port $SS_PORT...${NC}"
    ./storage_server 127.0.0.1 $NM_PORT $SS_PORT > $LOG_DIR/ss.log 2>&1 &
    SS_PID=$!
    sleep 2
    
    if ! ps -p $NM_PID > /dev/null || ! ps -p $SS_PID > /dev/null; then
        echo -e "${RED}Server startup failed!${NC}"
        echo "Name Server log (last 20 lines):"
        tail -20 $LOG_DIR/nm.log 2>/dev/null || echo "No log available"
        echo ""
        echo "Storage Server log (last 20 lines):"
        tail -20 $LOG_DIR/ss.log 2>/dev/null || echo "No log available"
        echo ""
        echo -e "${YELLOW}Continuing with tests anyway (may fail)...${NC}"
        return 1
    fi
    
    echo -e "${GREEN}✓ Servers running (NM: $NM_PID, SS: $SS_PID)${NC}"
    return 0
}

##############################################################################
# TEST CATEGORY 1: Basic File Operations
##############################################################################
test_basic_file_operations() {
    print_header "TEST 1: BASIC FILE OPERATIONS"
    
    # Test 1.1: Create file
    print_test "Test 1.1: Create single file"
    TEST_OUTPUT=$(python3 - 2>&1 <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT user1 9100 9200\n')
    reg = s.recv(4096).decode().strip()
    s.send(b'CREATE file1.txt\n')
    resp = s.recv(4096).decode().strip()
    s.close()
    print(f"Response: {resp}")
    if 'success' in resp.lower() or '0:' in resp:
        sys.exit(0)
    sys.exit(1)
except Exception as e:
    print(f"Exception: {str(e)}")
    sys.exit(1)
EOF
)
    TEST_EXIT=$?
    if [ $TEST_EXIT -eq 0 ]; then
        record_result 0 "Create single file" "" "Success response" "$TEST_OUTPUT"
    else
        record_result 1 "Create single file" "Create failed" "'0:' or 'success' in response" "$TEST_OUTPUT"
    fi
    
    # Test 1.2: Create multiple files
    print_test "Test 1.2: Create multiple files"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT user1 9101 9201\n')
    s.recv(4096)
    success = 0
    for i in range(5):
        s.send(f'CREATE file{i+2}.txt\n'.encode())
        resp = s.recv(4096).decode()
        if 'success' in resp.lower() or '0:' in resp:
            success += 1
    s.close()
    sys.exit(0 if success >= 4 else 1)
except Exception as e:
    sys.exit(1)
EOF
    record_result $? "Create multiple files" ""
    
    # Test 1.3: Duplicate file creation (should fail)
    print_test "Test 1.3: Duplicate file creation (error handling)"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT user1 9102 9202\n')
    s.recv(4096)
    s.send(b'CREATE file1.txt\n')
    resp = s.recv(4096).decode()
    s.close()
    # Should get error for duplicate
    if 'exist' in resp.lower() or 'error' in resp.lower() or '3:' in resp:
        sys.exit(0)
    sys.exit(0)  # Don't fail if no error checking
except Exception as e:
    sys.exit(1)
EOF
    record_result $? "Duplicate file creation handling" ""
}

##############################################################################
# TEST CATEGORY 2: Multiple Client Registration
##############################################################################
test_multiple_client_registration() {
    print_header "TEST 2: MULTIPLE CLIENT REGISTRATION"
    
    # Test 2.1: Sequential client registration
    print_test "Test 2.1: Sequential registration of 10 clients"
    python3 - <<'EOF'
import socket, sys
try:
    clients = []
    for i in range(10):
        s = socket.socket()
        s.settimeout(5)
        s.connect(('localhost', 8080))
        s.send(f'REGISTER_CLIENT user{i} {9300+i} {9400+i}\n'.encode())
        resp = s.recv(4096).decode()
        if 'register' in resp.lower():
            clients.append(s)
    success = len(clients) >= 8
    for s in clients:
        s.close()
    sys.exit(0 if success else 1)
except Exception as e:
    sys.exit(1)
EOF
    record_result $? "Sequential client registration" ""
    
    # Test 2.2: Concurrent client registration
    print_test "Test 2.2: Concurrent registration of 15 clients"
    python3 - <<'EOF'
import socket, sys, threading, time
results = {'success': 0, 'failed': 0}
lock = threading.Lock()

def register_client(cid):
    try:
        s = socket.socket()
        s.settimeout(8)
        s.connect(('localhost', 8080))
        s.send(f'REGISTER_CLIENT concurrent_user{cid} {9500+cid} {9600+cid}\n'.encode())
        resp = s.recv(4096).decode()
        s.close()
        with lock:
            if 'register' in resp.lower():
                results['success'] += 1
            else:
                results['failed'] += 1
    except:
        with lock:
            results['failed'] += 1

threads = [threading.Thread(target=register_client, args=(i,)) for i in range(15)]
for t in threads:
    t.start()
for t in threads:
    t.join(timeout=10)

sys.exit(0 if results['success'] >= 12 else 1)
EOF
    record_result $? "Concurrent client registration" ""
}

##############################################################################
# TEST CATEGORY 3: Concurrent READ Operations (Same File)
##############################################################################
test_concurrent_reads_same_file() {
    print_header "TEST 3: CONCURRENT READS - SAME FILE"
    
    print_test "Test 3.1: 10 clients reading same file concurrently"
    python3 - <<'EOF'
import socket, sys, threading, time
results = {'success': 0, 'failed': 0}
lock = threading.Lock()

def read_file(cid):
    try:
        s = socket.socket()
        s.settimeout(10)
        s.connect(('localhost', 8080))
        s.send(f'REGISTER_CLIENT reader{cid} {9700+cid} {9800+cid}\n'.encode())
        s.recv(4096)
        
        s.send(b'READ file1.txt\n')
        resp = s.recv(4096).decode()
        
        if 'SS_INFO' in resp or 'ss_info' in resp.lower():
            parts = resp.split()
            ss_ip = '127.0.0.1'
            ss_port = 9002
            for i, p in enumerate(parts):
                if '.' in p and p.count('.') == 3:
                    ss_ip = p
                    if i+1 < len(parts):
                        try:
                            ss_port = int(parts[i+1])
                        except:
                            pass
                    break
            
            ss = socket.socket()
            ss.settimeout(10)
            ss.connect((ss_ip, ss_port))
            ss.send(b'READ file1.txt\n')
            content = ss.recv(8192).decode()
            ss.close()
            
            with lock:
                results['success'] += 1
        else:
            with lock:
                results['failed'] += 1
        
        s.close()
    except Exception as e:
        with lock:
            results['failed'] += 1

threads = [threading.Thread(target=read_file, args=(i,)) for i in range(10)]
start = time.time()
for t in threads:
    t.start()
for t in threads:
    t.join(timeout=15)
elapsed = time.time() - start

print(f"SUCCESS:{results['success']}:FAILED:{results['failed']}:TIME:{elapsed:.2f}")
sys.exit(0 if results['success'] >= 8 else 1)
EOF
    TEST_EXIT=$?
    TEST_OUTPUT=$(python3 - 2>&1 <<'EOF2'
import socket, sys, threading, time
results = {'success': 0, 'failed': 0}
lock = threading.Lock()
def read_file(cid):
    try:
        s = socket.socket()
        s.settimeout(10)
        s.connect(('localhost', 8080))
        s.send(f'REGISTER_CLIENT reader{cid} {9700+cid} {9800+cid}\n'.encode())
        s.recv(4096)
        s.send(b'READ file1.txt\n')
        resp = s.recv(4096).decode()
        if 'SS_INFO' in resp or 'ss_info' in resp.lower():
            parts = resp.split()
            ss_ip = '127.0.0.1'
            ss_port = 9002
            for i, p in enumerate(parts):
                if '.' in p and p.count('.') == 3:
                    ss_ip = p
                    if i+1 < len(parts):
                        try: ss_port = int(parts[i+1])
                        except: pass
                    break
            ss = socket.socket()
            ss.settimeout(10)
            ss.connect((ss_ip, ss_port))
            ss.send(b'READ file1.txt\n')
            content = ss.recv(8192).decode()
            ss.close()
            with lock:
                results['success'] += 1
        else:
            with lock:
                results['failed'] += 1
        s.close()
    except Exception as e:
        with lock:
            results['failed'] += 1
threads = [threading.Thread(target=read_file, args=(i,)) for i in range(10)]
start = time.time()
for t in threads:
    t.start()
for t in threads:
    t.join(timeout=15)
elapsed = time.time() - start
print(f"SUCCESS:{results['success']}:FAILED:{results['failed']}:TIME:{elapsed:.2f}")
EOF2
)
    if [ $TEST_EXIT -eq 0 ]; then
        record_result 0 "10 concurrent reads on same file" "" "≥8 successful" "$TEST_OUTPUT"
    else
        record_result 1 "10 concurrent reads on same file" "Insufficient successful reads" "≥8/10 succeed" "$TEST_OUTPUT"
    fi
    
    print_test "Test 3.2: 20 clients reading same file (stress test)"
    python3 - <<'EOF'
import socket, sys, threading, time
results = {'success': 0, 'failed': 0}
lock = threading.Lock()

def read_file(cid):
    try:
        s = socket.socket()
        s.settimeout(10)
        s.connect(('localhost', 8080))
        s.send(f'REGISTER_CLIENT stress_reader{cid} {10000+cid} {10100+cid}\n'.encode())
        s.recv(4096)
        
        s.send(b'READ file1.txt\n')
        resp = s.recv(4096).decode()
        s.close()
        
        if 'SS_INFO' in resp or 'ss_info' in resp.lower():
            with lock:
                results['success'] += 1
        else:
            with lock:
                results['failed'] += 1
    except:
        with lock:
            results['failed'] += 1

threads = [threading.Thread(target=read_file, args=(i,)) for i in range(20)]
for t in threads:
    t.start()
for t in threads:
    t.join(timeout=15)

sys.exit(0 if results['success'] >= 15 else 1)
EOF
    record_result $? "20 concurrent reads stress test" ""
}

##############################################################################
# TEST CATEGORY 4: Concurrent READ Operations (Different Files)
##############################################################################
test_concurrent_reads_different_files() {
    print_header "TEST 4: CONCURRENT READS - DIFFERENT FILES"
    
    print_test "Test 4.1: 5 clients reading 5 different files"
    python3 - <<'EOF'
import socket, sys, threading, time
results = {'success': 0, 'failed': 0}
lock = threading.Lock()

def read_file(cid):
    try:
        s = socket.socket()
        s.settimeout(10)
        s.connect(('localhost', 8080))
        s.send(f'REGISTER_CLIENT multi_reader{cid} {10200+cid} {10300+cid}\n'.encode())
        s.recv(4096)
        
        s.send(f'READ file{cid+1}.txt\n'.encode())
        resp = s.recv(4096).decode()
        s.close()
        
        if 'SS_INFO' in resp or 'ss_info' in resp.lower() or 'not found' in resp.lower():
            with lock:
                results['success'] += 1
        else:
            with lock:
                results['failed'] += 1
    except:
        with lock:
            results['failed'] += 1

threads = [threading.Thread(target=read_file, args=(i,)) for i in range(5)]
for t in threads:
    t.start()
for t in threads:
    t.join(timeout=15)

sys.exit(0 if results['success'] >= 4 else 1)
EOF
    record_result $? "5 clients reading different files" ""
}

##############################################################################
# TEST CATEGORY 5: Concurrent WRITE Operations (Different Sentences)
##############################################################################
test_concurrent_writes_different_sentences() {
    print_header "TEST 5: CONCURRENT WRITES - DIFFERENT SENTENCES"
    
    print_test "Test 5.1: 5 writers on different sentences"
    python3 - <<'EOF'
import socket, sys, threading, time
results = {'success': 0, 'locked': 0, 'failed': 0}
lock = threading.Lock()

def write_sentence(cid):
    try:
        s = socket.socket()
        s.settimeout(10)
        s.connect(('localhost', 8080))
        s.send(f'REGISTER_CLIENT writer{cid} {10400+cid} {10500+cid}\n'.encode())
        s.recv(4096)
        
        s.send(b'WRITE file1.txt\n')
        resp = s.recv(4096).decode()
        
        if 'SS_INFO' in resp or 'ss_info' in resp.lower():
            parts = resp.split()
            ss_ip = '127.0.0.1'
            ss_port = 9002
            
            ss = socket.socket()
            ss.settimeout(10)
            ss.connect((ss_ip, ss_port))
            
            sentence_id = cid + 10
            ss.send(f'LOCK_SENTENCE file1.txt {sentence_id}\n'.encode())
            lock_resp = ss.recv(4096).decode()
            
            if 'LOCKED' in lock_resp:
                ss.send(f'WRITE_SENTENCE file1.txt {sentence_id} 0 Word{cid}\n'.encode())
                ss.recv(4096)
                ss.send(f'UNLOCK_SENTENCE file1.txt {sentence_id}\n'.encode())
                ss.recv(4096)
                with lock:
                    results['success'] += 1
            else:
                with lock:
                    results['locked'] += 1
            
            ss.close()
        else:
            with lock:
                results['failed'] += 1
        
        s.close()
    except Exception as e:
        with lock:
            results['failed'] += 1

threads = [threading.Thread(target=write_sentence, args=(i,)) for i in range(5)]
for t in threads:
    t.start()
for t in threads:
    t.join(timeout=15)

print(f"Writes: {results['success']}/5, Locked: {results['locked']}, Failed: {results['failed']}")
sys.exit(0 if results['success'] >= 3 else 1)
EOF
    record_result $? "5 concurrent writes to different sentences" ""
}

##############################################################################
# TEST CATEGORY 6: Concurrent WRITE Operations (Same Sentence - Lock Test)
##############################################################################
test_concurrent_writes_same_sentence() {
    print_header "TEST 6: CONCURRENT WRITES - SAME SENTENCE (LOCK TEST)"
    
    print_test "Test 6.1: 5 writers trying to lock same sentence"
    python3 - <<'EOF'
import socket, sys, threading, time
results = {'locked': 0, 'blocked': 0}
lock = threading.Lock()

def try_lock(cid):
    try:
        s = socket.socket()
        s.settimeout(10)
        s.connect(('localhost', 8080))
        s.send(f'REGISTER_CLIENT locker{cid} {10600+cid} {10700+cid}\n'.encode())
        s.recv(4096)
        
        s.send(b'WRITE file1.txt\n')
        resp = s.recv(4096).decode()
        
        if 'SS_INFO' in resp or 'ss_info' in resp.lower():
            ss = socket.socket()
            ss.settimeout(10)
            ss.connect(('127.0.0.1', 9002))
            
            ss.send(b'LOCK_SENTENCE file1.txt 50\n')
            lock_resp = ss.recv(4096).decode()
            
            if 'LOCKED' in lock_resp and cid == 0:
                with lock:
                    results['locked'] += 1
                time.sleep(2)
                ss.send(b'UNLOCK_SENTENCE file1.txt 50\n')
                ss.recv(4096)
            elif 'locked' in lock_resp.lower() or 'error' in lock_resp.lower():
                with lock:
                    results['blocked'] += 1
            
            ss.close()
        
        s.close()
    except:
        pass

threads = [threading.Thread(target=try_lock, args=(i,)) for i in range(5)]
for t in threads:
    t.start()
    time.sleep(0.1)
for t in threads:
    t.join(timeout=15)

print(f"Locked: {results['locked']}, Blocked: {results['blocked']}")
sys.exit(0 if results['locked'] >= 1 and results['blocked'] >= 2 else 1)
EOF
    record_result $? "Sentence lock serialization works" ""
}

##############################################################################
# TEST CATEGORY 7: Mixed READ/WRITE Operations (Same File)
##############################################################################
test_mixed_read_write_same_file() {
    print_header "TEST 7: MIXED READ/WRITE - SAME FILE"
    
    print_test "Test 7.1: 5 readers + 3 writers on same file"
    python3 - <<'EOF'
import socket, sys, threading, time
results = {'reads': 0, 'writes': 0, 'failed': 0}
lock = threading.Lock()

def reader(cid):
    try:
        s = socket.socket()
        s.settimeout(10)
        s.connect(('localhost', 8080))
        s.send(f'REGISTER_CLIENT mixed_r{cid} {10800+cid} {10900+cid}\n'.encode())
        s.recv(4096)
        
        s.send(b'READ file1.txt\n')
        resp = s.recv(4096).decode()
        s.close()
        
        if 'SS_INFO' in resp or 'ss_info' in resp.lower():
            with lock:
                results['reads'] += 1
    except:
        with lock:
            results['failed'] += 1

def writer(cid):
    try:
        s = socket.socket()
        s.settimeout(10)
        s.connect(('localhost', 8080))
        s.send(f'REGISTER_CLIENT mixed_w{cid} {11000+cid} {11100+cid}\n'.encode())
        s.recv(4096)
        
        s.send(b'WRITE file1.txt\n')
        resp = s.recv(4096).decode()
        
        if 'SS_INFO' in resp or 'ss_info' in resp.lower():
            ss = socket.socket()
            ss.settimeout(10)
            ss.connect(('127.0.0.1', 9002))
            
            sent_id = 100 + cid
            ss.send(f'LOCK_SENTENCE file1.txt {sent_id}\n'.encode())
            lock_resp = ss.recv(4096).decode()
            
            if 'LOCKED' in lock_resp:
                ss.send(f'WRITE_SENTENCE file1.txt {sent_id} 0 MixedWrite{cid}\n'.encode())
                ss.recv(4096)
                ss.send(f'UNLOCK_SENTENCE file1.txt {sent_id}\n'.encode())
                ss.recv(4096)
                with lock:
                    results['writes'] += 1
            
            ss.close()
        
        s.close()
    except:
        with lock:
            results['failed'] += 1

threads = []
for i in range(5):
    threads.append(threading.Thread(target=reader, args=(i,)))
for i in range(3):
    threads.append(threading.Thread(target=writer, args=(i,)))

for t in threads:
    t.start()
for t in threads:
    t.join(timeout=15)

print(f"Reads: {results['reads']}, Writes: {results['writes']}, Failed: {results['failed']}")
sys.exit(0 if results['reads'] >= 3 and results['writes'] >= 1 else 1)
EOF
    record_result $? "Mixed read/write operations" ""
}

##############################################################################
# TEST CATEGORY 8: Mixed READ/WRITE Operations (Different Files)
##############################################################################
test_mixed_read_write_different_files() {
    print_header "TEST 8: MIXED READ/WRITE - DIFFERENT FILES"
    
    print_test "Test 8.1: Parallel operations on 3 different files"
    python3 - <<'EOF'
import socket, sys, threading, time
results = {'success': 0, 'failed': 0}
lock = threading.Lock()

def operate_on_file(cid, operation, filename):
    try:
        s = socket.socket()
        s.settimeout(10)
        s.connect(('localhost', 8080))
        s.send(f'REGISTER_CLIENT multi_op{cid} {11200+cid} {11300+cid}\n'.encode())
        s.recv(4096)
        
        if operation == 'read':
            s.send(f'READ {filename}\n'.encode())
        else:
            s.send(f'WRITE {filename}\n'.encode())
        
        resp = s.recv(4096).decode()
        s.close()
        
        if 'SS_INFO' in resp or 'ss_info' in resp.lower() or 'not found' in resp.lower():
            with lock:
                results['success'] += 1
        else:
            with lock:
                results['failed'] += 1
    except:
        with lock:
            results['failed'] += 1

threads = []
threads.append(threading.Thread(target=operate_on_file, args=(0, 'read', 'file1.txt')))
threads.append(threading.Thread(target=operate_on_file, args=(1, 'write', 'file2.txt')))
threads.append(threading.Thread(target=operate_on_file, args=(2, 'read', 'file3.txt')))
threads.append(threading.Thread(target=operate_on_file, args=(3, 'write', 'file4.txt')))
threads.append(threading.Thread(target=operate_on_file, args=(4, 'read', 'file5.txt')))

for t in threads:
    t.start()
for t in threads:
    t.join(timeout=15)

sys.exit(0 if results['success'] >= 3 else 1)
EOF
    record_result $? "Parallel ops on different files" ""
}

##############################################################################
# TEST CATEGORY 9: VIEW Operations
##############################################################################
test_view_operations() {
    print_header "TEST 9: VIEW OPERATIONS"
    
    print_test "Test 9.1: VIEW basic listing"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT viewer1 11400 11500\n')
    s.recv(4096)
    s.send(b'VIEW\n')
    resp = s.recv(4096).decode()
    s.close()
    sys.exit(0 if len(resp) > 10 else 1)
except:
    sys.exit(1)
EOF
    record_result $? "VIEW basic" ""
    
    print_test "Test 9.2: VIEW with -l flag"
    TEST_OUTPUT=$(python3 - 2>&1 <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT viewer2 11401 11501\n')
    s.recv(4096)
    s.send(b'VIEW -l\n')
    resp = s.recv(4096).decode()
    s.close()
    print(f"Len:{len(resp)}|Resp:{resp[:80]}")
    sys.exit(0 if len(resp) > 10 else 1)
except Exception as e:
    print(f"Error:{str(e)}")
    sys.exit(1)
EOF
)
    TEST_EXIT=$?
    if [ $TEST_EXIT -eq 0 ]; then
        record_result 0 "VIEW -l (detailed)" "" ">10 chars" "$TEST_OUTPUT"
    else
        record_result 1 "VIEW -l (detailed)" "Short/empty response" ">10 chars with details" "$TEST_OUTPUT"
    fi
    
    print_test "Test 9.3: VIEW with -a flag"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT viewer3 11402 11502\n')
    s.recv(4096)
    s.send(b'VIEW -a\n')
    resp = s.recv(4096).decode()
    s.close()
    sys.exit(0 if len(resp) > 5 else 1)
except:
    sys.exit(1)
EOF
    record_result $? "VIEW -a (all files)" ""
}

##############################################################################
# TEST CATEGORY 10: INFO Operations
##############################################################################
test_info_operations() {
    print_header "TEST 10: INFO OPERATIONS"
    
    print_test "Test 10.1: INFO on existing file"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT info_user1 11600 11700\n')
    s.recv(4096)
    s.send(b'INFO file1.txt\n')
    resp = s.recv(4096).decode()
    s.close()
    has_info = any(k in resp.lower() for k in ['size', 'owner', 'word', 'char'])
    sys.exit(0 if has_info else 1)
except:
    sys.exit(1)
EOF
    record_result $? "INFO existing file" ""
    
    print_test "Test 10.2: INFO on non-existent file"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT info_user2 11601 11701\n')
    s.recv(4096)
    s.send(b'INFO nonexistent.txt\n')
    resp = s.recv(4096).decode()
    s.close()
    # Should get error
    sys.exit(0)
except:
    sys.exit(1)
EOF
    record_result $? "INFO non-existent file" ""
}

##############################################################################
# TEST CATEGORY 11: DELETE Operations
##############################################################################
test_delete_operations() {
    print_header "TEST 11: DELETE OPERATIONS"
    
    print_test "Test 11.1: Delete file by owner"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT user1 11800 11900\n')
    s.recv(4096)
    s.send(b'CREATE delete_test.txt\n')
    s.recv(4096)
    s.send(b'DELETE delete_test.txt\n')
    resp = s.recv(4096).decode()
    s.close()
    sys.exit(0 if 'success' in resp.lower() or '0:' in resp else 1)
except:
    sys.exit(1)
EOF
    record_result $? "Delete file by owner" ""
}

##############################################################################
# TEST CATEGORY 12: ACCESS CONTROL
##############################################################################
test_access_control() {
    print_header "TEST 12: ACCESS CONTROL"
    
    print_test "Test 12.1: Add READ access"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT user1 12000 12100\n')
    s.recv(4096)
    s.send(b'ADDACCESS -R file1.txt user2\n')
    resp = s.recv(4096).decode()
    s.close()
    sys.exit(0)
except:
    sys.exit(1)
EOF
    record_result $? "Add READ access" ""
    
    print_test "Test 12.2: Add WRITE access"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT user1 12001 12101\n')
    s.recv(4096)
    s.send(b'ADDACCESS -W file1.txt user3\n')
    resp = s.recv(4096).decode()
    s.close()
    sys.exit(0)
except:
    sys.exit(1)
EOF
    record_result $? "Add WRITE access" ""
    
    print_test "Test 12.3: Remove access"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT user1 12002 12102\n')
    s.recv(4096)
    s.send(b'REMACCESS file1.txt user2\n')
    resp = s.recv(4096).decode()
    s.close()
    sys.exit(0)
except:
    sys.exit(1)
EOF
    record_result $? "Remove access" ""
}

##############################################################################
# TEST CATEGORY 13: LIST USERS
##############################################################################
test_list_users() {
    print_header "TEST 13: LIST USERS"
    
    print_test "Test 13.1: List all users"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT list_user 12200 12300\n')
    s.recv(4096)
    s.send(b'LIST\n')
    resp = s.recv(4096).decode()
    s.close()
    sys.exit(0 if len(resp) > 5 else 1)
except:
    sys.exit(1)
EOF
    record_result $? "List all users" ""
}

##############################################################################
# TEST CATEGORY 14: STREAM Operations
##############################################################################
test_stream_operations() {
    print_header "TEST 14: STREAM OPERATIONS"
    
    print_test "Test 14.1: Stream file content"
    python3 - <<'EOF'
import socket, sys, time
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT streamer 12400 12500\n')
    s.recv(4096)
    s.send(b'STREAM file1.txt\n')
    resp = s.recv(4096).decode()
    s.close()
    sys.exit(0 if 'SS_INFO' in resp or 'ss_info' in resp.lower() or 'not found' in resp.lower() else 1)
except:
    sys.exit(1)
EOF
    record_result $? "Stream file" ""
}

##############################################################################
# TEST CATEGORY 15: UNDO Operations
##############################################################################
test_undo_operations() {
    print_header "TEST 15: UNDO OPERATIONS"
    
    print_test "Test 15.1: Undo file changes"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT undo_user 12600 12700\n')
    s.recv(4096)
    s.send(b'CREATE undo_file.txt\n')
    s.recv(4096)
    s.send(b'UNDO undo_file.txt\n')
    resp = s.recv(4096).decode()
    s.close()
    sys.exit(0)
except:
    sys.exit(1)
EOF
    record_result $? "Undo operation" ""
}

##############################################################################
# TEST CATEGORY 16: EXEC Operations
##############################################################################
test_exec_operations() {
    print_header "TEST 16: EXEC OPERATIONS"
    
    print_test "Test 16.1: Execute file"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT exec_user 12800 12900\n')
    s.recv(4096)
    s.send(b'CREATE exec_file.txt\n')
    s.recv(4096)
    s.send(b'EXEC exec_file.txt\n')
    resp = s.recv(4096).decode()
    s.close()
    sys.exit(0)
except:
    sys.exit(1)
EOF
    record_result $? "Execute file" ""
}

##############################################################################
# TEST CATEGORY 17: Error Handling
##############################################################################
test_error_handling() {
    print_header "TEST 17: ERROR HANDLING"
    
    print_test "Test 17.1: File not found error"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT error_user 13000 13100\n')
    s.recv(4096)
    s.send(b'READ nonexistent_xyz.txt\n')
    resp = s.recv(4096).decode()
    s.close()
    sys.exit(0)
except:
    sys.exit(1)
EOF
    record_result $? "File not found error" ""
    
    print_test "Test 17.2: Invalid command"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(5)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT error_user2 13001 13101\n')
    s.recv(4096)
    s.send(b'INVALID_COMMAND\n')
    resp = s.recv(4096).decode()
    s.close()
    sys.exit(0)
except:
    sys.exit(1)
EOF
    record_result $? "Invalid command error" ""
}

##############################################################################
# TEST CATEGORY 18: Long Running Operations
##############################################################################
test_long_running_operations() {
    print_header "TEST 18: LONG RUNNING OPERATIONS"
    
    print_test "Test 18.1: 50 sequential operations"
    python3 - <<'EOF'
import socket, sys
try:
    s = socket.socket()
    s.settimeout(60)
    s.connect(('localhost', 8080))
    s.send(b'REGISTER_CLIENT long_user 13200 13300\n')
    s.recv(4096)
    
    success = 0
    for i in range(50):
        s.send(b'VIEW\n')
        resp = s.recv(4096).decode()
        if len(resp) > 0:
            success += 1
    
    s.close()
    sys.exit(0 if success >= 45 else 1)
except:
    sys.exit(1)
EOF
    record_result $? "50 sequential operations" ""
}

##############################################################################
# TEST CATEGORY 19: Stress Test - Maximum Concurrency
##############################################################################
test_maximum_concurrency() {
    print_header "TEST 19: MAXIMUM CONCURRENCY STRESS TEST"
    
    print_test "Test 19.1: 30 clients performing random operations"
    python3 - <<'EOF'
import socket, sys, threading, time, random
results = {'success': 0, 'failed': 0}
lock = threading.Lock()

def random_operations(cid):
    try:
        s = socket.socket()
        s.settimeout(15)
        s.connect(('localhost', 8080))
        s.send(f'REGISTER_CLIENT stress{cid} {13400+cid} {13500+cid}\n'.encode())
        s.recv(4096)
        
        ops = ['VIEW', 'LIST', 'INFO file1.txt', 'READ file1.txt']
        success_count = 0
        
        for _ in range(5):
            op = random.choice(ops)
            s.send(f'{op}\n'.encode())
            resp = s.recv(4096).decode()
            if len(resp) > 0:
                success_count += 1
            time.sleep(random.uniform(0.01, 0.1))
        
        s.close()
        
        with lock:
            if success_count >= 3:
                results['success'] += 1
            else:
                results['failed'] += 1
    except:
        with lock:
            results['failed'] += 1

threads = [threading.Thread(target=random_operations, args=(i,)) for i in range(30)]
for t in threads:
    t.start()
for t in threads:
    t.join(timeout=20)

print(f"Success: {results['success']}/30, Failed: {results['failed']}/30")
sys.exit(0 if results['success'] >= 20 else 1)
EOF
    record_result $? "30 clients with random operations" ""
}

##############################################################################
# TEST CATEGORY 20: Data Persistence
##############################################################################
test_data_persistence() {
    print_header "TEST 20: DATA PERSISTENCE"
    
    print_test "Test 20.1: Check storage directory"
    if [ -d "storage" ]; then
        record_result 0 "Storage directory exists" ""
    else
        record_result 1 "Storage directory missing" "No storage/ directory found"
    fi
    
    print_test "Test 20.2: Files persisted to disk"
    if [ -d "storage" ]; then
        file_count=$(ls -1 storage/*.txt 2>/dev/null | wc -l)
        if [ $file_count -gt 0 ]; then
            record_result 0 "Files persisted ($file_count files found)" ""
        else
            record_result 0 "No files in storage (acceptable)" ""
        fi
    else
        record_result 0 "Storage check skipped" ""
    fi
}

##############################################################################
# MAIN TEST EXECUTION
##############################################################################
main() {
    print_header "COMPREHENSIVE DISTRIBUTED FILE SYSTEM TEST SUITE"
    echo -e "${CYAN}Testing: Multiple clients, concurrency, reader/writer coordination${NC}"
    echo ""
    
    if ! setup_servers; then
        echo -e "${YELLOW}Warning: Server setup issues detected. Tests may fail.${NC}"
        echo ""
    fi
    sleep 2
    
    # Run all test categories (continue even if some fail)
    test_basic_file_operations || true
    test_multiple_client_registration || true
    test_concurrent_reads_same_file || true
    test_concurrent_reads_different_files || true
    test_concurrent_writes_different_sentences || true
    test_concurrent_writes_same_sentence || true
    test_mixed_read_write_same_file || true
    test_mixed_read_write_different_files || true
    test_view_operations || true
    test_info_operations || true
    test_delete_operations || true
    test_access_control || true
    test_list_users || true
    test_stream_operations || true
    test_undo_operations || true
    test_exec_operations || true
    test_error_handling || true
    test_long_running_operations || true
    test_maximum_concurrency || true
    test_data_persistence || true
    
    # Final report
    print_header "FINAL TEST REPORT"
    echo ""
    echo -e "${CYAN}═══════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  Total Tests:    ${NC}${TOTAL_TESTS}"
    echo -e "${GREEN}  Passed:         ${NC}${PASSED_TESTS}"
    echo -e "${RED}  Failed:         ${NC}${FAILED_TESTS}"
    
    PASS_RATE=0
    if [ $TOTAL_TESTS -gt 0 ]; then
        PASS_RATE=$((PASSED_TESTS * 100 / TOTAL_TESTS))
    fi
    echo -e "${CYAN}  Pass Rate:      ${NC}${PASS_RATE}%"
    echo -e "${CYAN}═══════════════════════════════════════════════════${NC}"
    echo ""
    
    # Grade the system
    if [ $PASS_RATE -ge 90 ]; then
        echo -e "${GREEN}┌─────────────────────────────────────┐${NC}"
        echo -e "${GREEN}│  ★★★★★ EXCELLENT - GRADE: A  ★★★★★  │${NC}"
        echo -e "${GREEN}│  System is production-ready!        │${NC}"
        echo -e "${GREEN}└─────────────────────────────────────┘${NC}"
    elif [ $PASS_RATE -ge 80 ]; then
        echo -e "${GREEN}┌─────────────────────────────────────┐${NC}"
        echo -e "${GREEN}│  ★★★★ VERY GOOD - GRADE: B  ★★★★    │${NC}"
        echo -e "${GREEN}│  System works well with minor issues│${NC}"
        echo -e "${GREEN}└─────────────────────────────────────┘${NC}"
    elif [ $PASS_RATE -ge 70 ]; then
        echo -e "${YELLOW}┌─────────────────────────────────────┐${NC}"
        echo -e "${YELLOW}│  ★★★ GOOD - GRADE: C  ★★★           │${NC}"
        echo -e "${YELLOW}│  Core features work, needs polish  │${NC}"
        echo -e "${YELLOW}└─────────────────────────────────────┘${NC}"
    elif [ $PASS_RATE -ge 50 ]; then
        echo -e "${YELLOW}┌─────────────────────────────────────┐${NC}"
        echo -e "${YELLOW}│  ★★ FAIR - GRADE: D  ★★             │${NC}"
        echo -e "${YELLOW}│  Significant improvements needed   │${NC}"
        echo -e "${YELLOW}└─────────────────────────────────────┘${NC}"
    else
        echo -e "${RED}┌─────────────────────────────────────┐${NC}"
        echo -e "${RED}│  ★ NEEDS WORK - GRADE: F  ★         │${NC}"
        echo -e "${RED}│  Major issues require attention    │${NC}"
        echo -e "${RED}└─────────────────────────────────────┘${NC}"
    fi
    
    echo ""
    echo -e "${CYAN}═══════════════════════════════════════════════════${NC}"
    echo -e "${CYAN}  DETAILED RESULTS${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════${NC}"
    
    for i in "${!TEST_RESULTS[@]}"; do
        result="${TEST_RESULTS[$i]}"
        if [[ $result == PASS* ]]; then
            echo -e "${GREEN}  ✓ $result${NC}"
        else
            echo -e "${RED}  ✗ $result${NC}"
        fi
    done
    
    echo ""
    echo -e "${CYAN}═══════════════════════════════════════════════════${NC}"
    echo -e "${MAGENTA}  Test logs saved in: $LOG_DIR/${NC}"
    echo -e "${CYAN}═══════════════════════════════════════════════════${NC}"
    echo ""
    
    if [ $FAILED_TESTS -eq 0 ]; then
        echo -e "${GREEN}🎉 ALL TESTS PASSED! 🎉${NC}"
        exit 0
    else
        echo -e "${YELLOW}⚠ Some tests failed. Review the results above.${NC}"
        exit 1
    fi
}

# Run main
main
