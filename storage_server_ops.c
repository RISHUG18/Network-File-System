#include "storage_server.h"

// ==================== SENTENCE LOCKING AND WRITE OPERATIONS ====================

ErrorCode lock_sentence(StorageServer* ss, const char* filename, int sentence_num, int client_id) {
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }

    if (sentence_num < 0 || sentence_num > file->sentence_count) {
        return ERR_INVALID_SENTENCE;
    }

    char snapshot[MAX_CONTENT_SIZE];
    bool snapshot_ready = false;

    // Take snapshot before any modifications
    pthread_rwlock_rdlock(&file->file_lock);
    rebuild_file_content(file, snapshot);
    snapshot_ready = true;
    pthread_rwlock_unlock(&file->file_lock);

    // If requesting to lock a new sentence at the end
    if (sentence_num == file->sentence_count) {
        // Create new empty sentence
        SentenceNode* new_node = create_empty_sentence_node();
        if (!new_node) {
            return ERR_SYSTEM_ERROR;
        }
        
        // Lock it immediately
        pthread_mutex_lock(&new_node->lock);
        new_node->is_locked = true;
        new_node->lock_holder_id = client_id;
        pthread_mutex_unlock(&new_node->lock);
        
        // Append to file (this handles structure lock internally)
        append_sentence(file, new_node);
        
        if (snapshot_ready) {
            push_undo(ss, filename, snapshot);
        }
        return ERR_SUCCESS;
    }

    // Get existing sentence
    SentenceNode* sentence = get_sentence_by_index(file, sentence_num);
    if (!sentence) {
        return ERR_INVALID_SENTENCE;
    }

    // Try to lock the sentence
    pthread_mutex_lock(&sentence->lock);

    if (sentence->is_locked) {
        if (sentence->lock_holder_id == client_id) {
            // Already locked by this client
            pthread_mutex_unlock(&sentence->lock);
            return ERR_SUCCESS;
        }
        // Locked by another client
        pthread_mutex_unlock(&sentence->lock);
        return ERR_FILE_LOCKED;
    }

    // Lock it
    sentence->is_locked = true;
    sentence->lock_holder_id = client_id;

    pthread_mutex_unlock(&sentence->lock);

    if (snapshot_ready) {
        push_undo(ss, filename, snapshot);
    }

    return ERR_SUCCESS;
}

ErrorCode unlock_sentence(StorageServer* ss, const char* filename, int sentence_num, int client_id) {
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }
    
    if (sentence_num < 0 || sentence_num >= file->sentence_count) {
        return ERR_INVALID_SENTENCE;
    }
    
    SentenceNode* sent = get_sentence_by_index(file, sentence_num);
    if (!sent) {
        return ERR_INVALID_SENTENCE;
    }
    
    pthread_mutex_lock(&sent->lock);
    
    if (sent->is_locked && sent->lock_holder_id == client_id) {
        sent->is_locked = false;
        sent->lock_holder_id = -1;
    }
    
    pthread_mutex_unlock(&sent->lock);
    
    return ERR_SUCCESS;
}

