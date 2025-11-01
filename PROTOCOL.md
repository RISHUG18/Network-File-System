# Name Server Protocol Specification

## Version 1.0

## Overview
This document defines the communication protocol between the Name Server (NM), Storage Servers (SS), and Clients in the LangOS Distributed File System.

## Connection Types

### 1. Storage Server Connection
- **Purpose**: Register SS with NM and maintain persistent connection
- **Initiator**: Storage Server
- **Lifecycle**: Long-lived (until SS disconnects)

### 2. Client Connection
- **Purpose**: Register client and process user requests
- **Initiator**: Client
- **Lifecycle**: Session-based (until client disconnects)

## Message Format

All messages follow the format:
```
COMMAND [ARG1] [ARG2] ... [ARGN]\n
```

All responses follow the format:
```
<error_code>:<message>\n
```

## Registration Messages

### Storage Server Registration

**Request:**
```
REGISTER_SS <nm_port> <client_port> <file_count> <file1> <file2> ...
```

**Parameters:**
- `nm_port`: Port for NM-SS communication
- `client_port`: Port for direct client connections
- `file_count`: Number of files on this SS (can be 0)
- `file1`, `file2`, ...: Paths of files stored on this SS

**Response:**
```
0:SS registered with ID <ss_id>
```

**Example:**
```
REGISTER_SS 9001 9002 2 /data/file1.txt /data/file2.txt
```

### Client Registration

**Request:**
```
REGISTER_CLIENT <username> <nm_port> <ss_port>
```

**Parameters:**
- `username`: Unique username for this client
- `nm_port`: Client's port for NM communication
- `ss_port`: Client's port for SS communication

**Response:**
```
0:Client registered with ID <client_id>
```

**Example:**
```
REGISTER_CLIENT alice 7001 7002
```

## Client Commands

### VIEW - List Files

**Command:**
```
VIEW [flags]
```

**Flags:**
- `-a`: Show all files (not just accessible ones)
- `-l`: Show detailed listing
- `-al` or `-la`: Combine both flags

**Response:**
```
0:<file_list>
```

**Example:**
```
VIEW -al

Response:
0:OWNER      SIZE  WORDS CHARS LAST_ACCESS         FILENAME
------------------------------------------------------------
alice      1234    100    500 2025-11-01 10:30:00 file1.txt
bob        5678    250   1200 2025-11-01 11:00:00 file2.txt
```

### CREATE - Create File

**Command:**
```
CREATE <filename>
```

**Response:**
```
0:File '<filename>' created successfully
```

**Error Codes:**
- `3`: File already exists
- `5`: No storage server available

**Example:**
```
CREATE myfile.txt
```

### DELETE - Delete File

**Command:**
```
DELETE <filename>
```

**Response:**
```
0:File '<filename>' deleted successfully
```

**Error Codes:**
- `1`: File not found
- `9`: Permission denied (not owner)

**Example:**
```
DELETE myfile.txt
```

### READ - Read File

**Command:**
```
READ <filename>
```

**Response:**
```
0:SS_INFO <ss_ip> <ss_port>
```

Client should then connect to the specified SS to read the file.

**Error Codes:**
- `1`: File not found
- `2`: Unauthorized (no read access)
- `5`: Storage server not available

**Example:**
```
READ myfile.txt

Response:
0:SS_INFO 192.168.1.100 9002
```

### WRITE - Write to File

**Command:**
```
WRITE <filename> <sentence_number>
```

**Response:**
```
0:SS_INFO <ss_ip> <ss_port>
```

Client should then connect to the specified SS to perform write operations.

**Error Codes:**
- `1`: File not found
- `9`: Permission denied (no write access)
- `5`: Storage server not available

**Example:**
```
WRITE myfile.txt 1

Response:
0:SS_INFO 192.168.1.100 9002
```

### INFO - File Information

**Command:**
```
INFO <filename>
```

**Response:**
```
0:File: <filename>
Owner: <owner>
Size: <size> bytes
Word Count: <words>
Character Count: <chars>
Created: <timestamp>
Last Modified: <timestamp>
Last Accessed: <timestamp>
Storage Server: <ss_id>
Your Access: <access_level>
[Access Control List: ...]
```

**Error Codes:**
- `1`: File not found
- `2`: Unauthorized

**Example:**
```
INFO myfile.txt
```

### STREAM - Stream File

**Command:**
```
STREAM <filename>
```

**Response:**
```
0:SS_INFO <ss_ip> <ss_port>
```

Client should then connect to the specified SS which will stream the file word-by-word with 0.1s delay.

**Error Codes:**
- `1`: File not found
- `2`: Unauthorized
- `5`: Storage server not available

**Example:**
```
STREAM myfile.txt
```

### EXEC - Execute File

**Command:**
```
EXEC <filename>
```

