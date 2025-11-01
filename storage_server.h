#ifndef STORAGE_SERVER_H
#define STORAGE_SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

// Constants
#define MAX_FILENAME 256
#define MAX_PATH 512
#define MAX_FILES 1000
#define MAX_SENTENCE_LOCKS 1000
#define BUFFER_SIZE 4096
#define MAX_CONTENT_SIZE (1024 * 1024)  // 1MB max file size
#define LOG_FILE "ss_log.txt"
#define UNDO_HISTORY_SIZE 100
#define STORAGE_DIR "./storage"

// Error Codes (matching NM)
typedef enum {
    ERR_SUCCESS = 0,
    ERR_FILE_NOT_FOUND = 1,
    ERR_UNAUTHORIZED = 2,
    ERR_FILE_EXISTS = 3,
    ERR_FILE_LOCKED = 4,
    ERR_INVALID_OPERATION = 7,
    ERR_INVALID_SENTENCE = 10,
    ERR_SYSTEM_ERROR = 99
} ErrorCode;

// Sentence structure
typedef struct Sentence {
    char* content;
    int length;
    int word_count;
    pthread_mutex_t lock;
    bool is_locked;
    int lock_holder_id;  // Client ID holding the lock
} Sentence;

// File structure
typedef struct FileEntry {
    char filename[MAX_FILENAME];
    char filepath[MAX_PATH];
    Sentence* sentences;
    int sentence_count;
    size_t total_size;
    int total_words;
    int total_chars;
    pthread_rwlock_t file_lock;  // Reader-writer lock for file
    time_t last_modified;
    time_t last_accessed;
} FileEntry;

// Undo history entry
typedef struct UndoEntry {
    char filename[MAX_FILENAME];
    char* content;  // Full file content snapshot
    time_t timestamp;
} UndoEntry;

// Undo stack
typedef struct UndoStack {
    UndoEntry entries[UNDO_HISTORY_SIZE];
    int top;
    pthread_mutex_t lock;
} UndoStack;

// Storage Server
typedef struct StorageServer {
    int ss_id;
    char nm_ip[16];
    int nm_port;
    int nm_socket_fd;
    int client_port;
    int client_socket_fd;
    
    // File management
    FileEntry* files[MAX_FILES];
    int file_count;
    pthread_mutex_t files_lock;
    
    // Undo management
    UndoStack undo_stack;
    
    // Logging
    FILE* log_file;
    pthread_mutex_t log_lock;
    
    // Running state
    bool is_running;
} StorageServer;

// Connection thread arguments
typedef struct ConnectionArgs {
    StorageServer* ss;
    int socket_fd;
    struct sockaddr_in addr;
} ConnectionArgs;

// Function Declarations

// Initialization
StorageServer* init_storage_server(const char* nm_ip, int nm_port, int client_port);
void destroy_storage_server(StorageServer* ss);
bool register_with_nm(StorageServer* ss);
void start_client_server(StorageServer* ss);

// File operations
FileEntry* create_file(StorageServer* ss, const char* filename);
FileEntry* find_file(StorageServer* ss, const char* filename);
ErrorCode delete_file(StorageServer* ss, const char* filename);
ErrorCode read_file(StorageServer* ss, const char* filename, char* content, size_t* size);
ErrorCode write_sentence(StorageServer* ss, const char* filename, int sentence_num, 
                        int word_index, const char* new_content, int client_id);
ErrorCode lock_sentence(StorageServer* ss, const char* filename, int sentence_num, int client_id);
ErrorCode unlock_sentence(StorageServer* ss, const char* filename, int sentence_num, int client_id);

// Sentence parsing
void parse_sentences(FileEntry* file, const char* content);
void rebuild_file_content(FileEntry* file, char* content);
int count_words(const char* text);
bool is_sentence_delimiter(char c);

// Undo operations
void push_undo(StorageServer* ss, const char* filename, const char* content);
ErrorCode pop_undo(StorageServer* ss, const char* filename, char* content);
ErrorCode handle_undo(StorageServer* ss, const char* filename);

// Streaming
ErrorCode stream_file(StorageServer* ss, int client_fd, const char* filename);

// File info
ErrorCode get_file_info(StorageServer* ss, const char* filename, 
                       size_t* size, int* words, int* chars);

// Persistence
bool save_file_to_disk(FileEntry* file);
bool load_file_from_disk(StorageServer* ss, const char* filename);
void load_all_files(StorageServer* ss);

// Networking
void* handle_nm_connection(void* arg);
void* handle_client_connection(void* arg);
void send_response(int socket_fd, const char* message);

// Logging
void log_message(StorageServer* ss, const char* level, const char* operation, 
                const char* details);

// Utilities
const char* error_to_string(ErrorCode error);
void parse_command(const char* command, char* cmd, char* args[], int* arg_count);
void ensure_storage_dir();

#endif // STORAGE_SERVER_H
