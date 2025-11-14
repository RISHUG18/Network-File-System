# Manual Test Commands for Failing Tests

Based on the test output, here are the **3 failing tests** and exact commands to reproduce them manually:

---

## Test Failure 1: Owner reads file (Test 4)
**Issue**: Timeout or no response when reading file

### Manual Test Commands:

```bash
# Terminal 1: Start Name Server
./name_server

# Terminal 2: Start Storage Server
./storage_server 127.0.0.1 8080 9002

# Terminal 3: Run Client Commands
./client

# In client prompt, execute:
REGISTER file_creator 10300 10400
CREATE readable_file.txt
READ readable_file.txt
```

### Expected Behavior:
- Should receive SS_INFO from Name Server with IP and port
- Should connect to Storage Server successfully
- Should read file content without timeout

### Debug Steps:
1. Check if file exists: `ls storage/readable_file.txt`
2. Verify Name Server logs for READ command
3. Check Storage Server logs for READ request
4. Test with: `echo "test content" > storage/readable_file.txt` then try READ again

---

## Test Failure 2: Concurrent writes to different sentences (Test 5.3)
**Issue**: 0 out of 5 concurrent writes succeeded

### Manual Test Commands:

```bash
# Setup (in Terminal 3 client):
REGISTER file_creator 10900 11000
CREATE writable_file.txt

# Test concurrent writes manually with multiple client instances:

# Terminal 3: Client 1
./client
REGISTER file_creator 10900 11000
# Get SS info
# Then connect directly to storage server at 127.0.0.1:9002

# Use netcat or telnet to test:
nc 127.0.0.1 9002

# Send these commands in sequence:
WRITE writable_file.txt 10
# Wait for LOCKED response
0 Word0
# Wait for SUCCESS response
ETIRW
# Wait for SUCCESS response
```

### Expected Behavior:
- Each client should lock different sentence (10, 11, 12, 13, 14)
- All 5 writes should succeed since they're on different sentences
- No lock contention should occur

### Debug Steps:
1. Try writing to sentence 0 first: `WRITE writable_file.txt 0`
2. Check if LOCKED response is received
3. Try word update: `0 TestWord`
4. Check if SUCCESS is received
5. Finalize: `ETIRW`
6. Check if SUCCESS is received

---

## Test Failure 3: Sentence lock serialization (Test 5.4)
**Issue**: 0 locked, 3 blocked (expected 1+ locked)

### Manual Test Commands:

```bash
# Terminal 3: Client 1 (hold lock)
./client
REGISTER file_creator 11100 11200

# Connect to SS directly:
nc 127.0.0.1 9002

# Lock sentence 100:
WRITE writable_file.txt 100
# Should get LOCKED response
# Now HOLD this lock (don't send ETIRW yet)

# Terminal 4: Client 2 (try to lock same sentence)
nc 127.0.0.1 9002

# Try to lock SAME sentence:
WRITE writable_file.txt 100
# Should get ERROR or "locked by another client" response

# Terminal 5: Client 3 (another attempt)
nc 127.0.0.1 9002
WRITE writable_file.txt 100
# Should also get blocked

# Now in Terminal 3, release the lock:
ETIRW
# Should get SUCCESS

# Now Terminal 4 or 5 can try again:
WRITE writable_file.txt 100
# Should now succeed
```

### Expected Behavior:
- First client locks sentence 100 successfully
- Second and third clients get ERROR (sentence locked)
- After first client releases (ETIRW), others can acquire lock

### Debug Steps:
1. Check if sentence locking is working at all
2. Verify Storage Server has proper lock tracking
3. Check if client_id is being passed correctly
4. Look at Storage Server code for `lock_sentence()` and `unlock_sentence()` functions

---

## Additional Debug Commands:

### Check Storage Server State:
```bash
# View storage server logs
cat ss_log.txt

# Check created files
ls -la storage/

# View file content
cat storage/readable_file.txt
cat storage/writable_file.txt
```

### Check Name Server State:
```bash
# View name server logs
cat nm_log.txt
```

### Network Testing:
```bash
# Test if ports are open
netstat -tuln | grep 8080  # Name Server
netstat -tuln | grep 9002  # Storage Server

# Test connection
telnet localhost 8080
telnet localhost 9002
```

---

## Quick Test Script:

```bash
#!/bin/bash
# quick_test_failing.sh

echo "=== Testing Failed Test Cases ==="

# Kill existing processes
pkill -9 name_server
pkill -9 storage_server
sleep 1

# Start servers
./name_server &
NS_PID=$!
sleep 2

./storage_server 127.0.0.1 8080 9002 &
SS_PID=$!
sleep 2

# Test 1: Read file
echo -e "\n=== TEST 1: Read File ==="
echo "REGISTER testuser 10300 10400
CREATE test_read.txt
WRITE test_read.txt
VIEW test_read.txt
READ test_read.txt
QUIT" | timeout 10 ./client

# Test 2: Write operations
echo -e "\n=== TEST 2: Write Operations ==="
echo "REGISTER testuser 10900 11000
CREATE test_write.txt
WRITE test_write.txt 0
QUIT" | timeout 10 ./client

# Cleanup
kill $NS_PID $SS_PID 2>/dev/null
```

Make this executable and run:
```bash
chmod +x quick_test_failing.sh
./quick_test_failing.sh
```

---

## Key Issues to Check:

1. **Timeout on READ**: 
   - Check if Storage Server is responding to READ commands
   - Verify socket communication is working
   - Check if file actually exists in storage

2. **Concurrent Writes Failing**:
   - Verify sentence locking mechanism
   - Check if multiple clients can connect simultaneously
   - Ensure different sentences don't interfere

3. **Lock Serialization**:
   - Verify lock tracking per sentence
   - Check if client_id is properly used
   - Ensure locks are released after ETIRW

Run these manual tests and check the outputs!
