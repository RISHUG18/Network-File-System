#include "name_server.h"
#include <errno.h>
#include <signal.h>

// Global name server instance for signal handling
NameServer* g_nm = NULL;

// ==================== UTILITY FUNCTIONS ====================

const char* error_to_string(ErrorCode error) {
    switch (error) {
        case ERR_SUCCESS: return "Success";
        case ERR_FILE_NOT_FOUND: return "File not found";
        case ERR_UNAUTHORIZED: return "Unauthorized access";
        case ERR_FILE_EXISTS: return "File already exists";
        case ERR_FILE_LOCKED: return "File is locked for writing";
        case ERR_SS_NOT_FOUND: return "Storage server not found";
        case ERR_CLIENT_NOT_FOUND: return "Client not found";
        case ERR_INVALID_OPERATION: return "Invalid operation";
        case ERR_SS_DISCONNECTED: return "Storage server disconnected";
        case ERR_PERMISSION_DENIED: return "Permission denied";
        case ERR_INVALID_SENTENCE: return "Invalid sentence number";
        case ERR_PARENT_NOT_FOUND: return "Parent folder not found";
        case ERR_SYSTEM_ERROR: return "System error";
        default: return "Unknown error";
    }
}

void log_message(NameServer* nm, const char* level, const char* client_ip, 
                int client_port, const char* username, const char* operation, 
                const char* details) {
    pthread_mutex_lock(&nm->log_lock);
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    // Log to file
    fprintf(nm->log_file, "[%s] [%s] IP=%s Port=%d User=%s Op=%s Details=%s\n",
            timestamp, level, client_ip ? client_ip : "N/A", client_port,
            username ? username : "N/A", operation, details ? details : "");
    fflush(nm->log_file);
    
    // Also print to terminal
    printf("[%s] [%s] %s - %s\n", timestamp, level, operation, 
           details ? details : "");
    
    pthread_mutex_unlock(&nm->log_lock);
}

void log_error(NameServer* nm, ErrorCode error, const char* details) {
    log_message(nm, "ERROR", NULL, 0, NULL, error_to_string(error), details);
}

// Persistence helpers (forward declarations)
static void free_acl_list(AccessEntry* head);
static void free_access_requests(AccessRequest* head);
static void load_user_registry(NameServer* nm);
static void save_user_registry(NameServer* nm);
static RegisteredUser* find_registered_user(NameServer* nm, const char* username);
static RegisteredUser* ensure_registered_user(NameServer* nm, const char* username);
static void mark_user_active(NameServer* nm, const char* username, const char* ip, bool is_active);

// ==================== TRIE OPERATIONS ====================

static void free_acl_list(AccessEntry* head) {
    while (head) {
        AccessEntry* next = head->next;
        free(head);
        head = next;
    }
}

static void free_access_requests(AccessRequest* head) {
    while (head) {
        AccessRequest* next = head->next;
        free(head);
        head = next;
    }
}

TrieNode* create_trie_node() {
    TrieNode* node = (TrieNode*)calloc(1, sizeof(TrieNode));
    if (!node) {
        perror("Failed to allocate trie node");
        return NULL;
    }
    node->is_end_of_word = false;
    node->file_metadata = NULL;
    return node;
}

void insert_file_trie(TrieNode* root, const char* filename, FileMetadata* metadata) {
    TrieNode* current = root;
    for (int i = 0; filename[i] != '\0'; i++) {
        unsigned char index = (unsigned char)filename[i];
        if (!current->children[index]) {
            current->children[index] = create_trie_node();
        }
        current = current->children[index];
    }
    current->is_end_of_word = true;
    current->file_metadata = metadata;
}

FileMetadata* search_file_trie(TrieNode* root, const char* filename) {
    TrieNode* current = root;
    for (int i = 0; filename[i] != '\0'; i++) {
        unsigned char index = (unsigned char)filename[i];
        if (!current->children[index]) {
            return NULL;
        }
        current = current->children[index];
    }
    if (current->is_end_of_word) {
        return (FileMetadata*)current->file_metadata;
    }
    return NULL;
}

