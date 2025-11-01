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

#### Storage Server (Complete)
Handles actual file storage and client operations:

- **Concurrent File Access**: Reader-writer locks + sentence-level locking
- **Sentence Parsing**: Automatic parsing with `.!?` delimiters
- **Undo Mechanism**: 100-entry circular buffer with full file snapshots
- **Data Persistence**: Files stored in `./storage/` with auto-save
- **Streaming**: Word-by-word transmission with 0.1s delay
- **Write Operations**: Word-level updates with sentence re-parsing
- **Thread Safety**: RW locks for files, mutexes for sentences
- **Client Direct Connection**: Handles READ, WRITE, STREAM directly
- **NM Communication**: CREATE, DELETE, INFO, UNDO via NM

### 🚧 Pending Components

- **Full Client Implementation**: Complete CLI client with all operations and direct SS connectivity
- **Advanced Features**: Multi-server replication, load balancing, compression

## Quick Start

### Build Both Servers
```bash
make
```

### Terminal 1: Start Name Server
```bash
./name_server 8080
```

### Terminal 2: Start Storage Server
```bash
./storage_server 127.0.0.1 8080 9002
```

### Terminal 3: Test with Python Client
```bash
python3 test_client.py alice
```

### Or Run Integration Test
```bash
./test_integration.sh
```

## Project Structure

```
.
├── name_server.h           - NM data structures and declarations
├── name_server.c           - NM core (trie, cache, registration)
├── name_server_ops.c       - NM file operations and access control
├── name_server_main.c      - NM connection handling and main loop
├── storage_server.h        - SS data structures and declarations
├── storage_server.c        - SS core (parsing, persistence, undo)
├── storage_server_ops.c    - SS file operations and locking
├── storage_server_main.c   - SS networking and client handling
├── Makefile                - Build system for both servers
├── test_client.py          - Python test client for NM
├── test_integration.sh     - Full system integration test
├── NAME_SERVER_README.md   - Detailed NM documentation
├── STORAGE_SERVER_README.md- Detailed SS documentation
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
- ✅ [10] Data Persistence: Files in ./storage/, auto-save on write
- ✅ [5] Concurrent Access: RW locks + sentence-level mutexes

## Concurrency Implementation

### Name Server Concurrency
- **Trie Lock**: `pthread_mutex_t` for file metadata operations
- **SS Lock**: `pthread_mutex_t` for storage server registry
- **Client Lock**: `pthread_mutex_t` for client registry
- **Log Lock**: `pthread_mutex_t` for thread-safe logging
- **Cache Lock**: `pthread_mutex_t` for LRU cache operations
- **Thread-per-connection**: Each client/SS connection handled in separate thread

### Storage Server Concurrency
- **File RW Lock**: `pthread_rwlock_t` per file
  - Multiple readers simultaneously
  - Exclusive writer access
  - Prevents race conditions during reads/writes
- **Sentence Mutex**: `pthread_mutex_t` per sentence
  - Locks individual sentences during WRITE
  - Prevents concurrent edits to same sentence
  - Allows parallel writes to different sentences
- **Undo Stack Lock**: `pthread_mutex_t` for undo history
- **File List Lock**: `pthread_mutex_t` for file registry
- **Log Lock**: `pthread_mutex_t` for logging

### Concurrency Features
1. **Multiple Concurrent Readers**: Any number of clients can read the same file simultaneously
2. **Sentence-Level Write Locking**: Different clients can write to different sentences in parallel
3. **Lock Holder Tracking**: Each sentence remembers which client has it locked
4. **Deadlock Prevention**: Proper lock ordering (file lock → sentence lock)
5. **No Race Conditions**: All shared data protected by appropriate locks

### Storage Server Features [Complete]
- ✅ File storage and retrieval with persistence
- ✅ Undo history (100-entry circular buffer with snapshots)
- ✅ Sentence-level locking for WRITE (mutex per sentence)
- ✅ Data persistence (./storage/ directory)
- ✅ Client direct connection handling
- ✅ Concurrent file access (reader-writer locks)
- ✅ Word-by-word streaming with 0.1s delay
- ✅ Automatic sentence parsing and re-parsing
- ✅ Thread-safe operations

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

### Build Everything
```bash
make          # Build both Name Server and Storage Server
make clean    # Clean artifacts
make debug    # Build with debug symbols
```

### Run Name Server
```bash
./name_server 8080
```

### Run Storage Server(s)
```bash
# Server 1
./storage_server 127.0.0.1 8080 9002

# Server 2 (optional, in another terminal)
./storage_server 127.0.0.1 8080 9003
```

### Test
```bash
# Interactive mode
python3 test_client.py alice

# Automated test suite
python3 test_client.py testuser --test

# Full integration test
./test_integration.sh

# Manual testing with netcat
nc localhost 8080
REGISTER_CLIENT testuser 7001 7002
CREATE myfile.txt
VIEW -l
INFO myfile.txt
QUIT
```

## Documentation

- **NAME_SERVER_README.md**: Detailed Name Server documentation
- **STORAGE_SERVER_README.md**: Detailed Storage Server documentation
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

### Phase 2: Storage Server ✅ (Complete)
- File I/O operations with persistence
- Undo history management (circular buffer)
- Sentence parsing and locking
- Client connection handler
- Concurrent access control (RW locks + mutexes)
- Data persistence in ./storage/
- Word-by-word streaming

### Phase 3: Client ⏳ (Pending)
- User interface (CLI)
- NM communication
- Direct SS communication for READ/WRITE/STREAM
- All 11 commands
- WRITE command with ETIRW protocol

### Phase 4: Integration & Testing ✅ (Complete)
- NM-SS integration ✅
- End-to-end workflows ✅
- Concurrent access testing ✅
- Integration test script ✅
- Bug fixes and optimization (ongoing)

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
