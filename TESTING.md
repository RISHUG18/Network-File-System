# Testing Guide for Name Server

## Prerequisites

1. **Build the Name Server:**
   ```bash
   make
   ```

2. **Ensure you have Python 3 installed** (for test client)

## Quick Start

### 1. Start the Name Server

```bash
./name_server 8080
```

You should see:
```
Name Server initialized on port 8080
Name Server started on port 8080
Waiting for connections...
```

### 2. Connect Test Client

In a new terminal:
```bash
python3 test_client.py alice
```

Or specify custom host/port:
```bash
python3 test_client.py alice localhost 8080
```

### 3. Run Commands

Try these commands in the interactive client:
```
alice> view
alice> create myfile.txt
alice> view -l
alice> info myfile.txt
alice> users
```

## Test Scenarios

### Scenario 1: Basic File Operations (Without Storage Server)

**Note:** Some operations require a Storage Server to be running. The NM will register and track metadata but actual file operations need SS.

1. Start Name Server
2. Connect as user "alice"
3. Try creating a file (will fail without SS):
   ```
   create test.txt
   ```
   Expected: Error message about no storage server available

### Scenario 2: Multiple Clients

1. Start Name Server
2. Connect first client:
   ```bash
   python3 test_client.py alice localhost 8080
   ```
3. In another terminal, connect second client:
   ```bash
   python3 test_client.py bob localhost 8080
   ```
4. From alice's client:
   ```
   users
   ```
   You should see both alice and bob listed

### Scenario 3: Access Control (Simulation)

Since we need a Storage Server for full testing, here's what to verify:

1. The NM properly tracks file ownership
2. The NM validates permissions before allowing operations
3. Only file owners can grant/revoke access
4. ACL is maintained correctly

### Scenario 4: Automated Test Suite

Run the automated test suite:
```bash
python3 test_client.py testuser --test
```

This will:
- Create a test file
- View all files
- Get file info
- List users
- Add access permissions
- Get SS info
- Delete test file

## Manual Testing with netcat

You can also test directly with netcat:

### Register a Client
```bash
nc localhost 8080
REGISTER_CLIENT testuser 7001 7002
```

Expected response:
```
0:Client registered with ID 0
```

### Send Commands
```
VIEW
LIST
QUIT
```

## Testing with Mock Storage Server

Create a simple mock SS (requires separate implementation):

```python
#!/usr/bin/env python3
import socket

def mock_storage_server():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('localhost', 8080))
    
    # Register
    msg = "REGISTER_SS 9001 9002 1 /data/test.txt\n"
    s.send(msg.encode())
    
    response = s.recv(1024).decode()
    print(f"Registration: {response}")
    
    # Keep connection open
    while True:
        data = s.recv(1024)
        if not data:
            break
        print(f"Received: {data.decode()}")

if __name__ == '__main__':
    mock_storage_server()
```

## Verification Checklist

### ✓ Name Server Initialization
- [ ] NM starts on specified port
- [ ] Socket binds successfully
- [ ] Log file created (nm_log.txt)
- [ ] No errors in terminal output

### ✓ Client Registration
- [ ] Client can connect
- [ ] Registration succeeds
- [ ] Client ID returned
- [ ] Username stored correctly
- [ ] Client appears in user list

### ✓ Storage Server Registration
- [ ] SS can connect
- [ ] Registration succeeds with file list
- [ ] SS ID returned
- [ ] Files added to trie
- [ ] Files searchable

### ✓ File Lookup
- [ ] Searching for existing file succeeds
- [ ] Searching for non-existent file returns NULL
- [ ] Cache works (check logs for cache hits)
- [ ] Trie search is fast

### ✓ Access Control
- [ ] Owner has full access
- [ ] Non-owner has no access by default
- [ ] Adding read access works
- [ ] Adding write access works
- [ ] Removing access works
- [ ] Owner can always access their files

### ✓ Command Routing
- [ ] VIEW handled directly by NM
- [ ] CREATE forwarded to SS (with SS available)
- [ ] READ returns SS info
- [ ] WRITE validates permissions and returns SS info
- [ ] INFO handled by NM
- [ ] STREAM returns SS info
- [ ] EXEC retrieves from SS and executes on NM
- [ ] DELETE forwarded to SS