void delete_file_trie(TrieNode* root, const char* filename) {
    TrieNode* current = root;
    for (int i = 0; filename[i] != '\0'; i++) {
        unsigned char index = (unsigned char)filename[i];
        if (!current->children[index]) {
            return;  // File not in trie
        }
        current = current->children[index];
    }
    if (current->is_end_of_word) {
        current->is_end_of_word = false;
        if (current->file_metadata) {
            FileMetadata* metadata = (FileMetadata*)current->file_metadata;
            free_acl_list(metadata->acl);
            free_access_requests(metadata->pending_requests);
            free(metadata);
            current->file_metadata = NULL;
        }
    }
}

void destroy_trie(TrieNode* root) {
    if (!root) return;
    for (int i = 0; i < 256; i++) {
        if (root->children[i]) {
            destroy_trie(root->children[i]);
        }
    }
    if (root->file_metadata) {
        FileMetadata* metadata = (FileMetadata*)root->file_metadata;
        struct AccessEntry* entry = metadata->acl;
        while (entry) {
            struct AccessEntry* next = entry->next;
            free(entry);
            entry = next;
        }
        free_access_requests(metadata->pending_requests);
        free(metadata);
    }
    free(root);
}

// ==================== CACHE OPERATIONS ====================

LRUCache* create_cache(int capacity) {
    LRUCache* cache = (LRUCache*)malloc(sizeof(LRUCache));
    cache->head = NULL;
    cache->tail = NULL;
    cache->size = 0;
    cache->capacity = capacity;
    pthread_mutex_init(&cache->lock, NULL);
    return cache;
}

void move_to_front(LRUCache* cache, CacheEntry* entry) {
    if (cache->head == entry) return;
    
    // Remove from current position
    if (entry->prev) entry->prev->next = entry->next;
    if (entry->next) entry->next->prev = entry->prev;
    if (cache->tail == entry) cache->tail = entry->prev;
    
    // Move to front
    entry->next = cache->head;
    entry->prev = NULL;
    if (cache->head) cache->head->prev = entry;
    cache->head = entry;
    if (!cache->tail) cache->tail = entry;
}

