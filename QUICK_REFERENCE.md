# Name Server Quick Reference

## Build & Run
```bash
make                    # Build
make clean              # Clean
./name_server 8080      # Start on port 8080
```

## Test
```bash
python3 test_client.py alice                # Interactive mode
python3 test_client.py testuser --test      # Automated tests
nc localhost 8080                           # Manual testing
```

## Commands (from Client)

### File Operations
```
VIEW [-a] [-l]               List files
CREATE <file>                Create file
DELETE <file>                Delete file (owner only)
READ <file>                  Get SS info for reading
WRITE <file> <sent#>         Get SS info for writing
INFO <file>                  Show file details
STREAM <file>                Get SS info for streaming
EXEC <file>                  Execute as shell script
UNDO <file>                  Undo last change
```

### Access Control
```
ADDACCESS -R <file> <user>   Grant read access
ADDACCESS -W <file> <user>   Grant write access
REMACCESS <file> <user>      Revoke access
```

### User Management
```
LIST                         List all users
```

### Session
```
QUIT / EXIT                  Disconnect
```

## Registration Protocols

### Storage Server
```
REGISTER_SS <nm_port> <client_port> <file_count> <file1> <file2> ...
Response: 0:SS registered with ID <id>
```

### Client
```
REGISTER_CLIENT <username> <nm_port> <ss_port>
Response: 0:Client registered with ID <id>
```

## Response Format
```
<error_code>:<message>
```

## Error Codes
| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | File not found |
| 2 | Unauthorized |
| 3 | File exists |
| 4 | File locked |
| 5 | SS not found |
| 9 | Permission denied |
| 99 | System error |

## File Structure
```
name_server.h          Headers & data structures
name_server.c          Core implementation
name_server_ops.c      File operations
name_server_main.c     Connection handling
Makefile               Build system
test_client.py         Test client
```

## Key Data Structures

### FileMetadata
- filename, owner, ss_id
- timestamps (created, modified, accessed)
- stats (size, words, chars)
- ACL (access control list)

### StorageServer
- id, ip, ports
- file list
- active status

### Client
- id, username, ip, ports
- active status
- connected time

## Logging
- **File**: `nm_log.txt`
- **Format**: `[TIMESTAMP] [LEVEL] IP=<ip> Port=<port> User=<user> Op=<op> Details=<details>`
- **Terminal**: Real-time operation status

## Performance
- File lookup: O(m) - m = filename length
- Cache hit: O(1)
- Supports: 100 clients, 50 storage servers

## Thread Safety
4 mutexes protect:
- SS registry (`ss_lock`)
- Client registry (`client_lock`)
- File trie (`trie_lock`)
- Log file (`log_lock`)

## Network
- Protocol: TCP
- Port: Configurable (default 8080)
- Encoding: UTF-8 text
- Max message: 4096 bytes

## Debugging

### Check logs
```bash
tail -f nm_log.txt
```

### Debug build
```bash
make debug
gdb ./name_server
(gdb) run 8080
```

### Check port usage
```bash
lsof -i :8080
netstat -tulpn | grep 8080
```

## Common Issues

**Port in use**: 
```bash
lsof -i :8080
kill -9 <PID>
```

**Connection refused**: Ensure NM is running

**Permission denied**: Check file ownership and ACL

**No files visible**: Storage Server not registered or VIEW -a

## Protocol Flow

### File Creation
```
Client → NM: CREATE myfile.txt
NM → SS: CREATE myfile.txt
SS → NM: SUCCESS
NM → Client: 0:File created
```

### File Read
```
Client → NM: READ myfile.txt
NM validates access
NM → Client: 0:SS_INFO 192.168.1.100 9002
Client → SS: (direct connection)
```

### Access Grant
```
Client → NM: ADDACCESS -W myfile.txt bob
NM validates (owner check)
NM updates ACL
NM → Client: 0:Access granted
```

## Documentation
- `NAME_SERVER_README.md` - Detailed docs
- `PROTOCOL.md` - Protocol spec
- `TESTING.md` - Testing guide
- `IMPLEMENTATION_SUMMARY.md` - Implementation details

## Tips
1. Start NM before SS/Clients
2. Use unique usernames
3. Check logs for debugging
4. Use test client for quick testing
5. File operations need SS running

## Contact
- Repository: course-project-panic-error
- Course: CS3-OSN-Monsoon-2025
