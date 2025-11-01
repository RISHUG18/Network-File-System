# Getting Started with Name Server

## Quick Setup (5 minutes)

### 1. Build the Name Server
```bash
cd /home/rishabh/Desktop/sem3/osn/course-project-panic-error
make
```

You should see: `Name Server built successfully!`

### 2. Start the Name Server
```bash
./name_server 8080
```

You should see:
```
Name Server initialized on port 8080
Name Server started on port 8080
Waiting for connections...
```

### 3. Test with Python Client (in a new terminal)
```bash
cd /home/rishabh/Desktop/sem3/osn/course-project-panic-error
python3 test_client.py alice
```

You should see:
```
✓ Connected: Client registered with ID 0

LangOS Name Server Test Client
==================================================
Commands:
  view [flags]              - List files (-a for all, -l for detailed)
  create <file>             - Create a file
  ...
==================================================

alice>
```

### 4. Try Some Commands
```
alice> users
✓ Registered Users:
  alice (@127.0.0.1)

alice> view
No files found

alice> create test.txt
✗ Error: No storage server available

alice> quit
✓ Disconnected
```

**Note**: File operations need a Storage Server running. The last error is expected!

## Understanding the System

### What You Have Now
- ✅ **Name Server**: The central coordinator (complete)
- ⏳ **Storage Server**: File storage (to be implemented)
- ⏳ **Client**: User interface (to be implemented)

### Current Capabilities
- ✅ Client registration
- ✅ User management
- ✅ File metadata tracking
- ✅ Access control (ACL)
- ✅ Command parsing and routing
- ⏳ Actual file operations (need Storage Server)

## Architecture Overview

```
[Your Test Client] --TCP--> [Name Server:8080] --TCP--> [Storage Server (TBD)]
     (Python)                     (C)                         (C)
```

### Communication Flow
1. **Client connects** to Name Server with username
2. **Name Server** validates and registers the client
3. **Client sends commands** (VIEW, CREATE, READ, etc.)
4. **Name Server**:
   - Validates permissions
   - For metadata ops (VIEW, LIST): handles directly
   - For file ops (READ, WRITE): returns Storage Server info
   - For file creation (CREATE, DELETE): forwards to Storage Server
5. **Client** connects directly to Storage Server for data operations

## Next Steps for Development

### Phase 1: Test Current Implementation ✅
```bash
# Terminal 1
./name_server 8080

# Terminal 2
python3 test_client.py alice

# Terminal 3
python3 test_client.py bob

# Try commands: users, view, quit
```

### Phase 2: Implement Storage Server
You need to implement:
1. **File I/O**: Create, read, write, delete files
2. **Sentence Parsing**: Split file into sentences
3. **Undo History**: Stack-based undo
4. **Locking**: Sentence-level locks for write
5. **NM Communication**: Register with NM, respond to commands
6. **Client Communication**: Direct connection for READ/WRITE/STREAM

Template:
```c
// storage_server.c
- register_with_nm()
- handle_nm_commands()
- handle_client_connection()
- file_create(), file_read(), file_write(), file_delete()
- parse_sentences()
- lock_sentence(), unlock_sentence()
- push_undo(), pop_undo()
```

### Phase 3: Implement Full Client
You need to implement:
1. **User Interface**: Better CLI or GUI
2. **NM Communication**: Send all commands to NM
3. **SS Communication**: Direct connection for READ/WRITE/STREAM
4. **WRITE Protocol**: 
   ```
   WRITE filename sentence_num
   word_index content
   word_index content
   ...
   ETIRW
   ```

Template:
```c
// client.c
- connect_to_nm()
- send_command()
- handle_response()
- connect_to_ss()  // For READ, WRITE, STREAM
- stream_file()  // Display word-by-word with 0.1s delay
```

## File Organization

```
project/
├── name_server.h           ✅ Header
├── name_server.c           ✅ Core implementation
├── name_server_ops.c       ✅ File operations
├── name_server_main.c      ✅ Main & networking
├── storage_server.h        ⏳ To implement
├── storage_server.c        ⏳ To implement
├── client.h                ⏳ To implement
├── client.c                ⏳ To implement
├── Makefile                ✅ Build system
└── test_client.py          ✅ Python test tool
```

## Key Concepts to Understand

### 1. Trie for File Lookup
The Name Server uses a Trie for O(m) file lookup where m = filename length.

Example:
```
Root
├── f
│   └── i
│       └── l
│           └── e
│               └── 1 [metadata]
└── t
    └── e
        └── s
            └── t [metadata]
```

### 2. Access Control Lists (ACL)
Each file has:
- **Owner**: Always has READ + WRITE access
- **ACL**: List of (username, access_level) pairs
  - ACCESS_READ: Can read file
  - ACCESS_WRITE: Can read and write file