FileMetadata* get_from_cache(LRUCache* cache, const char* filename) {
    pthread_mutex_lock(&cache->lock);
    
    CacheEntry* current = cache->head;
    while (current) {
        if (strcmp(current->filename, filename) == 0) {
            move_to_front(cache, current);
            current->timestamp = time(NULL);
            pthread_mutex_unlock(&cache->lock);
            return current->metadata;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&cache->lock);
    return NULL;
}

void put_in_cache(LRUCache* cache, const char* filename, FileMetadata* metadata) {
    pthread_mutex_lock(&cache->lock);
    
    // Check if already exists
    CacheEntry* current = cache->head;
    while (current) {
        if (strcmp(current->filename, filename) == 0) {
            current->metadata = metadata;
            current->timestamp = time(NULL);
            move_to_front(cache, current);
            pthread_mutex_unlock(&cache->lock);
            return;
        }
        current = current->next;
    }
    
    // Create new entry
    CacheEntry* entry = (CacheEntry*)malloc(sizeof(CacheEntry));
    strncpy(entry->filename, filename, MAX_FILENAME - 1);
    entry->filename[MAX_FILENAME - 1] = '\0';
    entry->metadata = metadata;
    entry->timestamp = time(NULL);
    entry->next = cache->head;
    entry->prev = NULL;
    
    if (cache->head) cache->head->prev = entry;
    cache->head = entry;
    if (!cache->tail) cache->tail = entry;
    
    cache->size++;
    
    // Evict if necessary
    if (cache->size > cache->capacity) {
        CacheEntry* old_tail = cache->tail;
        cache->tail = old_tail->prev;
        if (cache->tail) cache->tail->next = NULL;
        free(old_tail);
        cache->size--;
    }
    
    pthread_mutex_unlock(&cache->lock);
}

void destroy_cache(LRUCache* cache) {
    pthread_mutex_lock(&cache->lock);
    CacheEntry* current = cache->head;
    while (current) {
        CacheEntry* next = current->next;
        free(current);
        current = next;
    }
    pthread_mutex_unlock(&cache->lock);
    pthread_mutex_destroy(&cache->lock);
    free(cache);
}

// ==================== NAME SERVER INITIALIZATION ====================

NameServer* init_name_server(int port) {
    NameServer* nm = (NameServer*)malloc(sizeof(NameServer));
    if (!nm) {
        perror("Failed to allocate name server");
        return NULL;
    }
    
    nm->nm_port = port;
    nm->ss_count = 0;
    nm->client_count = 0;
    nm->next_ss_index = 0;
    nm->is_running = true;
    
    // Initialize data structures
    nm->file_trie = create_trie_node();
    nm->cache = create_cache(CACHE_SIZE);
    
    for (int i = 0; i < MAX_SS; i++) {
        nm->storage_servers[i] = NULL;
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        nm->clients[i] = NULL;
    }
    nm->registered_user_count = 0;
    for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
        nm->user_registry[i] = NULL;
    }
    
    // Initialize locks
    pthread_mutex_init(&nm->ss_lock, NULL);
    pthread_mutex_init(&nm->client_lock, NULL);
    pthread_mutex_init(&nm->trie_lock, NULL);
    pthread_mutex_init(&nm->log_lock, NULL);
    pthread_mutex_init(&nm->registry_lock, NULL);
    pthread_mutex_init(&nm->persistence_lock, NULL);
    
    // Open log file
    nm->log_file = fopen(LOG_FILE, "a");
    if (!nm->log_file) {
        perror("Failed to open log file");
        free(nm);
        return NULL;
    }
    
    // Create socket
    nm->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (nm->socket_fd < 0) {
        perror("Failed to create socket");
        fclose(nm->log_file);
        free(nm);
        return NULL;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(nm->socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(nm->socket_fd);
        fclose(nm->log_file);
        free(nm);
        return NULL;
    }
    
    // Bind socket
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(nm->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(nm->socket_fd);
        fclose(nm->log_file);
        free(nm);
        return NULL;
    }
    
    // Listen
    if (listen(nm->socket_fd, 10) < 0) {
        perror("Listen failed");
        close(nm->socket_fd);
        fclose(nm->log_file);
        free(nm);
        return NULL;
    }
    
    log_message(nm, "INFO", NULL, 0, NULL, "INIT", "Name Server initialized");
    printf("Name Server initialized on port %d\n", port);

    load_user_registry(nm);
    
    return nm;
}

void destroy_name_server(NameServer* nm) {
    if (!nm) return;
    
    nm->is_running = false;
    
    // Close all connections
    for (int i = 0; i < MAX_SS; i++) {
        if (nm->storage_servers[i]) {
            if (nm->storage_servers[i]->socket_fd >= 0) {
                close(nm->storage_servers[i]->socket_fd);
            }
            for (int j = 0; j < nm->storage_servers[i]->file_count; j++) {
                free(nm->storage_servers[i]->files[j]);
            }
            free(nm->storage_servers[i]->files);
            pthread_mutex_destroy(&nm->storage_servers[i]->lock);
            free(nm->storage_servers[i]);
        }
    }
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (nm->clients[i]) {
            if (nm->clients[i]->socket_fd >= 0) {
                close(nm->clients[i]->socket_fd);
            }
            free(nm->clients[i]);
        }
    }
    
    // Destroy data structures
    destroy_trie(nm->file_trie);
    destroy_cache(nm->cache);
    
    // Close socket
    if (nm->socket_fd >= 0) {
        close(nm->socket_fd);
    }
    
    // Close log file
    if (nm->log_file) {
        fclose(nm->log_file);
    }
    
    for (int i = 0; i < nm->registered_user_count; i++) {
        free(nm->user_registry[i]);
    }
    
    // Destroy locks
    pthread_mutex_destroy(&nm->ss_lock);
    pthread_mutex_destroy(&nm->client_lock);
    pthread_mutex_destroy(&nm->trie_lock);
    pthread_mutex_destroy(&nm->log_lock);
    pthread_mutex_destroy(&nm->registry_lock);
    pthread_mutex_destroy(&nm->persistence_lock);
    
    free(nm);
    printf("Name Server destroyed\n");
}

// ==================== STORAGE SERVER MANAGEMENT ====================

