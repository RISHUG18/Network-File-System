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
#include <ctype.h>

// Constants
#define MAX_FILENAME 256
#define MAX_PATH 512
#define MAX_FILES 1000
#define MAX_SENTENCE_LOCKS 1000
#define BUFFER_SIZE 4096
#define MAX_CONTENT_SIZE (1024 * 1024)  // 1MB max file size
#define LOG_FILE "ss_log.txt"
#define STORAGE_DIR "./storage"
#define CHECKPOINT_BASE_DIR STORAGE_DIR "/checkpoints"
#define MAX_CHECKPOINT_TAG 64
#define SENTENCE_UNDO_HISTORY 50

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

// Sentence Node - Doubly Linked List
struct SentenceNode;

typedef struct DraftSentence {
    char** words;                    // Staged words before commit
    int word_count;
    int word_capacity;
    char delimiter;
    struct DraftSentence* next;
} DraftSentence;

typedef struct SentenceUndoEntry {
    struct SentenceNode* sentence;   // Target sentence for this snapshot
    char** words_snapshot;           // Deep copy of words before commit
    int word_count;
    char delimiter;
    int appended_sentences;          // Sentences created during the commit
    struct SentenceUndoEntry* next;  // Stack linkage
} SentenceUndoEntry;

typedef struct SentenceNode {
    char** words;                    // Dynamic array of words
    int word_count;                  // Number of words in sentence
    int word_capacity;               // Allocated capacity for words array
    char delimiter;                  // Sentence ending: '.', '!', '?', or '\0'
    pthread_mutex_t lock;            // Sentence-level lock for concurrent access
    bool is_locked;
    int lock_holder_id;              // Client ID holding the lock
    struct SentenceNode* next;       // Next sentence in list
    struct SentenceNode* prev;       // Previous sentence in list
    DraftSentence* draft_head;       // Pending staged edits (linked list per delimiter)
    bool draft_dirty;                // True if staged edits differ from live data
} SentenceNode;

// File structure with Linked List
typedef struct FileEntry {
    char filename[MAX_FILENAME];
    char filepath[MAX_PATH];
    SentenceNode* head;              // First sentence (linked list head)
    SentenceNode* tail;              // Last sentence (linked list tail)
    int sentence_count;              // Total number of sentences
    size_t total_size;
    int total_words;
    int total_chars;
    pthread_rwlock_t file_lock;      // Reader-writer lock for file-level operations
    pthread_mutex_t structure_lock;  // Protects linked list structure modifications
    time_t last_modified;
    time_t last_accessed;
    SentenceUndoEntry* undo_head;    // Stack of sentence-level undo entries
    int undo_depth;
} FileEntry;

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
ErrorCode create_folder(StorageServer* ss, const char* foldername);
FileEntry* find_file(StorageServer* ss, const char* filename);
ErrorCode delete_file(StorageServer* ss, const char* filename);
ErrorCode read_file(StorageServer* ss, const char* filename, char* content, size_t* size);
ErrorCode write_sentence(StorageServer* ss, const char* filename, int sentence_num, 
                        int word_index, const char* new_content, int client_id);
ErrorCode lock_sentence(StorageServer* ss, const char* filename, int sentence_num, int client_id);
ErrorCode unlock_sentence(StorageServer* ss, const char* filename, int sentence_num, int client_id);
ErrorCode rename_file(StorageServer* ss, const char* old_filename, const char* new_filename);

// Sentence Node Operations (Linked List)
SentenceNode* create_sentence_node(const char** word_array, int word_count, char delimiter);
SentenceNode* create_empty_sentence_node();
void append_sentence(FileEntry* file, SentenceNode* node);
void insert_sentence_after(FileEntry* file, SentenceNode* prev, SentenceNode* new_node);
void delete_sentence_node(FileEntry* file, SentenceNode* node);
SentenceNode* get_sentence_by_index(FileEntry* file, int index);
void free_sentence_node(SentenceNode* node);
void free_all_sentences(FileEntry* file);

// Sentence parsing
void parse_sentences(FileEntry* file, const char* content);
void rebuild_file_content(FileEntry* file, char* content);
void refresh_file_stats(FileEntry* file);
int count_words(const char* text);
bool is_sentence_delimiter(char c);

// Word operations within sentence
bool insert_word_in_sentence(SentenceNode* sentence, int index, const char* word);
bool delete_word_in_sentence(SentenceNode* sentence, int index);
bool replace_word_in_sentence(SentenceNode* sentence, int index, const char* word);

ErrorCode handle_undo(StorageServer* ss, const char* filename);

// Undo file helpers
bool build_undo_path(const FileEntry* file, char* buffer, size_t size);
ErrorCode copy_file_contents(const char* src_path, const char* dst_path);

// Streaming
ErrorCode stream_file(StorageServer* ss, int client_fd, const char* filename);

// File info
ErrorCode get_file_info(StorageServer* ss, const char* filename, 
                       size_t* size, int* words, int* chars, time_t* last_accessed);

// Persistence
bool save_file_to_disk(FileEntry* file);
bool load_file_from_disk(StorageServer* ss, const char* filename);
void load_all_files(StorageServer* ss);

// Checkpoints
ErrorCode create_checkpoint(StorageServer* ss, const char* filename, const char* tag);
ErrorCode view_checkpoint(StorageServer* ss, const char* filename, const char* tag,
                         char* buffer, size_t buffer_size);
ErrorCode revert_to_checkpoint(StorageServer* ss, const char* filename, const char* tag);
ErrorCode list_checkpoints(StorageServer* ss, const char* filename, char* buffer, size_t buffer_size);
void remove_all_checkpoints(const char* filename);
void clear_file_undo_history(FileEntry* file);

// Draft management
void free_draft_sentences(DraftSentence* head);
ErrorCode commit_sentence_drafts(StorageServer* ss, const char* filename, int sentence_num);

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
