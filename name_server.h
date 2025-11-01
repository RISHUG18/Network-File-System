#ifndef NAME_SERVER_H
#define NAME_SERVER_H

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

// Constants
#define MAX_FILENAME 256
#define MAX_USERNAME 64
#define MAX_PATH 512
#define MAX_IP_LEN 16
#define MAX_CLIENTS 100
#define MAX_SS 50
#define MAX_FILES_PER_SS 1000
#define BUFFER_SIZE 4096
#define LOG_FILE "nm_log.txt"
#define CACHE_SIZE 100

// Error Codes
typedef enum {
    ERR_SUCCESS = 0,
    ERR_FILE_NOT_FOUND = 1,
    ERR_UNAUTHORIZED = 2,
    ERR_FILE_EXISTS = 3,
    ERR_FILE_LOCKED = 4,
    ERR_SS_NOT_FOUND = 5,
    ERR_CLIENT_NOT_FOUND = 6,
    ERR_INVALID_OPERATION = 7,
    ERR_SS_DISCONNECTED = 8,
    ERR_PERMISSION_DENIED = 9,
    ERR_INVALID_SENTENCE = 10,
    ERR_SYSTEM_ERROR = 99
} ErrorCode;

// Access Rights
typedef enum {
    ACCESS_NONE = 0,
    ACCESS_READ = 1,
    ACCESS_WRITE = 2  // Write implies read
} AccessRight;

// Trie Node for efficient file search
typedef struct TrieNode {
    struct TrieNode* children[256];  // ASCII characters
    bool is_end_of_word;
    void* file_metadata;  // Pointer to FileMetadata
} TrieNode;

// File Metadata
typedef struct FileMetadata {
    char filename[MAX_FILENAME];
    char owner[MAX_USERNAME];
    int ss_id;  // Storage server storing this file
    time_t created_time;
    time_t last_modified;
    time_t last_accessed;
    size_t file_size;
    int word_count;
    int char_count;
    // Access Control List
    struct AccessEntry {
        char username[MAX_USERNAME];
        AccessRight access;
        struct AccessEntry* next;
    }* acl;
} FileMetadata;

// Storage Server Info
typedef struct StorageServer {
    int id;
    char ip[MAX_IP_LEN];
    int nm_port;
    int client_port;
    int socket_fd;
    bool is_active;
    char** files;  // Array of file paths
    int file_count;
    pthread_mutex_t lock;
} StorageServer;

// Client Info
typedef struct Client {
    int id;
    char username[MAX_USERNAME];
    char ip[MAX_IP_LEN];
    int nm_port;
    int ss_port;
    int socket_fd;
    bool is_active;
    time_t connected_time;
} Client;

// Cache Entry for recent searches
typedef struct CacheEntry {
    char filename[MAX_FILENAME];
    FileMetadata* metadata;
    time_t timestamp;
    struct CacheEntry* next;
    struct CacheEntry* prev;
} CacheEntry;

// LRU Cache
typedef struct LRUCache {
    CacheEntry* head;
    CacheEntry* tail;
    int size;
    int capacity;
    pthread_mutex_t lock;
} LRUCache;

// Name Server
typedef struct NameServer {
    int nm_port;
    int socket_fd;
    
    // Data structures
    TrieNode* file_trie;
    StorageServer* storage_servers[MAX_SS];
    Client* clients[MAX_CLIENTS];
    int ss_count;
    int client_count;
    
    // Cache
    LRUCache* cache;
    
    // Locks
    pthread_mutex_t ss_lock;
    pthread_mutex_t client_lock;
    pthread_mutex_t trie_lock;
    pthread_mutex_t log_lock;
    
    // Logging
    FILE* log_file;
    
    // Running state
    bool is_running;
} NameServer;

// Function Declarations

// Initialization
NameServer* init_name_server(int port);
void destroy_name_server(NameServer* nm);
void start_name_server(NameServer* nm);

// Trie operations
TrieNode* create_trie_node();
void insert_file_trie(TrieNode* root, const char* filename, FileMetadata* metadata);
FileMetadata* search_file_trie(TrieNode* root, const char* filename);
void delete_file_trie(TrieNode* root, const char* filename);
void destroy_trie(TrieNode* root);

// Cache operations
LRUCache* create_cache(int capacity);
FileMetadata* get_from_cache(LRUCache* cache, const char* filename);
void put_in_cache(LRUCache* cache, const char* filename, FileMetadata* metadata);
void destroy_cache(LRUCache* cache);

// Storage Server management
int register_storage_server(NameServer* nm, const char* ip, int nm_port, 
                            int client_port, char** files, int file_count, int socket_fd);
StorageServer* get_storage_server(NameServer* nm, int ss_id);
StorageServer* find_ss_for_file(NameServer* nm, const char* filename);
void deregister_storage_server(NameServer* nm, int ss_id);

// Client management
int register_client(NameServer* nm, const char* username, const char* ip, 
                   int nm_port, int ss_port, int socket_fd);
Client* get_client(NameServer* nm, int client_id);
void deregister_client(NameServer* nm, int client_id);

// File operations
ErrorCode handle_view_files(NameServer* nm, Client* client, const char* flags, char* response);
ErrorCode handle_create_file(NameServer* nm, Client* client, const char* filename);
ErrorCode handle_delete_file(NameServer* nm, Client* client, const char* filename);
ErrorCode handle_read_file(NameServer* nm, Client* client, const char* filename, char* response);
ErrorCode handle_write_file(NameServer* nm, Client* client, const char* filename, int sentence_num);
ErrorCode handle_info_file(NameServer* nm, Client* client, const char* filename, char* response);
ErrorCode handle_stream_file(NameServer* nm, Client* client, const char* filename, char* response);
ErrorCode handle_exec_file(NameServer* nm, Client* client, const char* filename, char* response);
ErrorCode handle_undo_file(NameServer* nm, Client* client, const char* filename);

// Access control
ErrorCode add_access(NameServer* nm, Client* client, const char* filename, 
                    const char* username, AccessRight access);
ErrorCode remove_access(NameServer* nm, Client* client, const char* filename, const char* username);
AccessRight check_access(FileMetadata* metadata, const char* username);
bool is_owner(FileMetadata* metadata, const char* username);

// User management
ErrorCode handle_list_users(NameServer* nm, char* response);

// Logging
void log_message(NameServer* nm, const char* level, const char* client_ip, 
                int client_port, const char* username, const char* operation, 
                const char* details);
void log_error(NameServer* nm, ErrorCode error, const char* details);

// Networking
void* handle_connection(void* arg);
void send_response(int socket_fd, ErrorCode error, const char* message);
int forward_to_ss(NameServer* nm, int ss_id, const char* command, char* response);

// Utilities
const char* error_to_string(ErrorCode error);
char* format_file_info(FileMetadata* metadata, bool detailed);
void parse_command(const char* command, char* cmd, char* args[], int* arg_count);

#endif // NAME_SERVER_H