int register_storage_server(NameServer* nm, const char* ip, int nm_port, 
                            int client_port, char** files, int file_count, int socket_fd) {
    pthread_mutex_lock(&nm->ss_lock);
    
    // Check if SS already registered (same IP and client port)
    for (int i = 0; i < nm->ss_count; i++) {
        StorageServer* existing_ss = nm->storage_servers[i];
        if (existing_ss && 
            strcmp(existing_ss->ip, ip) == 0 && 
            existing_ss->client_port == client_port) {
            
            // Found existing SS - update it
            existing_ss->nm_port = nm_port;
            
            // Close old socket if different
            if (existing_ss->socket_fd >= 0 && existing_ss->socket_fd != socket_fd) {
                close(existing_ss->socket_fd);
            }
            existing_ss->socket_fd = socket_fd;
            existing_ss->is_active = true;
            
            // Update files
            // Free old files
            for (int j = 0; j < existing_ss->file_count; j++) {
                free(existing_ss->files[j]);
            }
            free(existing_ss->files);
            
            // Set new files
            existing_ss->file_count = file_count;
            existing_ss->files = (char**)malloc(sizeof(char*) * file_count);
            for (int j = 0; j < file_count; j++) {
                existing_ss->files[j] = strdup(files[j]);
            }
            
            int ss_id = existing_ss->id;
            pthread_mutex_unlock(&nm->ss_lock);
            
            // Update trie for new files
            pthread_mutex_lock(&nm->trie_lock);
            for (int j = 0; j < file_count; j++) {
                FileMetadata* metadata = search_file_trie(nm->file_trie, files[j]);
                if (metadata) {
                    metadata->ss_id = ss_id;
                    metadata->last_accessed = time(NULL);
                    put_in_cache(nm->cache, files[j], metadata);
                    continue;
                }

                metadata = (FileMetadata*)calloc(1, sizeof(FileMetadata));
                if (!metadata) {
                    continue;
                }

                strncpy(metadata->filename, files[j], MAX_FILENAME - 1);
                metadata->filename[MAX_FILENAME - 1] = '\0';
                metadata->owner[0] = '\0';
                metadata->ss_id = ss_id;
                metadata->created_time = time(NULL);
                metadata->last_modified = time(NULL);
                metadata->last_accessed = time(NULL);
                metadata->last_accessed_by[0] = '\0';
                metadata->file_size = 0;
                metadata->word_count = 0;
                metadata->char_count = 0;
                metadata->acl = NULL;

                insert_file_trie(nm->file_trie, files[j], metadata);
                put_in_cache(nm->cache, files[j], metadata);
            }
            pthread_mutex_unlock(&nm->trie_lock);
            
            char details[256];
            snprintf(details, sizeof(details), "SS_ID=%d IP=%s NM_Port=%d Client_Port=%d Files=%d (Reconnected)",
                     ss_id, ip, nm_port, client_port, file_count);
            log_message(nm, "INFO", ip, nm_port, NULL, "SS_REGISTER", details);
            
            printf("Storage Server %d reconnected: %s:%d (Files: %d)\n", 
                   ss_id, ip, client_port, file_count);
            
            return ss_id;
        }
    }

    if (nm->ss_count >= MAX_SS) {
        pthread_mutex_unlock(&nm->ss_lock);
        return -1;
    }
    
    StorageServer* ss = (StorageServer*)malloc(sizeof(StorageServer));
    ss->id = nm->ss_count;
    strncpy(ss->ip, ip, MAX_IP_LEN - 1);
    ss->ip[MAX_IP_LEN - 1] = '\0';
    ss->nm_port = nm_port;
    ss->client_port = client_port;
    ss->socket_fd = socket_fd;
    ss->is_active = true;
    ss->file_count = file_count;
    ss->files = (char**)malloc(sizeof(char*) * file_count);
    
    for (int i = 0; i < file_count; i++) {
        ss->files[i] = strdup(files[i]);
    }
    
    pthread_mutex_init(&ss->lock, NULL);
    
    nm->storage_servers[nm->ss_count] = ss;
    int ss_id = nm->ss_count;
    nm->ss_count++;
    
    pthread_mutex_unlock(&nm->ss_lock);
    
    // Add files to trie (reuse existing metadata when available)
    pthread_mutex_lock(&nm->trie_lock);
    for (int i = 0; i < file_count; i++) {
        FileMetadata* metadata = search_file_trie(nm->file_trie, files[i]);
        if (metadata) {
            metadata->ss_id = ss_id;
            metadata->last_accessed = time(NULL);
            put_in_cache(nm->cache, files[i], metadata);
            continue;
        }

        metadata = (FileMetadata*)calloc(1, sizeof(FileMetadata));
        if (!metadata) {
            continue;
        }

    strncpy(metadata->filename, files[i], MAX_FILENAME - 1);
    metadata->filename[MAX_FILENAME - 1] = '\0';
        metadata->owner[0] = '\0';  // Will be set on first write
        metadata->ss_id = ss_id;
        metadata->created_time = time(NULL);
        metadata->last_modified = time(NULL);
        metadata->last_accessed = time(NULL);
    metadata->last_accessed_by[0] = '\0';
        metadata->file_size = 0;
        metadata->word_count = 0;
        metadata->char_count = 0;
        metadata->acl = NULL;

        insert_file_trie(nm->file_trie, files[i], metadata);
        put_in_cache(nm->cache, files[i], metadata);
    }
    pthread_mutex_unlock(&nm->trie_lock);
    
    char details[256];
    snprintf(details, sizeof(details), "SS_ID=%d IP=%s NM_Port=%d Client_Port=%d Files=%d",
             ss_id, ip, nm_port, client_port, file_count);
    log_message(nm, "INFO", ip, nm_port, NULL, "SS_REGISTER", details);
    
    printf("Storage Server %d registered: %s:%d (Files: %d)\n", 
           ss_id, ip, client_port, file_count);
    
    return ss_id;
}

