# Multi-Client Test Changes Summary

## Changes Made to test_complete_system.py

All concurrent test cases have been updated to use **unique usernames** for each client to properly test multiple client handling. Previously, many tests reused "file_creator" which didn't properly test concurrent access from different users.

### 1. Test 4.2: Concurrent File Reads
**Before:** All 5 readers used username "file_creator"
**After:** Each reader uses unique username `reader_{idx}` (reader_0, reader_1, reader_2, etc.)
- Grants READ access to each reader before they try to read
- Tests true concurrent access from different users

### 2. Test 5.3: Concurrent Writes to Different Sentences
**Before:** All 5 writers used username "file_creator"
**After:** Each writer uses unique username `writer_{idx}` (writer_0, writer_1, etc.)
- Grants WRITE access to each writer
- Tests concurrent writes from different users to different sentences (10, 11, 12, 13, 14)

### 3. Test 5.4: Sentence Lock Serialization
**Before:** All 3 lockers used username "file_creator"
**After:** Each locker uses unique username `locker_{idx}` (locker_0, locker_1, locker_2)
- Grants WRITE access to each locker
- Tests lock contention on same sentence (100) from different users
- First should lock, others should be blocked

### 4. Test 13: Mixed Read/Write Operations
**Before:** All readers and writers used username "file_creator"
**After:** 
- Readers use unique usernames `mixed_reader_{idx}` (0-4)
- Writers use unique usernames `mixed_writer_{idx}` (0-2)
- Grants appropriate access (READ for readers, WRITE for writers)
- Tests true concurrent mixed operations from different users

### 5. Test 14: Stress Test
**Already correct:** Uses unique usernames `stress_user{idx}` for each of 20 clients

## What This Tests

These changes ensure the tests properly verify:

1. **Multiple Client Handling**: Each thread truly represents a different user/client
2. **Access Control**: Each user must be granted proper permissions
3. **Concurrent Access**: System must handle multiple different users accessing same files
4. **Lock Coordination**: Locks must work correctly across different client connections
5. **Thread Safety**: Name Server and Storage Server must properly handle concurrent requests from different clients

## Expected Behavior

With these changes:
- **Concurrent reads should all succeed** (multiple readers can read simultaneously)
- **Concurrent writes to different sentences should succeed** (no lock contention)
- **Concurrent writes to same sentence should serialize** (only one succeeds, others blocked)
- **Mixed operations should work** (readers and writers can operate concurrently on different sentences)

## Storage Server Changes

Also fixed `storage_server_main.c` to handle **empty file reads**:
- Previously: Empty files sent nothing, causing client timeout
- Now: Empty files send a newline `\n` so client knows response was received
- Ensures all READ operations get a response, even for empty files

## Code Quality Improvements

1. Each concurrent test now properly simulates real-world multi-user scenarios
2. Access control is properly tested (each user must be granted access)
3. Tests verify the system can handle the required specification of multiple concurrent clients
4. Better isolation between test cases (different usernames prevent interference)
