# LangOS Client - Complete C Implementation

## Overview

This is a complete C implementation of the client for the LangOS Distributed File System. It provides a full-featured command-line interface with **direct Storage Server connectivity** for READ, WRITE, and STREAM operations.

## Features

### ✅ Complete Protocol Support
- **Name Server Operations** (through NM):
  - VIEW, CREATE, DELETE, INFO, EXEC, UNDO, LIST
  - ADDACCESS, REMACCESS (access control)

- **Direct Storage Server Operations**:
  - READ - Direct connection to SS for reading file content
  - WRITE - Direct connection with ETIRW protocol (lock, write, unlock)
  - STREAM - Direct connection for word-by-word streaming

### ✅ ETIRW Write Protocol
The client implements the complete ETIRW (Edit Text In Realtime with Write-lock) protocol:
1. **Ask NM for SS info** - Client asks Name Server which Storage Server has the file
2. **Connect to SS directly** - Client establishes direct connection to Storage Server
3. **Lock sentence** - Client locks the target sentence before editing
4. **Interactive write mode** - User can update multiple words
5. **Unlock sentence** - Client unlocks when done

## File Structure

```
client.h          - Header file with all declarations
client_core.c     - Core client functions (connection, communication)
client_nm_ops.c   - Name Server operations (VIEW, CREATE, etc.)
client_ss_ops.c   - Storage Server operations (READ, WRITE, STREAM)
client.c          - Main file with CLI and interactive mode
```

## Building

```bash
# Build all components
make

# Build only client
make client

# Clean build
make clean
```

## Usage

### Basic Connection
```bash
# Connect to Name Server
./client <username> [nm_host] [nm_port]

# Examples
./client alice localhost 8080
./client bob 127.0.0.1 8080
```

### Interactive Commands

Once connected, you have access to all commands:

#### File Operations
```bash
# List files
view            # List all accessible files
view -a         # List all files
view -l         # Detailed listing

# Create file
create myfile.txt

# Delete file (owner only)
delete myfile.txt

# Get file info
info myfile.txt
```

#### Direct Storage Server Operations

##### READ - Read File Content
```bash
read myfile.txt
```
**Flow**:
1. Client → NM: "Where is myfile.txt?"
2. NM → Client: "SS at 127.0.0.1:9002"
3. Client connects to SS directly
4. Client → SS: "READ myfile.txt"
5. SS → Client: <file content>
6. Client displays content
7. Client closes SS connection

##### WRITE - Edit File with ETIRW Protocol
```bash
write myfile.txt 0
```
**Flow**:
1. Client → NM: "I want to write to sentence 0 of myfile.txt"
2. NM checks permissions
3. NM → Client: "SS at 127.0.0.1:9002"
4. Client connects to SS directly
5. Client → SS: "WRITE_LOCK myfile.txt 0"
6. SS → Client: "LOCKED" (or error if already locked)
7. **Interactive write mode begins**:
   ```
   write> write 0 Hello
   ✓ Word updated successfully
   write> write 1 world
   ✓ Word updated successfully
   write> done
   ```
8. Client → SS: "WRITE_UNLOCK myfile.txt 0"
9. SS → Client: "UNLOCKED"
10. Client closes SS connection

**Write Commands**:
- `write <word_index> <new_word>` - Update word at index
- `done` - Finish and unlock
- `cancel` - Cancel and unlock

##### STREAM - Stream File Word-by-Word
```bash
stream myfile.txt
```
**Flow**:
1. Client → NM: "Where is myfile.txt for streaming?"
2. NM → Client: "SS at 127.0.0.1:9002"
3. Client connects to SS directly
4. Client → SS: "STREAM myfile.txt"
5. SS sends words one-by-one with 0.1s delay
6. Client displays each word as it arrives
7. Connection closes when file complete

#### Other Operations
```bash
# Execute file as script (on Name Server)
exec script.sh

# Undo last change
undo myfile.txt

# List all users
users

# Grant access
addaccess R myfile.txt bob      # Read access
addaccess W myfile.txt charlie  # Write access (includes read)

# Revoke access
remaccess myfile.txt bob

# Help
help

# Quit
quit
```