StorageServer* get_storage_server(NameServer* nm, int ss_id) {
    if (ss_id < 0 || ss_id >= MAX_SS) return NULL;
    return nm->storage_servers[ss_id];
}

StorageServer* find_ss_for_file(NameServer* nm, const char* filename) {
    // First check cache
    FileMetadata* metadata = get_from_cache(nm->cache, filename);
    if (!metadata) {
        // Search in trie
        pthread_mutex_lock(&nm->trie_lock);
        metadata = search_file_trie(nm->file_trie, filename);
        pthread_mutex_unlock(&nm->trie_lock);
        
        if (metadata) {
            put_in_cache(nm->cache, filename, metadata);
        }
    }
    
    if (!metadata) return NULL;
    
    return get_storage_server(nm, metadata->ss_id);
}

void deregister_storage_server_safe(NameServer* nm, int ss_id, int socket_fd) {
    pthread_mutex_lock(&nm->ss_lock);
    
    StorageServer* ss = nm->storage_servers[ss_id];
    if (ss) {
        // If a specific fd is provided, only deregister if it matches
        if (socket_fd != -1 && ss->socket_fd != socket_fd) {
             pthread_mutex_unlock(&nm->ss_lock);
             return; // Stale disconnection event
        }

        ss->is_active = false;
        
        // Close socket and invalidate it
        if (ss->socket_fd >= 0) {
            close(ss->socket_fd);
            ss->socket_fd = -1;
        }

        char details[256];
        snprintf(details, sizeof(details), "SS_ID=%d IP=%s Client_Port=%d File_Count=%d", 
                 ss_id, ss->ip, ss->client_port, ss->file_count);
        log_message(nm, "WARN", ss->ip, ss->nm_port, NULL, "SS_DISCONNECT", details);
        printf("✗ Storage Server %d disconnected (IP: %s, Client Port: %d, Files: %d)\n", 
               ss_id, ss->ip, ss->client_port, ss->file_count);
    }
    
    pthread_mutex_unlock(&nm->ss_lock);
}

void deregister_storage_server(NameServer* nm, int ss_id) {
    deregister_storage_server_safe(nm, ss_id, -1);
}

// ==================== CLIENT MANAGEMENT ====================

int register_client(NameServer* nm, const char* username, const char* ip, 
                   int nm_port, int ss_port, int socket_fd) {
    pthread_mutex_lock(&nm->client_lock);
    
    if (nm->client_count >= MAX_CLIENTS) {
        pthread_mutex_unlock(&nm->client_lock);
        return -1;
    }
    
    Client* client = (Client*)malloc(sizeof(Client));
    client->id = nm->client_count;
    strncpy(client->username, username, MAX_USERNAME - 1);
    client->username[MAX_USERNAME - 1] = '\0';
    strncpy(client->ip, ip, MAX_IP_LEN - 1);
    client->ip[MAX_IP_LEN - 1] = '\0';
    client->nm_port = nm_port;
    client->ss_port = ss_port;
    client->socket_fd = socket_fd;
    client->is_active = true;
    client->connected_time = time(NULL);
    
    nm->clients[nm->client_count] = client;
    int client_id = nm->client_count;
    nm->client_count++;
    
    pthread_mutex_unlock(&nm->client_lock);
    mark_user_active(nm, username, ip, true);
    
    char details[256];
    snprintf(details, sizeof(details), "Client_ID=%d IP=%s NM_Port=%d SS_Port=%d",
             client_id, ip, nm_port, ss_port);
    log_message(nm, "INFO", ip, nm_port, username, "CLIENT_REGISTER", details);
    
    printf("Client %d registered: %s@%s:%d\n", client_id, username, ip, nm_port);
    
    return client_id;
}