**Response:**
```
0:Exit code: <code>
Output:
<command_output>
```

**Note:** Execution happens on the Name Server. The file content is retrieved from SS and executed as shell commands.

**Error Codes:**
- `1`: File not found
- `2`: Unauthorized
- `99`: Execution failed

**Example:**
```
EXEC script.sh
```

### UNDO - Undo Last Change

**Command:**
```
UNDO <filename>
```

**Response:**
```
0:Last change to '<filename>' undone
```

**Error Codes:**
- `1`: File not found
- `9`: Permission denied (no write access)
- `5`: Storage server not available

**Example:**
```
UNDO myfile.txt
```

### LIST - List Users

**Command:**
```
LIST
```

**Response:**
```
0:Registered Users:
  alice (@192.168.1.10)
  bob (@192.168.1.11)
  ...
```

**Example:**
```
LIST
```

### ADDACCESS - Grant Access

**Command:**
```
ADDACCESS <-R|-W> <filename> <username>
```

**Flags:**
- `-R`: Grant read access only
- `-W`: Grant write access (includes read)

**Response:**
```
0:Access granted to <username> for file '<filename>'
```

**Error Codes:**
- `1`: File not found
- `9`: Permission denied (not owner)

**Example:**
```
ADDACCESS -W myfile.txt bob
```

### REMACCESS - Revoke Access

**Command:**
```
REMACCESS <filename> <username>
```

**Response:**
```
0:Access removed from <username> for file '<filename>'
```

**Error Codes:**
- `1`: File not found
- `9`: Permission denied (not owner)
- `2`: User doesn't have access

**Example:**
```
REMACCESS myfile.txt bob
```

### QUIT/EXIT - Disconnect

**Command:**
```
QUIT
```
or
```
EXIT
```

**Response:**
```
0:Goodbye!
```

Connection is then closed by the server.

## Storage Server Commands (NM to SS)

These commands are sent from NM to SS:

### CREATE
```
CREATE <filename>

Response:
SUCCESS
```

### DELETE
```
DELETE <filename>

Response:
SUCCESS
```

### INFO
```
INFO <filename>

Response:
SIZE:<bytes> WORDS:<count> CHARS:<count>
```

### READ
```
READ <filename>

Response:
<file_content>
```

### UNDO
```
UNDO <filename>

Response:
SUCCESS
```

## Error Codes

| Code | Name | Description |
|------|------|-------------|
| 0 | ERR_SUCCESS | Operation successful |
| 1 | ERR_FILE_NOT_FOUND | File does not exist |
| 2 | ERR_UNAUTHORIZED | User not in ACL |
| 3 | ERR_FILE_EXISTS | File already exists |
| 4 | ERR_FILE_LOCKED | File/sentence locked |
| 5 | ERR_SS_NOT_FOUND | Storage server unavailable |
| 6 | ERR_CLIENT_NOT_FOUND | Client not found |
| 7 | ERR_INVALID_OPERATION | Invalid command/arguments |
| 8 | ERR_SS_DISCONNECTED | SS disconnected during operation |
| 9 | ERR_PERMISSION_DENIED | Insufficient permissions |
| 10 | ERR_INVALID_SENTENCE | Invalid sentence number |
| 99 | ERR_SYSTEM_ERROR | Internal server error |

## Connection Flow

### Storage Server Startup
1. SS starts and loads file inventory
2. SS connects to NM at known IP:port
3. SS sends `REGISTER_SS` with details
4. NM responds with SS ID
5. Connection remains open for future commands

### Client Session
1. Client starts and prompts for username
2. Client connects to NM at known IP:port
3. Client sends `REGISTER_CLIENT` with username
4. NM responds with client ID
5. Client enters command loop
6. For each command:
   - Client sends command to NM
   - NM validates and processes
   - NM responds with result or SS info
   - If SS info provided, client connects to SS
7. Client sends `QUIT` to end session

### Direct Client-SS Operations
For READ, WRITE, STREAM operations:
1. Client requests operation from NM
2. NM validates permissions
3. NM responds with `SS_INFO <ip> <port>`
4. Client establishes direct connection to SS
5. Client performs operation with SS
6. Client closes SS connection
7. Client continues session with NM

## Notes

- All strings are null-terminated
- Maximum message size: 4096 bytes
- Timeout recommendations:
  - Registration: 5 seconds
  - Normal operations: 30 seconds
  - Long operations (EXEC): 60 seconds
- Network byte order for binary data
- UTF-8 encoding for text content

## Security Considerations

- Username-based authentication (no passwords in v1.0)
- File ownership tracked
- ACL-based access control
- Command execution restricted to file owners
- All operations logged

## Future Extensions

Potential protocol extensions:
- Authentication tokens
- Binary file support
- Compression
- Encryption
- Batch operations
- Transaction support
- Versioning metadata
