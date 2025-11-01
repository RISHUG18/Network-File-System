# Storage Server Implementation

## Overview

The Storage Server is responsible for actual file storage, retrieval, and manipulation. It handles direct client connections for READ, WRITE, and STREAM operations, while coordinating with the Name Server for metadata management.

## Key Features

### 1. **Concurrent File Access**
- **Reader-Writer Locks**: Each file has a `pthread_rwlock_t` allowing multiple simultaneous readers or one exclusive writer
- **Sentence-Level Locking**: Individual sentences can be locked during WRITE operations
- **Lock Holder Tracking**: Each sentence lock tracks which client holds it
- **Deadlock Prevention**: Proper lock ordering and timeout mechanisms

### 2. **Sentence Parsing**
- Sentences are delimited by `.`, `!`, or `?`
- Dynamic sentence array that grows as needed
- Automatic re-parsing when delimiters are added/removed during writes
- Word-level granularity within sentences

### 3. **Undo Mechanism**
- Circular buffer with 100 entry history
- Full file snapshot stored for each change
- Per-file undo history
- Thread-safe with mutex protection

### 4. **Data Persistence**
- Files stored in `./storage/` directory
- Automatic save after every WRITE operation
- Files loaded on server startup
- Metadata preserved (timestamps, word counts)

### 5. **Streaming Support**
- Word-by-word transmission with 0.1s delay
- Graceful handling of client disconnection
- Non-blocking sends with `MSG_NOSIGNAL`

## Architecture

```
┌──────────────────────────────────────┐
│       Storage Server                 │
│                                      │
│  ┌────────────────────────────────┐ │
│  │  File Manager                  │ │
│  │  - FileEntry array             │ │
│  │  - Concurrent access control   │ │
│  │  - Sentence parsing            │ │
│  └────────────────────────────────┘ │
│                                      │
│  ┌────────────────────────────────┐ │
│  │  Undo Stack                    │ │
│  │  - Circular buffer             │ │
│  │  - File snapshots              │ │
│  └────────────────────────────────┘ │
│                                      │
│  ┌────────────────┬───────────────┐ │
│  │ NM Connection  │ Client Server │ │
│  │ - CREATE       │ - READ        │ │
│  │ - DELETE       │ - WRITE       │ │
│  │ - INFO         │ - STREAM      │ │
│  │ - UNDO         │ - Lock/Unlock │ │
│  └────────────────┴───────────────┘ │
└──────────────────────────────────────┘
```

## Data Structures

### FileEntry
```c
typedef struct FileEntry {
    char filename[MAX_FILENAME];
    char filepath[MAX_PATH];
    Sentence* sentences;           // Dynamic array
    int sentence_count;
    size_t total_size;
    int total_words;
    int total_chars;
    pthread_rwlock_t file_lock;    // For file-level concurrency
    time_t last_modified;
    time_t last_accessed;
} FileEntry;
```

### Sentence
```c
typedef struct Sentence {
    char* content;
    int length;
    int word_count;
    pthread_mutex_t lock;          // Sentence-level lock
    bool is_locked;
    int lock_holder_id;            // Client ID
} Sentence;
```

### UndoStack
```c
typedef struct UndoStack {
    UndoEntry entries[100];        // Circular buffer
    int top;
    pthread_mutex_t lock;
} UndoStack;
```

## Concurrency Model

### File-Level Locks
- **Read Operations**: Acquire `pthread_rwlock_rdlock()`
  - Multiple clients can read simultaneously
  - No writers allowed during reads
  
- **Write Operations**: Acquire `pthread_rwlock_wrlock()`
  - Exclusive access to file
  - No other readers or writers

### Sentence-Level Locks
- **WRITE_LOCK**: Client acquires lock on specific sentence
- **WRITE**: Updates can only be made if client holds the lock
- **WRITE_UNLOCK**: Releases the sentence lock
- **Lock Holder Tracking**: Prevents other clients from modifying locked sentences