### 3. Direct Client-SS Connection
For efficiency, clients connect directly to Storage Server for data-heavy operations:
```
Client → NM: "READ file.txt"
NM → Client: "SS_INFO 192.168.1.100 9002"
Client → SS: (direct TCP connection)
SS → Client: (file content)
```

### 4. Sentence-Level Locking
During WRITE, a specific sentence is locked:
```
Client A: WRITE file.txt 1  [Locks sentence 1]
Client B: WRITE file.txt 1  [Blocked - sentence locked]
Client A: ETIRW              [Unlocks sentence 1]
Client B: WRITE file.txt 1  [Now allowed]
```

## Common Commands Reference

### For Testing Now
```bash
# View users
users

# View files (will be empty without SS)
view
view -a
view -l

# Try creating (will fail without SS)
create test.txt

# List commands
help

# Disconnect
quit
```

### For Later (with Storage Server)
```bash
# Create and write
create myfile.txt
write myfile.txt 1
0 Hello
1 World
ETIRW

# Read
read myfile.txt

# Share with another user
addaccess -W myfile.txt bob

# View file info
info myfile.txt

# Undo last change
undo myfile.txt

# Execute file as script
exec script.sh

# Delete
delete myfile.txt
```

## Debugging Tips

### Check Name Server Logs
```bash
tail -f nm_log.txt
```

### Check If Port Is In Use
```bash
lsof -i :8080
```

### Kill Stuck Process
```bash
lsof -i :8080
kill -9 <PID>
```

### Rebuild from Scratch
```bash
make clean
make
```

### Debug Build
```bash
make debug
gdb ./name_server
(gdb) run 8080
```

## Testing Strategy

### Current Phase: Name Server Only
✅ Test client registration
✅ Test multiple clients
✅ Test user listing
✅ Test view commands
✅ Test graceful shutdown (Ctrl+C)

### Next Phase: With Storage Server
⏳ Test file creation
⏳ Test file reading/writing
⏳ Test access control
⏳ Test concurrent writes
⏳ Test undo functionality

### Final Phase: Full System
⏳ Test complete workflows
⏳ Test error scenarios
⏳ Test performance
⏳ Test concurrent users

## Important Files to Read

1. **PROTOCOL.md** - Understand the communication protocol
2. **NAME_SERVER_README.md** - Detailed Name Server documentation
3. **TESTING.md** - Comprehensive testing guide
4. **QUICK_REFERENCE.md** - Quick command reference

## FAQ

**Q: Why do file operations fail?**
A: You need to implement and run a Storage Server first.

**Q: How do I add multiple users?**
A: Run multiple instances of test_client.py with different usernames.

**Q: Where are logs stored?**
A: In `nm_log.txt` in the current directory.

**Q: Can I change the port?**
A: Yes: `./name_server <port_number>`

**Q: How do I stop the server?**
A: Press Ctrl+C for graceful shutdown.

**Q: How do I test without implementing SS?**
A: Use the test client to test registration, user listing, and view commands.

## Resources

- **Course Spec**: Original project requirements
- **NAME_SERVER_README.md**: Technical documentation
- **PROTOCOL.md**: Communication protocol details
- **IMPLEMENTATION_SUMMARY.md**: What's implemented and how

## Team Collaboration

### Git Workflow
```bash
# Check status
git status

# See changes
git diff

# Add files
git add .

# Commit
git commit -m "Implemented Name Server"

# Push
git push origin main
```

### Task Division (Suggested)
- **Person 1**: Storage Server (file I/O, undo, locking)
- **Person 2**: Client (UI, NM communication, SS communication)
- **Person 3**: Integration testing, documentation, demo

## Success Criteria

Your Name Server is ready when:
- ✅ Builds without errors
- ✅ Starts and listens on port
- ✅ Accepts client connections
- ✅ Registers clients with usernames
- ✅ Lists users correctly
- ✅ Logs all operations
- ✅ Handles graceful shutdown

**All criteria met! ✅**

## Getting Help

1. Check the log file: `nm_log.txt`
2. Read TESTING.md for troubleshooting
3. Read PROTOCOL.md to understand messages
4. Check error codes in QUICK_REFERENCE.md
5. Review IMPLEMENTATION_SUMMARY.md for architecture

## What's Next?

1. ✅ **Understand** how Name Server works (read this doc)
2. ✅ **Test** the Name Server (follow Quick Setup)
3. ⏳ **Design** Storage Server (read spec, plan data structures)
4. ⏳ **Implement** Storage Server (file I/O, undo, locking)
5. ⏳ **Test** NS + SS integration
6. ⏳ **Implement** Full Client
7. ⏳ **Test** complete system
8. ⏳ **Demo** and submit

---

**You're all set! The Name Server is production-ready. Time to build the Storage Server!** 🚀