## Architecture

### Connection Flow

```
                    ┌─────────────┐
                    │ Name Server │
                    │  (port 8080)│
                    └──────┬──────┘
                           │
                    ┌──────┴──────┐
                    │             │
         Initial    │             │  SS Info Request
        Connection  │             │  (READ/WRITE/STREAM)
                    │             │
              ┌─────▼─────┐       │
              │  Client   ◄───────┘
              └─────┬─────┘
                    │
                    │ Direct Connection
                    │ (for data operations)
                    │
              ┌─────▼──────────┐
              │ Storage Server │
              │  (port 9002)   │
              └────────────────┘
```

### Why Direct SS Connection?

For data-intensive operations (READ, WRITE, STREAM), the client connects **directly** to the Storage Server instead of routing through the Name Server. This:

1. **Reduces load** on Name Server
2. **Improves performance** (no intermediate hop)
3. **Enables streaming** (continuous data flow)
4. **Allows concurrent access** (multiple clients to different SS)

The Name Server acts as a **coordinator** that:
- Validates permissions
- Provides SS location
- Maintains file metadata

## Protocol Details

### Name Server Protocol

#### Request Format
```
COMMAND [args...]\n
```

#### Response Format
```
<error_code>:<message>\n
```

**Error Codes**:
- `0` - Success
- `1` - File not found
- `2` - Unauthorized (no read access)
- `3` - File already exists
- `9` - Permission denied (no write access)
- `99` - Internal error

#### Example: CREATE
```
Client → NM: CREATE myfile.txt\n
NM → Client: 0:File created successfully\n
```

#### Example: READ (get SS info)
```
Client → NM: READ myfile.txt\n
NM → Client: 0:SS_INFO 127.0.0.1 9002\n
```

### Storage Server Protocol

#### READ
```
Client → SS: READ myfile.txt\n
SS → Client: <file content>
```

#### WRITE (ETIRW Protocol)
```
# Lock
Client → SS: WRITE_LOCK myfile.txt 0\n
SS → Client: LOCKED\n

# Write word
Client → SS: WRITE myfile.txt 0 2 newword\n
SS → Client: SUCCESS\n

# Unlock
Client → SS: WRITE_UNLOCK myfile.txt 0\n
SS → Client: UNLOCKED\n
```

#### STREAM
```
Client → SS: STREAM myfile.txt\n
SS → Client: word1\n
             (0.1s delay)
             word2\n
             (0.1s delay)
             word3\n
             ...
             <connection closes>
```

## Code Examples

### Reading a File
```c
void cmd_read_file(Client* client, const char* filename) {
    // 1. Get SS info from NM
    char ss_ip[64];
    int ss_port;
    if (!get_ss_info(client, "READ myfile.txt", ss_ip, &ss_port)) {
        return;
    }
    
    // 2. Connect to SS
    int ss_socket = connect_to_ss(ss_ip, ss_port);
    
    // 3. Send READ command
    send_ss_command(ss_socket, "READ myfile.txt", response, sizeof(response));
    
    // 4. Display content
    printf("%s\n", response);
    
    // 5. Close connection
    close(ss_socket);
}
```

### Writing to a File
```c
void cmd_write_file(Client* client, const char* filename, int sentence_num) {
    // 1. Get SS info from NM
    char ss_ip[64];
    int ss_port;
    if (!get_ss_info(client, cmd, ss_ip, &ss_port)) {
        return;
    }
    
    // 2. Connect to SS
    int ss_socket = connect_to_ss(ss_ip, ss_port);
    
    // 3. Lock sentence
    send_ss_command(ss_socket, "WRITE_LOCK myfile.txt 0", response, ...);
    
    // 4. Interactive write loop
    while (true) {
        // User enters: write 2 hello
        send_ss_command(ss_socket, "WRITE myfile.txt 0 2 hello", ...);
    }
    
    // 5. Unlock sentence
    send_ss_command(ss_socket, "WRITE_UNLOCK myfile.txt 0", ...);
    
    // 6. Close connection
    close(ss_socket);
}
```

## Testing

