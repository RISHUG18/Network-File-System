[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/0ek2UV58)

# LangOS Distributed File System - Course Project

## Team: panic-error

A distributed, collaborative document system similar to Google Docs, built for the Operating Systems and Networks course.

## Project Status

### ✅ Implemented Components

#### Name Server (Complete)
The central coordinator of the system is fully implemented with all required features:

- **Efficient File Lookup**: Trie-based search (O(m) complexity) with LRU caching
- **Storage Server Management**: Registration, tracking, and health monitoring
- **Client Management**: User sessions with username-based authentication
- **Access Control**: Owner-based permissions with ACL support (READ/WRITE)
- **File Operations**: VIEW, CREATE, DELETE, READ, WRITE, INFO, STREAM, EXEC, UNDO
- **User Management**: LIST users, ADDACCESS, REMACCESS
- **Logging**: Comprehensive logging with timestamps, IPs, ports, usernames
- **Error Handling**: 11 error codes covering all scenarios
- **Thread Safety**: Mutex-based concurrency control
- **Protocol**: Well-defined text-based protocol for SS and Client communication

### 🚧 Pending Components

- **Storage Server**: File storage, persistence, undo history, sentence locking
- **Client**: Full user interface with direct SS connectivity for READ/WRITE/STREAM
- **Integration**: End-to-end testing of complete system

## Quick Start

### Build Name Server
```bash
make
```

### Run Name Server
```bash
./name_server 8080
```

### Test with Python Client
```bash
python3 test_client.py <username>
```

## Project Structure

```
.
├── name_server.h           - Header with data structures and declarations
├── name_server.c           - Core NM functionality (trie, cache, registration)
├── name_server_ops.c       - File operations and access control handlers
├── name_server_main.c      - Connection handling and main loop
├── Makefile                - Build system
├── test_client.py          - Python test client for NM
├── NAME_SERVER_README.md   - Detailed NM documentation
├── PROTOCOL.md             - Communication protocol specification
├── TESTING.md              - Comprehensive testing guide
└── README.md               - This file
```

## Architecture

```
┌─────────────┐         ┌─────────────┐         ┌─────────────┐
│   Client 1  │         │   Client 2  │         │   Client N  │
│  (alice)    │         │   (bob)     │         │   (...)     │
└──────┬──────┘         └──────┬──────┘         └──────┬──────┘
       │                       │                        │
       │    Registration &     │                        │
       │    File Operations    │                        │
       └───────────┬───────────┴────────────────────────┘
                   │
                   ▼
         ┌─────────────────────┐
         │   NAME SERVER       │
         │  - File Metadata    │
         │  - Access Control   │
         │  - SS Registry      │
         │  - Client Sessions  │
         │  - Trie + Cache     │
         └─────────┬───────────┘
                   │
       ┌───────────┼───────────┐
       │           │           │
       ▼           ▼           ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────┐
│ Storage     │ │ Storage     │ │ Storage     │
│ Server 1    │ │ Server 2    │ │ Server N    │
│ (pending)   │ │ (pending)   │ │ (pending)   │
└─────────────┘ └─────────────┘ └─────────────┘
```

## Features

### Name Server Features [150 points]

#### File Operations
- ✅ [10] VIEW files with flags (-a, -l, -al)
- ✅ [10] READ file (returns SS info for direct connection)
- ✅ [10] CREATE file
- ✅ [30] WRITE to file (sentence-level with SS coordination)
- ✅ [15] UNDO changes
- ✅ [10] INFO - detailed file metadata
- ✅ [10] DELETE file (owner only)
- ✅ [15] STREAM content (returns SS info)
- ✅ [15] EXEC file as shell script (executed on NM)
- ✅ [10] LIST users

#### Access Control
- ✅ [15] ADDACCESS (-R for read, -W for write)
- ✅ [15] REMACCESS (revoke access)
- ✅ Owner always has full access
- ✅ ACL maintained per file

#### System Requirements [40 points]
- ✅ [10] Efficient Search: Trie + LRU Cache (O(m) lookup)
- ✅ [5] Access Control: ACL-based permissions
- ✅ [5] Logging: Comprehensive with timestamps, IPs, usernames
- ✅ [5] Error Handling: 11 error codes, clear messages
- ⏳ [10] Data Persistence: (to be implemented in SS)
- ⏳ [5] Concurrent Access: (full implementation needs SS)

