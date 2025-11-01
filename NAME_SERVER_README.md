# Name Server Implementation - LangOS Distributed File System

## Overview

This is the Name Server (NM) component of the LangOS Distributed File System - a distributed, collaborative document system similar to Google Docs. The Name Server acts as the central coordinator, managing file metadata, access control, and routing requests between clients and storage servers.

## Architecture

The Name Server implements several key components:

### 1. **Efficient File Lookup**
- **Trie Data Structure**: O(m) lookup where m is the length of the filename
- **LRU Cache**: Recently accessed files are cached for faster repeated lookups
- Supports fast prefix-based search operations

### 2. **Storage Server Management**
- Maintains registry of all connected storage servers
- Tracks server status (active/inactive)
- Load balances file creation across available servers
- Handles server disconnections gracefully

### 3. **Client Management**
- Tracks all connected clients with their usernames
- Maintains session information
- Validates user permissions for all operations

### 4. **Access Control System**
- Owner-based permissions (file creator)
- Access Control Lists (ACL) per file
- Two access levels: READ and WRITE (WRITE includes READ)
- Owner can grant/revoke access to other users

### 5. **Logging & Error Handling**
- Comprehensive logging to `nm_log.txt`
- Terminal output for real-time monitoring
- Standardized error codes across the system
- Timestamps, IP addresses, ports, and usernames in all logs

## Features Implemented

### File Operations
- ✅ **VIEW**: List files with optional flags (-a for all, -l for detailed)
- ✅ **CREATE**: Create new files on storage servers
- ✅ **READ**: Route read requests to appropriate storage server
- ✅ **WRITE**: Validate permissions and route write requests
- ✅ **DELETE**: Remove files (owner only)
- ✅ **INFO**: Display detailed file metadata
- ✅ **STREAM**: Route streaming requests to storage servers
- ✅ **EXEC**: Execute file contents as shell commands on NM
- ✅ **UNDO**: Route undo requests to storage servers

### Access Control
- ✅ **ADDACCESS**: Grant READ or WRITE access to users
- ✅ **REMACCESS**: Revoke access from users
- ✅ Permission validation on all operations

### User Management
- ✅ **LIST**: Display all registered users
- ✅ Username-based authentication

## Protocol

### Storage Server Registration
```
REGISTER_SS <nm_port> <client_port> <file_count> <file1> <file2> ...
```

### Client Registration
```
REGISTER_CLIENT <username> <nm_port> <ss_port>
```

### Response Format
```
<error_code>:<message>
```

Error codes:
- `0`: Success
- `1`: File not found
- `2`: Unauthorized access
- `3`: File already exists
- `4`: File locked
- `5`: Storage server not found
- `9`: Permission denied
- `99`: System error

### Direct SS Connection
For READ, WRITE, and STREAM operations, NM returns:
```
0:SS_INFO <ip> <port>
```
Client then connects directly to the storage server.

## Building

### Compile
```bash
make
```

### Clean
```bash
make clean
```

### Debug Build
```bash
make debug
```

## Running

### Default Port (8080)
```bash
make run
# or
./name_server 8080
```

### Custom Port
```bash
make run-port PORT=9000
# or
./name_server 9000
```

## File Structure

```
name_server.h          - Header file with data structures and function declarations
name_server.c          - Core functionality (initialization, trie, cache, registration)
name_server_ops.c      - File operation handlers and access control
name_server_main.c     - Connection handling and main server loop
Makefile               - Build system
```

## Key Data Structures

### TrieNode
Provides O(m) file lookup where m is filename length.

### FileMetadata
Stores comprehensive file information:
- Filename, owner, storage server ID
- Timestamps (created, modified, accessed)
- File statistics (size, word count, character count)
- Access Control List

### LRUCache
Caches recently accessed file metadata for performance.

### StorageServer
Tracks storage server connection details and file lists.

### Client
Maintains client session information and credentials.

## Threading Model

- Main thread accepts connections
- Each connection spawns a new thread
- Thread-safe operations using mutexes:
  - `ss_lock`: Protects storage server registry
  - `client_lock`: Protects client registry
  - `trie_lock`: Protects file trie
  - `log_lock`: Protects log file access

## Error Handling

The system provides detailed error messages for:
- File not found
- Permission denied
- Unauthorized access
- Storage server unavailable
- Invalid operations
- System failures

All errors are logged with context (IP, port, username, operation).

## Logging

Logs are written to `nm_log.txt` with format:
```
[TIMESTAMP] [LEVEL] IP=<ip> Port=<port> User=<user> Op=<operation> Details=<details>
```

Log levels: INFO, WARN, ERROR

## Scalability

The implementation is designed for scalability:
- Trie allows efficient file lookup even with millions of files
- LRU cache reduces repeated trie traversals
- Thread-per-connection model handles multiple concurrent clients
- Lock granularity minimizes contention

## Limitations & Future Work

Current implementation:
- Name Server failure brings down the system (as specified)
- No Name Server replication/failover
- Storage server replication not implemented at NM level
- Maximum limits: 100 clients, 50 storage servers

Future enhancements:
- Name Server clustering
- Consistent hashing for SS selection
- Advanced caching strategies
- Metrics and monitoring

## Dependencies

- POSIX threads (pthread)
- Standard C library
- Linux socket API

## Testing

To test the Name Server:
1. Start the Name Server
2. Connect Storage Servers with file lists
3. Connect Clients with usernames
4. Execute file operations through clients

## Authors

Developed for the LangOS Distributed File System course project.

## License

Academic project - All rights reserved.