### Test Scenario 1: Basic File Operations
```bash
# Terminal 1: Start Name Server
./name_server 8080

# Terminal 2: Start Storage Server
./storage_server 127.0.0.1 8080 9002

# Terminal 3: Client
./client alice localhost 8080

alice> create test.txt
✓ File created successfully

alice> view -l
Files:
  test.txt (owner: alice)

alice> info test.txt
File: test.txt
Owner: alice
Size: 0 bytes
Created: 2024-11-01 10:30:00
```

### Test Scenario 2: Direct SS Read/Write
```bash
alice> write test.txt 0
✓ Storage Server: 127.0.0.1:9002
Connecting to Storage Server...
Locking sentence 0...
✓ Sentence locked

Write Mode (ETIRW Protocol)
Commands:
  write <word_index> <new_word>  - Update word at index
  done                           - Finish and unlock
  cancel                         - Cancel and unlock

write> write 0 Hello
✓ Word updated successfully

write> write 1 world
✓ Word updated successfully

write> done
Unlocking sentence...
✓ Sentence unlocked

alice> read test.txt
✓ Storage Server: 127.0.0.1:9002
Connecting to Storage Server...

--- File Content ---
Hello world.
--- End of File ---
```

### Test Scenario 3: Streaming
```bash
alice> stream test.txt
✓ Storage Server: 127.0.0.1:9002
Connecting to Storage Server...

--- Streaming File ---
Hello 
world.
--- End of Stream ---
```

## Key Differences from Python Client

| Feature | Python Client | C Client |
|---------|--------------|----------|
| **Direct SS Connection** | ❌ No (only gets info) | ✅ Yes (READ/WRITE/STREAM) |
| **ETIRW Protocol** | ❌ Not implemented | ✅ Full implementation |
| **Interactive Write** | ❌ No | ✅ Yes (lock, edit, unlock) |
| **Streaming** | ❌ No | ✅ Yes (word-by-word) |
| **Performance** | Slower (interpreted) | Faster (compiled C) |
| **Memory Usage** | Higher | Lower |

## Error Handling

The client handles all error conditions:
- **Connection failures** - Clear error messages
- **Permission denied** - Shows access control errors
- **File not found** - Indicates missing files
- **Locked sentences** - Reports if sentence already locked by another client
- **Network errors** - Handles disconnections gracefully

## Best Practices

1. **Always unlock sentences** - Use `done` or `cancel` in write mode
2. **Check permissions** - Use `info` to see file access before writing
3. **Handle errors** - Read error messages carefully
4. **Clean exit** - Use `quit` to disconnect properly
5. **Test before production** - Try operations with test files first

## Limitations

- Maximum file size: 1 MB
- Maximum filename length: 256 characters
- Maximum username length: 64 characters
- Buffer size: 16 KB for most operations

## Troubleshooting

### "Failed to connect to Name Server"
- Check if Name Server is running: `ps aux | grep name_server`
- Verify port: `netstat -tuln | grep 8080`
- Check firewall rules

### "Failed to connect to Storage Server"
- Storage Server may not be registered with NM
- Check SS logs: `cat ss_log.txt`
- Verify SS is running: `ps aux | grep storage_server`

### "Sentence is locked"
- Another client is currently editing that sentence
- Wait for them to finish or contact them
- Check SS logs to see lock holder

### "Permission denied"
- You don't have write access to this file
- Ask owner to grant access: `addaccess W <file> <your_username>`
- Check your access: `info <file>`

## Future Enhancements

- [ ] Batch write operations (multiple words at once)
- [ ] File upload/download from local filesystem
- [ ] Diff view for undo operations
- [ ] Auto-reconnect on connection loss
- [ ] Command history (up/down arrows)
- [ ] Tab completion for filenames
- [ ] Configuration file support

## Conclusion

This C client provides a **complete, production-ready** implementation of the LangOS client with:
- ✅ All 11 file operations
- ✅ Direct Storage Server connectivity
- ✅ Full ETIRW write protocol
- ✅ Interactive command-line interface
- ✅ Comprehensive error handling
- ✅ Clean, modular code structure

The client successfully demonstrates the **three-tier architecture** of the LangOS Distributed File System: Clients → Name Server → Storage Servers, with direct client-SS connections for data operations.