ErrorCode write_sentence(StorageServer* ss, const char* filename, int sentence_num, 
                        int word_index, const char* new_content, int client_id) {
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Validate sentence number
    if (sentence_num < 0 || sentence_num >= file->sentence_count) {
        // Auto-create sentence if it's exactly at the end
        if (sentence_num == file->sentence_count) {
            SentenceNode* new_node = create_empty_sentence_node();
            if (!new_node) {
                return ERR_SYSTEM_ERROR;
            }
            
            pthread_mutex_lock(&new_node->lock);
            new_node->is_locked = true;
            new_node->lock_holder_id = client_id;
            pthread_mutex_unlock(&new_node->lock);
            
            append_sentence(file, new_node);
        } else {
            return ERR_INVALID_SENTENCE;
        }
    }
    
    // Get the sentence
    SentenceNode* sent = get_sentence_by_index(file, sentence_num);
    if (!sent) {
        return ERR_INVALID_SENTENCE;
    }
    
    // Check if sentence is locked by this client
    pthread_mutex_lock(&sent->lock);
    if (sent->is_locked && sent->lock_holder_id != client_id) {
        pthread_mutex_unlock(&sent->lock);
        return ERR_FILE_LOCKED;
    }
    
    // Perform the word insertion
    bool success = insert_word_in_sentence(sent, word_index, new_content);
    
    pthread_mutex_unlock(&sent->lock);
    
    if (!success) {
        return ERR_INVALID_OPERATION;
    }
    
    // Check if the new word contains delimiters - if so, need to reparse
    bool has_delimiter = false;
    for (size_t i = 0; i < strlen(new_content); i++) {
        if (is_sentence_delimiter(new_content[i])) {
            has_delimiter = true;
            break;
        }
    }
    
    if (has_delimiter) {
        // Delimiter added - need to reparse entire file
        pthread_rwlock_wrlock(&file->file_lock);
        
        // Rebuild full content with current state
        char full_content[MAX_CONTENT_SIZE];
        rebuild_file_content(file, full_content);
        
        // Save lock states before reparsing
        typedef struct {
            bool is_locked;
            int lock_holder_id;
        } LockState;
        
        LockState* lock_states = (LockState*)calloc(file->sentence_count, sizeof(LockState));
        int old_count = file->sentence_count;
        
        pthread_mutex_lock(&file->structure_lock);
        SentenceNode* curr = file->head;
        int idx = 0;
        while (curr && idx < old_count) {
            pthread_mutex_lock(&curr->lock);
            lock_states[idx].is_locked = curr->is_locked;
            lock_states[idx].lock_holder_id = curr->lock_holder_id;
            pthread_mutex_unlock(&curr->lock);
            curr = curr->next;
            idx++;
        }
        pthread_mutex_unlock(&file->structure_lock);
        
        // Reparse (this frees old sentences and creates new ones)
        parse_sentences(file, full_content);
        
        // Restore locks where possible
        pthread_mutex_lock(&file->structure_lock);
        curr = file->head;
        idx = 0;
        while (curr && idx < old_count && idx < file->sentence_count) {
            pthread_mutex_lock(&curr->lock);
            curr->is_locked = lock_states[idx].is_locked;
            curr->lock_holder_id = lock_states[idx].lock_holder_id;
            pthread_mutex_unlock(&curr->lock);
            curr = curr->next;
            idx++;
        }
        pthread_mutex_unlock(&file->structure_lock);
        
        free(lock_states);
        
        pthread_rwlock_unlock(&file->file_lock);
    }
    
    // Save to disk (acquire file lock for this)
    pthread_rwlock_rdlock(&file->file_lock);
    refresh_file_stats(file);
    file->last_modified = time(NULL);
    save_file_to_disk(file);
    pthread_rwlock_unlock(&file->file_lock);
    
    char details[256];
    snprintf(details, sizeof(details), "File=%s Sentence=%d Word=%d", 
            filename, sentence_num, word_index);
    log_message(ss, "INFO", "WRITE", details);
    
    return ERR_SUCCESS;
}

// ==================== STREAMING ====================

ErrorCode stream_file(StorageServer* ss, int client_fd, const char* filename) {
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }
    
    pthread_rwlock_rdlock(&file->file_lock);
    pthread_mutex_lock(&file->structure_lock);
    
    // Send file word by word with 0.1s delay
    SentenceNode* current = file->head;
    
    while (current) {
        // Send each word in the sentence
        for (int i = 0; i < current->word_count; i++) {
            char word_msg[BUFFER_SIZE];
            snprintf(word_msg, sizeof(word_msg), "%s\n", current->words[i]);
            
            ssize_t sent_bytes = send(client_fd, word_msg, strlen(word_msg), MSG_NOSIGNAL);
            if (sent_bytes <= 0) {
                // Client disconnected
                pthread_mutex_unlock(&file->structure_lock);
                pthread_rwlock_unlock(&file->file_lock);
                return ERR_SYSTEM_ERROR;
            }
            
            // 0.1 second delay
            usleep(100000);
        }
        
        current = current->next;
    }
    
    pthread_mutex_unlock(&file->structure_lock);
    
    // Send STOP marker
    send(client_fd, "STOP\n", 5, MSG_NOSIGNAL);
    
    file->last_accessed = time(NULL);
    
    pthread_rwlock_unlock(&file->file_lock);
    
    char details[256];
    snprintf(details, sizeof(details), "File=%s", filename);
    log_message(ss, "INFO", "STREAM", details);
    
    return ERR_SUCCESS;
}

// ==================== FILE INFO ====================

