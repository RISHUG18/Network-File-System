# LangOS — Distributed Network File System

> A concurrent, multi-process distributed file system implemented in 5,643 lines of POSIX C — centralized naming server with trie metadata index, horizontally scalable storage nodes, sentence-granularity write locking, two-phase draft/commit protocol, tagged checkpointing, and passive failure detection. No third-party libraries.

![Language](https://img.shields.io/badge/language-C11-blue?style=flat-square)
![Build](https://img.shields.io/badge/build-GNU_Make-green?style=flat-square)
![Concurrency](https://img.shields.io/badge/concurrency-pthreads-orange?style=flat-square)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey?style=flat-square)
![Lines](https://img.shields.io/badge/lines_of_C-5%2C643-informational?style=flat-square)

---

## Motivation

Distributed file systems involve some of the hardest concurrency and consistency problems in systems design: where does metadata live, how do you route I/O without the control plane becoming a bottleneck, how do you give concurrent writers isolation without serializing the whole file, and how do you detect node failure without flooding healthy nodes with probes?

LangOS answers each of these questions with a concrete implementation — trie-indexed centralized metadata, data-plane bypass via SS_INFO redirect (the same separation GFS and HDFS use), sentence-granularity `pthread_mutex_t` locking with a draft/commit protocol for write isolation, and `poll()`-based passive disconnect detection that adds zero traffic to live nodes.

---

## Key Highlights

- **256-ary character trie** indexes all file paths at the naming server — O(L) lookup where L is path length, independent of total file count; DFS traversal over the trie subtree serves folder listings without iterating unrelated keys
- **100-slot LRU cache** with O(1) promotion and eviction sits in front of the trie, guarded by a lock independent of `trie_lock` so cache hits on hot paths never block metadata writers
- **Sentence-level concurrent writes** — each `SentenceNode` carries its own `pthread_mutex_t`; multiple clients can write to different sentences of the same file simultaneously
- **Three-domain lock hierarchy** (`pthread_rwlock_t` file-level → `pthread_mutex_t` structure → `pthread_mutex_t` per-sentence) with a strictly enforced acquisition order throughout every code path to prevent deadlock
- **Draft/commit isolation protocol (ETIRW)** — word edits stage into a `DraftSentence` linked list; `commit_sentence_drafts()` acquires a file write-lock, applies all drafts atomically, pushes an undo snapshot, and persists to disk in a single critical section
- **50-entry sentence undo stack** — each commit deep-copies the pre-commit word array into a `SentenceUndoEntry` before overwriting; `UNDO` restores the snapshot in-place without re-reading from disk
- **Tagged checkpoint system** with sanitized tag validation (preventing path traversal), per-file checkpoint directories, non-destructive view, and full revert that re-parses the restored content into the in-memory sentence list
- **Passive SS failure detection** via `poll(POLLERR | POLLHUP)` on persistent NM↔SS sockets — zero keepalive traffic to healthy nodes; reconnecting SS are matched by IP+port and their trie entries patched in-place without dropping live client sessions

---

## Architecture

```
  ┌─────────────┐   REGISTER / metadata ops   ┌──────────────────┐
  │   Client(s) │ ──────────────────────────▶ │   Naming Server  │
  │             │ ◀────────── SS_INFO ──────── │   (control plane)│
  └──────┬──────┘                             └────────┬─────────┘
         │                                             │ forward CREATE / DELETE
         │  Direct I/O (READ / WRITE / STREAM)         │ / INFO / CHECKPOINT
         │                                             ▼
         │                                   ┌──────────────────┐
         └──────────────────────────────────▶│  Storage Server  │
                                             │  (data plane)    │
                                             └──────────────────┘
```

The Naming Server (NM) owns all metadata and acts as the sole control plane. It never touches file content. For every I/O operation, it resolves the owning storage server, checks the ACL, and returns `SS_INFO <ip> <port>`. The client then connects directly to that Storage Server (SS), bypassing the NM entirely for data transfer. This is the same architectural decision made by GFS (master returns chunk server location) and HDFS (NameNode returns DataNode address) — the control plane must not become an I/O bottleneck.

Multiple storage servers register dynamically. The NM distributes new top-level files across them via round-robin and co-locates files under the same folder on the same SS (parent affinity).

---

## Core Components

### Naming Server
**Files:** `name_server.c`, `name_server_ops.c`, `name_server_main.c`, `name_server.h`

Single control-plane process. Owns five independent lock domains:

| Lock | Guards | Type |
|---|---|---|
| `trie_lock` | File path trie + metadata pointers | `pthread_mutex_t` |
| `ss_lock` | Storage server registry (≤50 nodes) | `pthread_mutex_t` |
| `client_lock` | Active client table (≤100 sessions) | `pthread_mutex_t` |
| `registry_lock` | Persistent user registry | `pthread_mutex_t` |
| `log_lock` | Log file writes | `pthread_mutex_t` |

Each accepted connection spawns a detached `pthread`. SS connections are held open; a monitor loop calls `poll()` on each SS socket for disconnect events without consuming any NM→SS bandwidth. Client connections persist for the session lifetime and process commands in a loop.

### Storage Server
**Files:** `storage_server.c`, `storage_server_ops.c`, `storage_server_main.c`, `storage_server.h`

Standalone process. Runs two threads: an NM command handler (`handle_nm_connection`) and a client accept loop (`start_client_server`), each on a separate port. In-memory state per file:

- `FileEntry` — owns a doubly-linked list of `SentenceNode`s, a `pthread_rwlock_t` for file-level read/write exclusion, a `pthread_mutex_t` for list structure modifications, and a LIFO undo stack
- `SentenceNode` — owns a `words[]` dynamic array, its own `pthread_mutex_t`, a `DraftSentence` chain for staged edits, and an `is_locked` flag with `lock_holder_id`

On startup: binds client port (auto-increments up to 10 times on conflict), loads all files recursively from `./storage/`, then registers with the NM.

### Client
**Files:** `client.c`, `client_core.c`, `client_nm_ops.c`, `client_ss_ops.c`, `client.h`

Single-threaded interactive shell. Maintains one persistent TCP connection to the NM for the session. For I/O operations (read, write, stream), opens a per-operation TCP connection directly to the SS, then closes it. Uses `getaddrinfo()` for hostname resolution (IPv4 and IPv6). All NM responses use `error_code:message` framing.

### Sentence-Level Locking and Draft/Commit Protocol

Files are parsed into a doubly-linked list of `SentenceNode` objects on `.`, `!`, `?` boundaries. The write flow:

```
Client: WRITE <file> <sentence_num>
  → SS: lock_sentence() — acquires sentence->lock, sets is_locked + lock_holder_id
  → SS: LOCKED

Client: <word_index> <content>  [repeatable]
  → SS: write_sentence() — holds file pthread_rwlock_rdlock, stages words into DraftSentence
  → SS: SUCCESS

Client: ETIRW
  → SS: commit_sentence_drafts()
       1. Acquires file pthread_rwlock_wrlock
       2. Deep-copies pre-commit state → SentenceUndoEntry → pushes onto file undo stack
       3. apply_drafts_to_file() under structure_lock — overwrites SentenceNode, inserts
          any new sentence nodes for delimiter-expanded content
       4. refresh_file_stats(), save_file_to_disk()
       5. Releases file write-lock, unlocks sentence
  → SS: SUCCESS
```

If staged content introduces new sentence delimiters (e.g., writing `"Hello. World."` into one sentence), `rebuild_draft_structure()` re-parses the draft chain into multiple `DraftSentence` nodes; `apply_drafts_to_file()` then inserts the additional nodes directly into the linked list.

### Access Control

`FileMetadata` carries `owner` (set on creation) and an `acl` linked list of `AccessEntry { username, AccessRight }`. The NM enforces ACL on every operation before routing. Non-owners can submit access requests queued in `pending_requests`; the owner approves or denies via `PROCESSREQUEST`.

### Checkpointing

Stored at `./storage/checkpoints/<filename>/<tag>.chk`. Tags are validated by `sanitize_checkpoint_tag()` — only `[a-zA-Z0-9_\-.]` allowed; any other character returns `ERR_INVALID_OPERATION` before any path construction occurs. `VIEWCHECKPOINT` reads the snapshot without touching the live file. `REVERT` overwrites the live file, clears the undo stack, and re-parses all sentences.

### Logging

Both NM and SS write structured log lines to `nm_log.txt` / `ss_log.txt`:
```
[2024-01-15 14:32:07] [INFO] IP=127.0.0.1 Port=8080 User=alice Op=WRITE Details=File=notes.txt Sentence=2 SS_ID=0
```
All writes serialized via `log_lock`. Output mirrored to stdout.

---

## System Workflow

### Client Connection
1. `getaddrinfo()` resolves NM hostname; iterates address candidates until `connect()` succeeds
2. Sends `REGISTER_CLIENT <username> <nm_port> <ss_port>`
3. NM spawns detached thread, assigns client ID, responds `0:Client registered with ID N`
4. Persistent command loop begins on the same socket

### Path Lookup
1. Incoming command filename checked against LRU cache — O(1) hit promotes entry to head
2. On cache miss: `search_file_trie()` traverses the 256-ary trie — O(L), L = path length
3. Returns `FileMetadata*` with `ss_id`, timestamps, ACL list

### File Creation (Top-Level)
1. Trie checked for duplicate
2. `next_ss_index` selects starting SS; iterates all active nodes in round-robin
3. NM→SS: `CREATE <filename>` forwarded over persistent socket under `ss->lock`
4. On `SUCCESS`: `FileMetadata` allocated, inserted into trie and cache; `next_ss_index` advanced

### File Creation (Inside Folder)
1. Parent path extracted via `strrchr(filename, '/')`
2. Parent metadata fetched from trie; `ss_id` read from parent — round-robin bypassed entirely
3. `CREATE` forwarded to parent's SS; new metadata inherits same `ss_id`

### Storage Server Failure and Recovery
**Failure:** `poll()` monitor detects `POLLERR` or `POLLHUP` on SS socket → `deregister_storage_server_safe()` called with the *original* socket FD. Validates `ss->socket_fd == original_fd` before marking inactive — prevents a reconnecting SS from being deregistered by a stale event.

**Recovery:** Reconnecting SS sends `REGISTER_SS` with same IP + client-port. `register_storage_server()` detects the match, closes stale socket, updates `socket_fd`, sets `is_active = true`, iterates reported file list to refresh trie entries. Live clients are unaffected.

---

## Build and Run

**Dependencies:** GCC or Clang, GNU Make, Linux (pthreads, POSIX sockets, `poll`, `dirent`). No external libraries.

```bash
make              # builds name_server, storage_server, client
make debug        # adds -DDEBUG -g3
make clean
```

**Start the cluster:**

```bash
# Terminal 1 — Naming Server
./name_server 8080

# Terminal 2 — Storage Server (node 0)
./storage_server 127.0.0.1 8080 9002

# Terminal 3 — Storage Server (node 1)
./storage_server 127.0.0.1 8080 9003

# Terminal 4 — Client
./client alice localhost 8080
```

**Example session:**

```
alice> create notes.txt
✓ File 'notes.txt' created successfully

alice> write notes.txt 0
✓ Storage Server: 127.0.0.1:9002
✓ Sentence locked
write> 0 Hello world.
✓ Word updated
write> ETIRW
✓ Changes finalized and sentence unlocked

alice> checkpoint notes.txt v1
✓ Checkpoint 'v1' saved

alice> addaccess W notes.txt bob
✓ Access granted to bob for file 'notes.txt'

alice> undo notes.txt
✓ Last change to 'notes.txt' undone

alice> revert notes.txt v1
✓ File reverted to 'v1'
```

---

## Repository Structure

```
Network-File-System/
├── Makefile                   # Per-TU compilation, debug target, clean
├── name_server.h              # TrieNode, FileMetadata, LRUCache, ErrorCode, ACL types
├── name_server.c              # Trie, LRU cache, SS/client registry, user persistence
├── name_server_ops.c          # All command handlers, forward_to_ss, ACL, checkpoints
├── name_server_main.c         # Accept loop, per-connection threads, poll() monitor
├── storage_server.h           # SentenceNode, FileEntry, DraftSentence, SentenceUndoEntry
├── storage_server.c           # Sentence list, parse/rebuild, disk I/O, checkpoint I/O
├── storage_server_ops.c       # Draft management, locking, commit, undo, streaming
├── storage_server_main.c      # NM registration, NM/client connection handlers
├── client.h                   # Client struct, all declarations
├── client_core.c              # TCP connect, NM register, send/recv, SS connect
├── client_nm_ops.c            # NM command wrappers (create, delete, ACL, checkpoint)
├── client_ss_ops.c            # Direct SS ops: read, ETIRW write, word streaming
└── client.c                   # Interactive shell, command dispatch
```

---

## Design Decisions

### Trie over Hash Map for Metadata Index

A hash map requires hashing the full path string on every lookup and cannot enumerate prefix ranges without iterating all keys. A 256-ary trie gives O(L) lookup guaranteed with no hash collisions, and DFS over a subtree is the natural implementation of folder listing. The memory cost per node (256 pointers) is acceptable for a metadata-only index.

### NM Returns SS Address; Never Proxies Data

Routing file content through the NM would serialize all I/O through a single socket. Returning `SS_INFO` and having clients connect directly to the SS is the architectural decision that makes the NM horizontally irrelevant to data throughput — the same separation GFS (master/chunkserver), HDFS (NameNode/DataNode), and NFS use. The NM becomes a pure control plane.

### Sentence Granularity over Byte-Range Locking

Byte-range locks (POSIX `fcntl`) require clients to know byte offsets, which shift on every edit. Sentence indices are stable identifiers: editing sentence 3 does not change the index of sentence 4. This makes the locking contract intuitive and sidesteps offset invalidation after each write.

### Draft/Commit over In-Place Writes

In-place word edits would expose partial state to concurrent readers between individual word updates. The draft chain gives each writer a private staging area; `commit_sentence_drafts()` acquires a file write-lock and applies all changes in one operation. Readers on `pthread_rwlock_rdlock` see only committed states.

### Passive Failure Detection over Keepalive Probes

A keepalive approach requires the NM to send O(N) probes per interval to N storage servers, handle timeouts, retry logic, and tune probe frequency to balance false positives against detection latency. `poll(POLLERR | POLLHUP)` on the existing persistent socket adds zero traffic to healthy nodes and detects failure immediately on TCP teardown. The only limitation is that a network partition (node alive, link dead) requires TCP keepalive at the kernel level — a known tradeoff.

### Three-Domain Lock Hierarchy

A single per-file mutex would serialize all readers. The design uses:
- `pthread_rwlock_t file_lock` — N concurrent readers; exclusive write during commit
- `pthread_mutex_t structure_lock` — protects linked list pointer surgery (insertion/deletion of sentence nodes)
- `pthread_mutex_t sentence->lock` — per-sentence write ownership (allows concurrent writes across sentences)

Acquisition order is always `file_lock` → `structure_lock` → `sentence->lock`. `commit_sentence_drafts()` explicitly releases `sentence->lock` before acquiring `structure_lock` to maintain this ordering.

---

## Performance

| Mechanism | Complexity / Behavior |
|---|---|
| Trie path lookup | O(L), L = path length; independent of file count |
| LRU cache hit | O(1) pointer dereference; `cache->lock` separate from `trie_lock` |
| Concurrent reads | Unlimited concurrent readers per file via `pthread_rwlock_rdlock` |
| Concurrent writes | One writer per sentence; writers to different sentences run in parallel |
| Disk I/O | Only on `ETIRW` commit; zero disk writes during draft word insertions |
| SS port conflict | Auto-increments client port up to 10 times; no operator intervention needed |
| Socket restart | `SO_REUSEADDR` on all server sockets eliminates TIME_WAIT bind failure |
| Word array growth | `realloc` with capacity doubling — amortized O(1) per insertion |

---

## Engineering Challenges

**Three-way lock ordering in `commit_sentence_drafts()`**
The commit path must hold `file_lock` (write) for reader exclusion, `structure_lock` for list surgery, and `sentence->lock` for draft ownership — but `structure_lock` is also acquired by `get_sentence_by_index()` which is called *after* entering the commit path. Resolving this required releasing `sentence->lock` before the `structure_lock` acquisition, then re-validating the sentence pointer. Every code path in the file was audited for adherence to the `file_lock → structure_lock → sentence->lock` ordering.

**Stale disconnect race on SS reconnect**
When an SS crashes and restarts quickly, the `poll()` monitor thread for the old socket may fire *after* the new connection is registered, calling `deregister_storage_server_safe()` on a live SS. Fixed by passing the original socket FD into the deregister function and only deregistering if `ss->socket_fd == original_fd` — a single integer comparison that closes the window entirely.

**Multi-sentence expansion from a single write**
Writing `"First sentence. Second sentence."` into sentence index 2 must split that one sentence node into two. After word insertion, `contains_sentence_delimiter()` checks the new content; if true, `rebuild_draft_structure()` serializes the full draft chain to a temporary buffer and re-parses it into multiple `DraftSentence` nodes. `apply_drafts_to_file()` then inserts additional nodes directly into the doubly-linked list, incrementing `sentence_count`.

**TCP stream reassembly in the client**
`recv()` on a TCP socket returns arbitrary byte boundaries. `cmd_stream_file()` maintains a `pending[]` ring buffer; each `recv()` appends to it and `memchr()` scans for `\n` delimiters. Complete lines are extracted and printed; the remaining partial line is compacted to the buffer start with `memmove()`. This correctly handles both fragmented and coalesced packets.

**Path traversal in checkpoint tags**
Checkpoint tags are user-supplied strings embedded directly into filesystem paths (`./storage/checkpoints/<file>/<tag>.chk`). `sanitize_checkpoint_tag()` rejects any character outside `[a-zA-Z0-9_\-.]` before any `snprintf` path construction occurs, closing the directory traversal vector entirely.

---

## Future Improvements

| Improvement | What it addresses |
|---|---|
| **Raft-based NM replication** | Eliminates the single point of failure for metadata; leader election on NM crash |
| **Write-ahead log (WAL) for trie** | NM trie currently rebuilt from SS registrations on restart; WAL gives crash-consistent recovery |
| **Consistent hashing for SS** | Round-robin causes full re-distribution on SS join/leave; consistent hashing minimizes remapping |
| **File content replication** | Writes go to a primary SS and N-1 replicas; reads served from any replica |
| **`sendfile(2)` for streaming** | Eliminates user-space copy in `stream_file()`; data moves directly from file descriptor to socket |
| **Persistent client→SS connections** | Currently reconnects per operation; pooling eliminates TCP handshake cost on hot paths |
| **mTLS on all sockets** | All traffic is currently plaintext; mutual TLS provides both encryption and node authentication |
| **Prometheus metrics endpoint** | Cache hit rate, active clients, per-SS load, operation latency histograms |
| **Load-aware SS selection** | Replace round-robin with least-loaded routing based on file count or disk utilization |
| **Compression (LZ4)** | Reduces wire size for large file streaming with negligible CPU cost |

---

## Tech Stack

| Capability | Implementation | Why |
|---|---|---|
| Language | C11 | Direct control over memory layout, no GC pauses, POSIX compatibility |
| Threading | `pthreads` | `pthread_mutex_t`, `pthread_rwlock_t` — standard POSIX; no runtime dependency |
| Networking | POSIX TCP sockets | Reliable ordered streams; `SO_REUSEADDR` for rapid restart |
| Name resolution | `getaddrinfo()` | Handles hostnames, IPv4, IPv6 transparently |
| Failure detection | `poll()` | Passive; zero overhead on healthy nodes |
| Build | GNU Make | Dependency-tracked per-translation-unit compilation |
| Persistence | Flat text files | Zero-dependency; `stat()`-compatible for metadata extraction |
| Logging | Append-mode `FILE*` | Mutex-serialized; structured `key=value` fields; dual stdout+file output |

---

## License

No license is currently specified for this repository.