### Thread Safety
```c
// Reading a file (allows concurrency)
pthread_rwlock_rdlock(&file->file_lock);
// ... read operations ...
pthread_rwlock_unlock(&file->file_lock);

// Writing to a file (exclusive)
pthread_rwlock_wrlock(&file->file_lock);
// ... write operations ...
pthread_rwlock_unlock(&file->file_lock);

// Locking a sentence
pthread_mutex_lock(&sentence->lock);
if (!sentence->is_locked) {
    sentence->is_locked = true;
    sentence->lock_holder_id = client_id;
}
pthread_mutex_unlock(&sentence->lock);
```

## Operations

### CREATE
```
NM → SS: CREATE <filename>
SS: Create empty file, add to file list
SS → NM: SUCCESS
```

### DELETE
```
NM → SS: DELETE <filename>
SS: Remove file from disk and memory
SS → NM: SUCCESS
```

### READ (Direct Client Connection)
```
Client → SS: READ <filename>
SS: Acquire read lock
SS → Client: <file_content>
SS: Release read lock
```

### WRITE (Direct Client Connection)
```
Client → SS: WRITE_LOCK <filename> <sentence_num>
SS → Client: LOCKED (or ERROR:Sentence is locked)

Client → SS: WRITE <filename> <sentence_num> <word_index> <content>
SS: Check lock, update word, reparse if delimiters added
SS → Client: SUCCESS

Client → SS: WRITE_UNLOCK <filename> <sentence_num>
SS → Client: UNLOCKED
```

### STREAM (Direct Client Connection)
```
Client → SS: STREAM <filename>
SS: For each word in file:
    Send word + "\n"
    Sleep 100ms
SS → Client: STOP
```

### UNDO
```
NM → SS: UNDO <filename>
SS: Pop from undo stack, restore file content
SS → NM: SUCCESS
```

### INFO
```
NM → SS: INFO <filename>
SS → NM: SIZE:<bytes> WORDS:<count> CHARS:<count>
```

## Sentence Parsing Algorithm

```c
1. Initialize sentence count = 0
2. Scan content for delimiters (. ! ?)
3. For each delimiter found:
   a. Extract text from last delimiter to current
   b. Create Sentence struct
   c. Count words in sentence
   d. Initialize sentence lock
   e. Increment sentence count
4. If no delimiters, treat entire content as one sentence
5. Update file totals (size, words, chars)
```

## Write Operation with Sentence Updates

```c
1. Acquire write lock on file
2. Save current content to undo stack
3. Validate sentence number and lock
4. Parse sentence into words
5. Update word at specified index
6. Check if new content contains delimiters
7. If delimiters found:
   a. Rebuild full file content
   b. Reparse into sentences
   c. Update sentence count
8. Save to disk
9. Release write lock
```

## File Persistence

### On Startup
```c
1. Ensure storage/ directory exists
2. Scan storage/ for files
3. For each file:
   a. Read content
   b. Parse into sentences
   c. Create FileEntry
   d. Add to file list
```

### On Write
```c
1. Rebuild content from sentences
2. Write to filepath in storage/
3. Update modification timestamp
```

## Error Handling

All operations return `ErrorCode`:
- `ERR_SUCCESS (0)`: Operation successful
- `ERR_FILE_NOT_FOUND (1)`: File doesn't exist
- `ERR_FILE_EXISTS (3)`: File already exists (CREATE)
- `ERR_FILE_LOCKED (4)`: Sentence locked by another client
- `ERR_INVALID_SENTENCE (10)`: Invalid sentence number
- `ERR_SYSTEM_ERROR (99)`: Internal error

## Building and Running

### Build
```bash
make storage_server
```

### Run
```bash
./storage_server <nm_ip> <nm_port> <client_port>

# Example
./storage_server 127.0.0.1 8080 9002
```