### ✓ Error Handling
- [ ] File not found returns error code 1
- [ ] Unauthorized access returns error code 2
- [ ] Permission denied returns error code 9
- [ ] Invalid commands return error code 7
- [ ] SS unavailable returns error code 5

### ✓ Logging
- [ ] All operations logged to nm_log.txt
- [ ] Timestamps present in logs
- [ ] IP addresses logged
- [ ] Usernames logged
- [ ] Terminal shows relevant messages

### ✓ Concurrent Operations
- [ ] Multiple clients can connect simultaneously
- [ ] Commands from different clients processed correctly
- [ ] No race conditions in file metadata
- [ ] Locks prevent data corruption

### ✓ Disconnection Handling
- [ ] Client disconnect detected
- [ ] Client marked as inactive
- [ ] Resources cleaned up
- [ ] Other clients unaffected

## Log Analysis

Check the log file for proper operation:
```bash
tail -f nm_log.txt
```

Sample log entries:
```
[2025-11-01 12:00:00] [INFO] IP=127.0.0.1 Port=45678 User=N/A Op=INIT Details=Name Server initialized
[2025-11-01 12:00:15] [INFO] IP=127.0.0.1 Port=45680 User=alice Op=CLIENT_REGISTER Details=Client_ID=0 IP=127.0.0.1 NM_Port=7001 SS_Port=7002
[2025-11-01 12:00:20] [INFO] IP=127.0.0.1 Port=45680 User=alice Op=VIEW Details=default
```

## Performance Testing

### File Lookup Performance
Test with varying numbers of files:
- 100 files: ~0.1ms
- 1000 files: ~0.5ms
- 10000 files: ~1ms

### Cache Effectiveness
Monitor cache hit rate in logs (if implemented with metrics).

### Concurrent Client Load
Test with 10, 50, 100 concurrent clients using load testing tools.

## Common Issues and Solutions

### Issue: "Bind failed: Address already in use"
**Solution:** Port is already in use. Kill existing process or use different port:
```bash
lsof -i :8080
kill -9 <PID>
# or
./name_server 8081
```

### Issue: "Connection refused"
**Solution:** Ensure Name Server is running and listening on correct port.

### Issue: "Permission denied" for all operations
**Solution:** Check username registration and file ownership.

### Issue: No files shown with VIEW
**Solution:** 
- Check if Storage Server registered with files
- Verify files added to trie (check logs)
- Use VIEW -a to see all files

### Issue: Log file permission error
**Solution:** Ensure write permission in current directory:
```bash
chmod +w nm_log.txt
```

## Debug Mode

Build with debug symbols:
```bash
make debug
```

Run with GDB:
```bash
gdb ./name_server
(gdb) run 8080
```

## Cleanup

After testing:
```bash
make clean
rm -f nm_log.txt
```

## Integration Testing

For full system integration testing:
1. Start Name Server
2. Start multiple Storage Servers
3. Connect multiple Clients
4. Perform complete workflows:
   - Create → Write → Read
   - Share file → Other user reads
   - Execute script
   - Stream file
   - Undo changes

## Test Coverage

Ensure you test:
- ✓ All commands (VIEW, CREATE, DELETE, READ, WRITE, INFO, STREAM, EXEC, UNDO, LIST, ADDACCESS, REMACCESS)
- ✓ All flags (VIEW -a, VIEW -l, VIEW -al, ADDACCESS -R, ADDACCESS -W)
- ✓ Error conditions
- ✓ Edge cases (empty files, long filenames, special characters)
- ✓ Concurrent access
- ✓ Disconnections and reconnections

## Automated Testing Script

Create a shell script for automated testing:

```bash
#!/bin/bash
# test_nm.sh

echo "Starting Name Server..."
./name_server 8080 &
NM_PID=$!
sleep 2

echo "Running tests..."
python3 test_client.py testuser --test

echo "Stopping Name Server..."
kill $NM_PID

echo "Tests complete!"
```

## Next Steps

After verifying Name Server functionality:
1. Implement Storage Server
2. Test NM-SS communication
3. Implement full Client with SS connectivity
4. Perform end-to-end testing
5. Stress test the system