Client* get_client(NameServer* nm, int client_id) {
    if (client_id < 0 || client_id >= MAX_CLIENTS) return NULL;
    return nm->clients[client_id];
}

void deregister_client(NameServer* nm, int client_id) {
    pthread_mutex_lock(&nm->client_lock);
    
    Client* client = nm->clients[client_id];
    if (client) {
        client->is_active = false;
        char details[128];
        snprintf(details, sizeof(details), "Client_ID=%d", client_id);
        log_message(nm, "INFO", client->ip, client->nm_port, client->username, 
                   "CLIENT_DISCONNECT", details);
        mark_user_active(nm, client->username, client->ip, false);
        printf("Client %d (%s) disconnected\n", client_id, client->username);
    }
    
    pthread_mutex_unlock(&nm->client_lock);
}

// ==================== ACCESS CONTROL ====================

AccessRight check_access(FileMetadata* metadata, const char* username) {
    // Owner has full access
    if (metadata->owner[0] != '\0' && strcmp(metadata->owner, username) == 0) {
        return ACCESS_WRITE;
    }
    
    // Check ACL
    struct AccessEntry* entry = metadata->acl;
    while (entry) {
        if (strcmp(entry->username, username) == 0) {
            return entry->access;
        }
        entry = entry->next;
    }
    
    return ACCESS_NONE;
}

bool is_owner(FileMetadata* metadata, const char* username) {
    return (metadata->owner[0] != '\0' && strcmp(metadata->owner, username) == 0);
}

ErrorCode add_access(NameServer* nm, Client* client, const char* filename, 
                    const char* username, AccessRight access) {
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* metadata = search_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);
    
    if (!metadata) {
        return ERR_FILE_NOT_FOUND;
    }
    
    if (!is_owner(metadata, client->username)) {
        return ERR_PERMISSION_DENIED;
    }
    
    // Check if user already has access
    struct AccessEntry* entry = metadata->acl;
    while (entry) {
        if (strcmp(entry->username, username) == 0) {
            entry->access = access;  // Update access
            char details[256];
            snprintf(details, sizeof(details), "File=%s User=%s Access=%s",
                    filename, username, access == ACCESS_READ ? "READ" : "WRITE");
            log_message(nm, "INFO", client->ip, client->nm_port, client->username,
                       "UPDATE_ACCESS", details);
            return ERR_SUCCESS;
        }
        entry = entry->next;
    }
    
    // Add new entry
    struct AccessEntry* new_entry = (struct AccessEntry*)malloc(sizeof(struct AccessEntry));
    strncpy(new_entry->username, username, MAX_USERNAME - 1);
    new_entry->username[MAX_USERNAME - 1] = '\0';
    new_entry->access = access;
    new_entry->next = metadata->acl;
    metadata->acl = new_entry;
    
    char details[256];
    snprintf(details, sizeof(details), "File=%s User=%s Access=%s",
            filename, username, access == ACCESS_READ ? "READ" : "WRITE");
    log_message(nm, "INFO", client->ip, client->nm_port, client->username,
               "ADD_ACCESS", details);
    
    return ERR_SUCCESS;
}

ErrorCode remove_access(NameServer* nm, Client* client, const char* filename, const char* username) {
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* metadata = search_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);
    
    if (!metadata) {
        return ERR_FILE_NOT_FOUND;
    }
    
    if (!is_owner(metadata, client->username)) {
        return ERR_PERMISSION_DENIED;
    }
    
    // Remove from ACL
    struct AccessEntry* entry = metadata->acl;
    struct AccessEntry* prev = NULL;
    
    while (entry) {
        if (strcmp(entry->username, username) == 0) {
            if (prev) {
                prev->next = entry->next;
            } else {
                metadata->acl = entry->next;
            }
            free(entry);
            
            char details[256];
            snprintf(details, sizeof(details), "File=%s User=%s", filename, username);
            log_message(nm, "INFO", client->ip, client->nm_port, client->username,
                       "REMOVE_ACCESS", details);
            return ERR_SUCCESS;
        }
        prev = entry;
        entry = entry->next;
    }
    
    return ERR_UNAUTHORIZED;
}