### Multiple Storage Servers
```bash
# Terminal 1
./storage_server 127.0.0.1 8080 9002

# Terminal 2
./storage_server 127.0.0.1 8080 9003

# Terminal 3
./storage_server 127.0.0.1 8080 9004
```

## Logging

All operations are logged to `ss_log.txt`:
```
[2025-11-01 12:00:00] [INFO] Op=INIT Details=Storage Server initialized
[2025-11-01 12:00:05] [INFO] Op=REGISTER_NM Details=SS registered with ID 0
[2025-11-01 12:00:10] [INFO] Op=CREATE Details=File=test.txt
[2025-11-01 12:00:15] [INFO] Op=WRITE Details=File=test.txt Sentence=0 Word=0
```

## Testing

### Test Individual Operations
```bash
# In terminal 1: Start Name Server
./name_server 8080

# In terminal 2: Start Storage Server
./storage_server 127.0.0.1 8080 9002

# In terminal 3: Test with client
python3 test_client.py alice
> create test.txt
> info test.txt
```

### Integration Test
```bash
./test_integration.sh
```

## Performance Considerations

### Concurrency
- Reader-writer locks allow high read throughput
- Sentence-level locking enables parallel writes to different sentences
- File-level locks for consistency during reparsing

### Memory
- Files kept in memory for fast access
- Sentence arrays dynamically sized
- Undo history limited to 100 entries per stack

### Disk I/O
- Writes are synchronous (fwrite + fclose)
- Files loaded at startup
- No caching layer (could be added)

## Limitations

Current implementation:
- Max file size: 1MB (configurable)
- Max files: 1000 per server (configurable)
- Undo history: 100 entries (circular)
- No file compression
- No distributed replication

## Future Enhancements

- Chunk-based file storage for large files
- Async disk I/O with write-back cache
- File compression
- Replication between storage servers
- Hot file caching
- Load balancing hints to NM

## Integration with Name Server

### Registration
1. SS connects to NM on startup
2. Sends file inventory
3. NM assigns SS ID and stores mapping
4. Connection kept open for commands

### Command Forwarding
- NM forwards CREATE, DELETE, INFO, UNDO to SS
- NM returns SS info for READ, WRITE, STREAM (client connects directly)
- NM uses SS responses to update its metadata

### Failure Handling
- If SS disconnects, NM marks it as inactive
- Files on that SS become unavailable
- No automatic failover (future work)

## Security Considerations

- No authentication at SS level (handled by NM)
- Direct client connections trusted based on NM validation
- File access control enforced by NM, not SS
- Local filesystem permissions protect storage/

## Debugging

### Enable Debug Output
```bash
# Build with debug symbols
make debug

# Run with gdb
gdb ./storage_server
(gdb) run 127.0.0.1 8080 9002
```

### Check Logs
```bash
tail -f ss_log.txt
```

### Inspect Storage
```bash
ls -la storage/
cat storage/test.txt
```

## Troubleshooting

### "Address already in use"
```bash
lsof -i :9002
kill -9 <PID>
```

### "Failed to connect to Name Server"
- Ensure NM is running
- Check IP and port
- Verify network connectivity

### "File corruption"
- Check storage/ directory permissions
- Verify disk space
- Review ss_log.txt for errors

### "Lock deadlock"
- Clients should unlock sentences after writing
- Implement timeout in client
- Restart SS if lock state corrupted

## Code Structure

```
storage_server.h       - Data structures and declarations
storage_server.c       - Core functionality, sentence parsing, persistence
storage_server_ops.c   - File operations, locking, undo
storage_server_main.c  - Networking, NM registration, client handling
```

## Thread Model

```
Main Thread
├── NM Connection Thread (handle_nm_connection)
│   └── Processes commands from Name Server
│
└── Client Server Thread (start_client_server)
    └── Spawns thread per client connection
        └── Client Connection Thread (handle_client_connection)
            └── Processes READ, WRITE, STREAM from clients
```

All threads are detached and clean up automatically.