ErrorCode get_file_info(StorageServer* ss, const char* filename, 
                       size_t* size, int* words, int* chars) {
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }
    
    pthread_rwlock_rdlock(&file->file_lock);
    
    *size = file->total_size;
    *words = file->total_words;
    *chars = file->total_chars;
    
    pthread_rwlock_unlock(&file->file_lock);
    
    return ERR_SUCCESS;
}

// ==================== UNDO OPERATION ====================

ErrorCode handle_undo(StorageServer* ss, const char* filename) {
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }
    
    char undo_content[MAX_CONTENT_SIZE];
    ErrorCode err = pop_undo(ss, filename, undo_content);
    if (err != ERR_SUCCESS) {
        return err;
    }
    
    pthread_rwlock_wrlock(&file->file_lock);
    
    // Reparse with undo content (this will free old sentences and create new ones)
    parse_sentences(file, undo_content);
    
    // Save to disk
    file->last_modified = time(NULL);
    save_file_to_disk(file);
    
    pthread_rwlock_unlock(&file->file_lock);
    
    char details[256];
    snprintf(details, sizeof(details), "File=%s", filename);
    log_message(ss, "INFO", "UNDO", details);
    
    return ERR_SUCCESS;
}

// ==================== INITIALIZATION ====================

StorageServer* init_storage_server(const char* nm_ip, int nm_port, int client_port) {
    StorageServer* ss = (StorageServer*)malloc(sizeof(StorageServer));
    if (!ss) {
        perror("Failed to allocate storage server");
        return NULL;
    }
    
    ss->ss_id = -1;
    strncpy(ss->nm_ip, nm_ip, sizeof(ss->nm_ip) - 1);
    ss->nm_ip[sizeof(ss->nm_ip) - 1] = '\0';
    ss->nm_port = nm_port;
    ss->client_port = client_port;
    ss->nm_socket_fd = -1;
    ss->client_socket_fd = -1;
    ss->file_count = 0;
    ss->is_running = true;
    memset(ss->files, 0, sizeof(ss->files));
    
    // Initialize locks
    pthread_mutex_init(&ss->files_lock, NULL);
    pthread_mutex_init(&ss->log_lock, NULL);
    pthread_mutex_init(&ss->undo_stack.lock, NULL);
    
    // Initialize undo stack
    ss->undo_stack.top = -1;
    for (int i = 0; i < UNDO_HISTORY_SIZE; i++) {
        ss->undo_stack.entries[i].content = NULL;
    }
    
    // Open log file
    ss->log_file = fopen(LOG_FILE, "a");
    if (!ss->log_file) {
        perror("Failed to open log file");
        free(ss);
        return NULL;
    }
    
    // Ensure storage directory exists
    ensure_storage_dir();
    
    // Load existing files
    load_all_files(ss);
    
    log_message(ss, "INFO", "INIT", "Storage Server initialized");
    printf("Storage Server initialized (Client port: %d)\n", client_port);
    printf("Loaded %d files from storage\n", ss->file_count);
    
    return ss;
}

void destroy_storage_server(StorageServer* ss) {
    if (!ss) return;
    
    ss->is_running = false;
    
    // Close sockets
    if (ss->nm_socket_fd >= 0) {
        close(ss->nm_socket_fd);
    }
    if (ss->client_socket_fd >= 0) {
        close(ss->client_socket_fd);
    }
    
    // Free files
    for (int i = 0; i < ss->file_count; i++) {
        if (ss->files[i]) {
            free_all_sentences(ss->files[i]);
            pthread_rwlock_destroy(&ss->files[i]->file_lock);
            pthread_mutex_destroy(&ss->files[i]->structure_lock);
            free(ss->files[i]);
        }
    }
    
    // Free undo stack
    for (int i = 0; i < UNDO_HISTORY_SIZE; i++) {
        if (ss->undo_stack.entries[i].content) {
            free(ss->undo_stack.entries[i].content);
        }
    }
    
    // Close log file
    if (ss->log_file) {
        fclose(ss->log_file);
    }
    
    // Destroy locks
    pthread_mutex_destroy(&ss->files_lock);
    pthread_mutex_destroy(&ss->log_lock);
    pthread_mutex_destroy(&ss->undo_stack.lock);
    
    free(ss);
    printf("Storage Server destroyed\n");
}

// Continued in storage_server_net.c...
