#include "storage_server.h"

// ==================== SENTENCE LOCKING AND WRITE OPERATIONS ====================

ErrorCode lock_sentence(StorageServer* ss, const char* filename, int sentence_num, int client_id) {
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }
    
    if (sentence_num < 0 || sentence_num >= file->sentence_count) {
        return ERR_INVALID_SENTENCE;
    }
    
    Sentence* sent = &file->sentences[sentence_num];
    
    pthread_mutex_lock(&sent->lock);
    
    if (sent->is_locked && sent->lock_holder_id != client_id) {
        pthread_mutex_unlock(&sent->lock);
        return ERR_FILE_LOCKED;
    }
    
    sent->is_locked = true;
    sent->lock_holder_id = client_id;
    
    pthread_mutex_unlock(&sent->lock);
    
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
    
    Sentence* sent = &file->sentences[sentence_num];
    
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
    
    // Lock the file for writing
    pthread_rwlock_wrlock(&file->file_lock);
    
    // Save current state for undo
    char old_content[MAX_CONTENT_SIZE];
    rebuild_file_content(file, old_content);
    push_undo(ss, filename, old_content);
    
    if (sentence_num < 0 || sentence_num >= file->sentence_count) {
        // If sentence doesn't exist, create new sentences
        if (sentence_num == file->sentence_count) {
            // Extend sentence array
            file->sentences = (Sentence*)realloc(file->sentences, 
                                                (file->sentence_count + 1) * sizeof(Sentence));
            Sentence* new_sent = &file->sentences[file->sentence_count];
            new_sent->content = strdup("");
            new_sent->length = 0;
            new_sent->word_count = 0;
            new_sent->is_locked = false;
            new_sent->lock_holder_id = -1;
            pthread_mutex_init(&new_sent->lock, NULL);
            file->sentence_count++;
        } else {
            pthread_rwlock_unlock(&file->file_lock);
            return ERR_INVALID_SENTENCE;
        }
    }
    
    Sentence* sent = &file->sentences[sentence_num];
    
    // Check if sentence is locked by this client
    pthread_mutex_lock(&sent->lock);
    if (sent->is_locked && sent->lock_holder_id != client_id) {
        pthread_mutex_unlock(&sent->lock);
        pthread_rwlock_unlock(&file->file_lock);
        return ERR_FILE_LOCKED;
    }
    pthread_mutex_unlock(&sent->lock);
    
    // Parse current sentence into words
    char* words[1000];
    int word_count = 0;
    char sent_copy[MAX_CONTENT_SIZE];
    strncpy(sent_copy, sent->content, MAX_CONTENT_SIZE - 1);
    sent_copy[MAX_CONTENT_SIZE - 1] = '\0';
    
    char* token = strtok(sent_copy, " \t\n");
    while (token && word_count < 1000) {
        words[word_count++] = strdup(token);
        token = strtok(NULL, " \t\n");
    }
    
    // Update word at index
    if (word_index >= 0 && word_index < word_count) {
        free(words[word_index]);
        words[word_index] = strdup(new_content);
    } else if (word_index == word_count) {
        // Append new word
        words[word_count++] = strdup(new_content);
    } else {
        // Free allocated words
        for (int i = 0; i < word_count; i++) {
            free(words[i]);
        }
        pthread_rwlock_unlock(&file->file_lock);
        return ERR_INVALID_OPERATION;
    }
    
    // Rebuild sentence with new content
    char new_sentence[MAX_CONTENT_SIZE] = {0};
    int offset = 0;
    
    for (int i = 0; i < word_count; i++) {
        if (i > 0) {
            new_sentence[offset++] = ' ';
        }
        strcpy(new_sentence + offset, words[i]);
        offset += strlen(words[i]);
        
        // Check if word contains sentence delimiter
        int word_len = strlen(words[i]);
        if (word_len > 0 && is_sentence_delimiter(words[i][word_len - 1])) {
            // Split into new sentences if delimiter found
            // For now, keep it simple and just update the content
        }
        
        free(words[i]);
    }
    
    // Update sentence content
    free(sent->content);
    sent->content = strdup(new_sentence);
    sent->length = strlen(new_sentence);
    sent->word_count = count_words(new_sentence);
    
    // Reparse if delimiters were added (creates new sentences)
    char full_content[MAX_CONTENT_SIZE];
    rebuild_file_content(file, full_content);
    
    // Free old sentences
    for (int i = 0; i < file->sentence_count; i++) {
        free(file->sentences[i].content);
        pthread_mutex_destroy(&file->sentences[i].lock);
    }
    free(file->sentences);
    
    // Reparse with new content
    parse_sentences(file, full_content);
    
    // Save to disk
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
    
    // Send file word by word with 0.1s delay
    for (int i = 0; i < file->sentence_count; i++) {
        Sentence* sent = &file->sentences[i];
        
        // Parse sentence into words
        char sent_copy[MAX_CONTENT_SIZE];
        strncpy(sent_copy, sent->content, MAX_CONTENT_SIZE - 1);
        sent_copy[MAX_CONTENT_SIZE - 1] = '\0';
        
        char* token = strtok(sent_copy, " \t\n");
        while (token) {
            // Send word
            char word_msg[BUFFER_SIZE];
            snprintf(word_msg, sizeof(word_msg), "%s\n", token);
            
            ssize_t sent_bytes = send(client_fd, word_msg, strlen(word_msg), MSG_NOSIGNAL);
            if (sent_bytes <= 0) {
                // Client disconnected
                pthread_rwlock_unlock(&file->file_lock);
                return ERR_SYSTEM_ERROR;
            }
            
            // 0.1 second delay
            usleep(100000);
            
            token = strtok(NULL, " \t\n");
        }
    }
    
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
    
    // Free current sentences
    for (int i = 0; i < file->sentence_count; i++) {
        free(file->sentences[i].content);
        pthread_mutex_destroy(&file->sentences[i].lock);
    }
    free(file->sentences);
    
    // Reparse with undo content
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
    ss->file_count = 0;
    ss->is_running = true;
    
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
            for (int j = 0; j < ss->files[i]->sentence_count; j++) {
                free(ss->files[i]->sentences[j].content);
                pthread_mutex_destroy(&ss->files[i]->sentences[j].lock);
            }
            free(ss->files[i]->sentences);
            pthread_rwlock_destroy(&ss->files[i]->file_lock);
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