static RegisteredUser* find_registered_user(NameServer* nm, const char* username) {
    if (!username) return NULL;
    for (int i = 0; i < nm->registered_user_count; i++) {
        if (nm->user_registry[i] && strcmp(nm->user_registry[i]->username, username) == 0) {
            return nm->user_registry[i];
        }
    }
    return NULL;
}

static RegisteredUser* ensure_registered_user(NameServer* nm, const char* username) {
    RegisteredUser* existing = find_registered_user(nm, username);
    if (existing) {
        return existing;
    }
    if (nm->registered_user_count >= MAX_REGISTERED_USERS) {
        return NULL;
    }
    RegisteredUser* user = (RegisteredUser*)calloc(1, sizeof(RegisteredUser));
    strncpy(user->username, username, MAX_USERNAME - 1);
    user->first_seen = 0;
    user->last_seen = 0;
    user->is_active = false;
    nm->user_registry[nm->registered_user_count++] = user;
    return user;
}

static void save_user_registry(NameServer* nm) {
    pthread_mutex_lock(&nm->persistence_lock);
    FILE* fp = fopen(USER_REGISTRY_FILE, "w");
    if (!fp) {
        pthread_mutex_unlock(&nm->persistence_lock);
        perror("Failed to persist user registry");
        return;
    }
    pthread_mutex_lock(&nm->registry_lock);
    for (int i = 0; i < nm->registered_user_count; i++) {
        RegisteredUser* user = nm->user_registry[i];
        if (!user) continue;
        fprintf(fp, "%s|%s|%ld|%ld|%d\n",
                user->username,
                user->last_ip[0] ? user->last_ip : "",
                (long)user->first_seen,
                (long)user->last_seen,
                user->is_active ? 1 : 0);
    }
    pthread_mutex_unlock(&nm->registry_lock);
    fclose(fp);
    pthread_mutex_unlock(&nm->persistence_lock);
}

static void load_user_registry(NameServer* nm) {
    pthread_mutex_lock(&nm->persistence_lock);
    FILE* fp = fopen(USER_REGISTRY_FILE, "r");
    if (!fp) {
        pthread_mutex_unlock(&nm->persistence_lock);
        return;
    }
    pthread_mutex_lock(&nm->registry_lock);
    for (int i = 0; i < nm->registered_user_count; i++) {
        free(nm->user_registry[i]);
        nm->user_registry[i] = NULL;
    }
    nm->registered_user_count = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        char* username = strtok(line, "|");
        char* ip = strtok(NULL, "|");
        char* first_seen_str = strtok(NULL, "|");
        char* last_seen_str = strtok(NULL, "|");
        char* active_str = strtok(NULL, "|");
        if (!username) {
            continue;
        }
        RegisteredUser* user = ensure_registered_user(nm, username);
        if (!user) {
            continue;
        }
        if (ip) {
            strncpy(user->last_ip, ip, MAX_IP_LEN - 1);
        }
        user->first_seen = first_seen_str ? (time_t)atol(first_seen_str) : 0;
        user->last_seen = last_seen_str ? (time_t)atol(last_seen_str) : 0;
        user->is_active = active_str ? (atoi(active_str) != 0) : false;
    }
    pthread_mutex_unlock(&nm->registry_lock);
    fclose(fp);
    pthread_mutex_unlock(&nm->persistence_lock);
}

static void mark_user_active(NameServer* nm, const char* username, const char* ip, bool is_active) {
    if (!username) return;
    bool updated = false;
    pthread_mutex_lock(&nm->registry_lock);
    RegisteredUser* user = ensure_registered_user(nm, username);
    if (user) {
        if (ip && *ip) {
            strncpy(user->last_ip, ip, MAX_IP_LEN - 1);
            user->last_ip[MAX_IP_LEN - 1] = '\0';
        }
        time_t now = time(NULL);
        if (user->first_seen == 0) {
            user->first_seen = now;
        }
        user->last_seen = now;
        user->is_active = is_active;
        updated = true;
    }
    pthread_mutex_unlock(&nm->registry_lock);
    if (updated) {
        save_user_registry(nm);
    }
}

// To be continued in next part...