### Storage Server Features [Pending]
- File storage and retrieval
- Undo history (stack-based)
- Sentence-level locking for WRITE
- Data persistence
- Client direct connection handling

### Client Features [Pending]
- Interactive command-line interface
- Username-based login
- All 11 file operations
- Direct SS connection for READ/WRITE/STREAM
- Error display

## Technical Highlights

### 1. Efficient File Search (O(m) complexity)
- **Trie Data Structure**: Each character is a node, files searchable in O(m) time where m = filename length
- **LRU Cache**: Recent searches cached, evicting least-recently-used entries
- **Better than O(N)**: Meets requirement for faster than linear search

### 2. Scalability
- Thread-per-connection model
- Fine-grained locking (separate locks for SS, clients, trie, logs)
- Non-blocking operations where possible
- Supports 100 concurrent clients, 50 storage servers

### 3. Security
- Username-based authentication
- File ownership tracking
- ACL-based access control (READ, WRITE levels)
- All operations logged for audit

### 4. Error Handling
Comprehensive error codes:
```
0  - Success
1  - File not found
2  - Unauthorized
3  - File exists
4  - File locked
5  - SS not found
9  - Permission denied
99 - System error
```

### 5. Protocol Design
- Text-based protocol for debugging
- Clear request/response format
- Support for both persistent (SS) and session (Client) connections
- Direct client-SS communication for data-heavy operations

## Building and Running

### Build
```bash
make          # Build Name Server
make clean    # Clean artifacts
make debug    # Build with debug symbols
```

### Run Name Server
```bash
./name_server 8080
```

### Test
```bash
# Interactive mode
python3 test_client.py alice

# Automated test suite
python3 test_client.py testuser --test

# Manual testing with netcat
nc localhost 8080
REGISTER_CLIENT testuser 7001 7002
VIEW
LIST
QUIT
```

## Documentation

- **NAME_SERVER_README.md**: Detailed Name Server documentation
- **PROTOCOL.md**: Complete protocol specification with examples
- **TESTING.md**: Comprehensive testing guide
- **This README**: Project overview

## Development Roadmap

### Phase 1: Name Server ✅ (Complete)
- Core data structures (Trie, Cache, registries)
- All file operation handlers
- Access control system
- Logging and error handling
- Thread safety

### Phase 2: Storage Server ⏳ (Next)
- File I/O operations
- Undo history management
- Sentence parsing and locking
- Client connection handler
- Data persistence

### Phase 3: Client ⏳ (Pending)
- User interface
- NM communication
- Direct SS communication for READ/WRITE/STREAM
- All 11 commands

### Phase 4: Integration & Testing ⏳ (Pending)
- End-to-end workflows
- Concurrent access testing
- Stress testing
- Bug fixes and optimization

## Protocol Example

### Client Registration
```
Client → NM: REGISTER_CLIENT alice 7001 7002
NM → Client: 0:Client registered with ID 0
```

### Create File
```
Client → NM: CREATE myfile.txt
NM → SS: CREATE myfile.txt
SS → NM: SUCCESS
NM → Client: 0:File 'myfile.txt' created successfully
```

### Read File
```
Client → NM: READ myfile.txt
NM → Client: 0:SS_INFO 192.168.1.100 9002
Client → SS: (direct connection for reading)
```

## Dependencies

- **GCC**: C compiler with C11 support
- **pthread**: POSIX threads library
- **Standard C Library**: socket, stdio, stdlib, string, time
- **Python 3**: For test client (optional)
- **Linux**: Developed and tested on Linux

## Team

- Course: Operating Systems and Networks (CS3-OSN-Monsoon-2025)
- Team: panic-error
- Repository: course-project-panic-error

## License

Academic project - All rights reserved.

## Acknowledgments

Project specification and requirements provided by the OSN course instructors.

---

**Note**: This is a work in progress. The Name Server is complete and ready for testing. Storage Server and Client implementations are pending.
