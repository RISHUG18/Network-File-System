#include "storage_server.h"

// ==================== UTILITY FUNCTIONS ====================

const char* error_to_string(ErrorCode error) {
    switch (error) {
        case ERR_SUCCESS: return "SUCCESS";
        case ERR_FILE_NOT_FOUND: return "File not found";
        case ERR_UNAUTHORIZED: return "Unauthorized access";
        case ERR_FILE_EXISTS: return "File already exists";
        case ERR_FILE_LOCKED: return "Sentence is locked";
        case ERR_INVALID_OPERATION: return "Invalid operation";
        case ERR_INVALID_SENTENCE: return "Invalid sentence number";
        case ERR_SYSTEM_ERROR: return "System error";
        default: return "Unknown error";
    }
}

void log_message(StorageServer* ss, const char* level, const char* operation, 
                const char* details) {
    pthread_mutex_lock(&ss->log_lock);
    
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    fprintf(ss->log_file, "[%s] [%s] Op=%s Details=%s\n",
            timestamp, level, operation, details ? details : "");
    fflush(ss->log_file);
    
    printf("[%s] [%s] %s - %s\n", timestamp, level, operation, 
           details ? details : "");
    
    pthread_mutex_unlock(&ss->log_lock);
}

void parse_command(const char* command, char* cmd, char* args[], int* arg_count) {
    char buffer[BUFFER_SIZE];
    strncpy(buffer, command, BUFFER_SIZE - 1);
    buffer[BUFFER_SIZE - 1] = '\0';
    
    *arg_count = 0;
    char* token = strtok(buffer, " \t\n");
    
    if (token) {
        strcpy(cmd, token);
        token = strtok(NULL, " \t\n");
        
        while (token && *arg_count < 10) {
            args[*arg_count] = strdup(token);
            (*arg_count)++;
            token = strtok(NULL, " \t\n");
        }
    }
}

void ensure_storage_dir() {
    struct stat st = {0};
    if (stat(STORAGE_DIR, &st) == -1) {
        mkdir(STORAGE_DIR, 0700);
    }
}

// ==================== SENTENCE OPERATIONS ====================

bool is_sentence_delimiter(char c) {
    return (c == '.' || c == '!' || c == '?');
}

int count_words(const char* text) {
    if (!text || strlen(text) == 0) return 0;
    
    int count = 0;
    bool in_word = false;
    
    for (int i = 0; text[i] != '\0'; i++) {
        if (text[i] == ' ' || text[i] == '\t' || text[i] == '\n') {
            in_word = false;
        } else {
            if (!in_word) {
                count++;
                in_word = true;
            }
        }
    }
    
    return count;
}

void parse_sentences(FileEntry* file, const char* content) {
    if (!content || strlen(content) == 0) {
        file->sentence_count = 0;
        file->sentences = NULL;
        file->total_size = 0;
        file->total_words = 0;
        file->total_chars = 0;
        return;
    }
    
    // Count sentences first
    int sent_count = 0;
    int len = strlen(content);
    for (int i = 0; i < len; i++) {
        if (is_sentence_delimiter(content[i])) {
            sent_count++;
        }
    }
    
    // If no delimiters, treat whole content as one sentence
    if (sent_count == 0) sent_count = 1;
    
    // Allocate sentence array
    file->sentences = (Sentence*)calloc(sent_count, sizeof(Sentence));
    file->sentence_count = sent_count;
    
    // Parse sentences
    int sent_idx = 0;
    int start = 0;
    
    for (int i = 0; i < len; i++) {
        if (is_sentence_delimiter(content[i]) || i == len - 1) {
            int end = i + 1;
            if (i == len - 1 && !is_sentence_delimiter(content[i])) {
                end = len;
            }
            
            int sent_len = end - start;
            if (sent_len > 0) {
                file->sentences[sent_idx].content = (char*)malloc(sent_len + 1);
                strncpy(file->sentences[sent_idx].content, content + start, sent_len);
                file->sentences[sent_idx].content[sent_len] = '\0';
                file->sentences[sent_idx].length = sent_len;
                file->sentences[sent_idx].word_count = count_words(file->sentences[sent_idx].content);
                file->sentences[sent_idx].is_locked = false;
                file->sentences[sent_idx].lock_holder_id = -1;
                pthread_mutex_init(&file->sentences[sent_idx].lock, NULL);
                
                sent_idx++;
                start = end;
                
                // Skip whitespace after delimiter
                while (start < len && (content[start] == ' ' || content[start] == '\n')) {
                    start++;
                }
                i = start - 1;
            }
            
            if (sent_idx >= sent_count) break;
        }
    }
    
    file->sentence_count = sent_idx;
    
    // Calculate totals
    file->total_size = len;
    file->total_chars = len;
    file->total_words = 0;
    for (int i = 0; i < file->sentence_count; i++) {
        file->total_words += file->sentences[i].word_count;
    }
}

void rebuild_file_content(FileEntry* file, char* content) {
    content[0] = '\0';
    int offset = 0;
    
    for (int i = 0; i < file->sentence_count; i++) {
        if (file->sentences[i].content) {
            strcpy(content + offset, file->sentences[i].content);
            offset += file->sentences[i].length;
            
            // Add space between sentences if needed
            if (i < file->sentence_count - 1 && 
                file->sentences[i].content[file->sentences[i].length - 1] != ' ') {
                content[offset++] = ' ';
            }
        }
    }
    content[offset] = '\0';
}

// ==================== FILE PERSISTENCE ====================

bool save_file_to_disk(FileEntry* file) {
    char content[MAX_CONTENT_SIZE];
    rebuild_file_content(file, content);
    
    FILE* fp = fopen(file->filepath, "w");
    if (!fp) {
        return false;
    }
    
    fwrite(content, 1, strlen(content), fp);
    fclose(fp);
    
    return true;
}

bool load_file_from_disk(StorageServer* ss, const char* filename) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_DIR, filename);
    
    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        return false;
    }
    
    // Read file content
    char content[MAX_CONTENT_SIZE];
    size_t bytes_read = fread(content, 1, MAX_CONTENT_SIZE - 1, fp);
    content[bytes_read] = '\0';
    fclose(fp);
    
    // Create file entry
    FileEntry* file = (FileEntry*)malloc(sizeof(FileEntry));
    strncpy(file->filename, filename, MAX_FILENAME - 1);
    file->filename[MAX_FILENAME - 1] = '\0';
    strncpy(file->filepath, filepath, MAX_PATH - 1);
    file->filepath[MAX_PATH - 1] = '\0';
    
    pthread_rwlock_init(&file->file_lock, NULL);
    
    struct stat st;
    if (stat(filepath, &st) == 0) {
        file->last_modified = st.st_mtime;
        file->last_accessed = st.st_atime;
    } else {
        file->last_modified = time(NULL);
        file->last_accessed = time(NULL);
    }
    
    // Parse sentences
    parse_sentences(file, content);
    
    // Add to file list
    pthread_mutex_lock(&ss->files_lock);
    if (ss->file_count < MAX_FILES) {
        ss->files[ss->file_count++] = file;
        pthread_mutex_unlock(&ss->files_lock);
        return true;
    }
    pthread_mutex_unlock(&ss->files_lock);
    
    return false;
}

void load_all_files(StorageServer* ss) {
    DIR* dir = opendir(STORAGE_DIR);
    if (!dir) {
        return;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  // Regular file
            load_file_from_disk(ss, entry->d_name);
        }
    }
    
    closedir(dir);
}

// ==================== UNDO STACK ====================

void push_undo(StorageServer* ss, const char* filename, const char* content) {
    pthread_mutex_lock(&ss->undo_stack.lock);
    
    // Move to next position (circular buffer)
    ss->undo_stack.top = (ss->undo_stack.top + 1) % UNDO_HISTORY_SIZE;
    
    UndoEntry* entry = &ss->undo_stack.entries[ss->undo_stack.top];
    
    // Free old content if exists
    if (entry->content) {
        free(entry->content);
    }
    
    // Store new undo entry
    strncpy(entry->filename, filename, MAX_FILENAME - 1);
    entry->filename[MAX_FILENAME - 1] = '\0';
    entry->content = strdup(content);
    entry->timestamp = time(NULL);
    
    pthread_mutex_unlock(&ss->undo_stack.lock);
}

ErrorCode pop_undo(StorageServer* ss, const char* filename, char* content) {
    pthread_mutex_lock(&ss->undo_stack.lock);
    
    // Search for most recent undo for this file
    int idx = ss->undo_stack.top;
    bool found = false;
    
    for (int i = 0; i < UNDO_HISTORY_SIZE; i++) {
        if (ss->undo_stack.entries[idx].content &&
            strcmp(ss->undo_stack.entries[idx].filename, filename) == 0) {
            found = true;
            break;
        }
        idx = (idx - 1 + UNDO_HISTORY_SIZE) % UNDO_HISTORY_SIZE;
    }
    
    if (!found) {
        pthread_mutex_unlock(&ss->undo_stack.lock);
        return ERR_SYSTEM_ERROR;
    }
    
    // Copy content
    strcpy(content, ss->undo_stack.entries[idx].content);
    
    // Clear this undo entry
    free(ss->undo_stack.entries[idx].content);
    ss->undo_stack.entries[idx].content = NULL;
    
    pthread_mutex_unlock(&ss->undo_stack.lock);
    
    return ERR_SUCCESS;
}

// ==================== FILE OPERATIONS ====================

FileEntry* find_file(StorageServer* ss, const char* filename) {
    pthread_mutex_lock(&ss->files_lock);
    
    for (int i = 0; i < ss->file_count; i++) {
        if (ss->files[i] && strcmp(ss->files[i]->filename, filename) == 0) {
            pthread_mutex_unlock(&ss->files_lock);
            return ss->files[i];
        }
    }
    
    pthread_mutex_unlock(&ss->files_lock);
    return NULL;
}

FileEntry* create_file(StorageServer* ss, const char* filename) {
    // Check if file already exists
    if (find_file(ss, filename)) {
        return NULL;
    }
    
    pthread_mutex_lock(&ss->files_lock);
    
    if (ss->file_count >= MAX_FILES) {
        pthread_mutex_unlock(&ss->files_lock);
        return NULL;
    }
    
    // Create file entry
    FileEntry* file = (FileEntry*)malloc(sizeof(FileEntry));
    strncpy(file->filename, filename, MAX_FILENAME - 1);
    file->filename[MAX_FILENAME - 1] = '\0';
    
    snprintf(file->filepath, sizeof(file->filepath), "%s/%s", STORAGE_DIR, filename);
    
    file->sentences = NULL;
    file->sentence_count = 0;
    file->total_size = 0;
    file->total_words = 0;
    file->total_chars = 0;
    file->last_modified = time(NULL);
    file->last_accessed = time(NULL);
    
    pthread_rwlock_init(&file->file_lock, NULL);
    
    // Create empty file on disk
    FILE* fp = fopen(file->filepath, "w");
    if (fp) {
        fclose(fp);
    }
    
    ss->files[ss->file_count++] = file;
    
    pthread_mutex_unlock(&ss->files_lock);
    
    char details[256];
    snprintf(details, sizeof(details), "File=%s", filename);
    log_message(ss, "INFO", "CREATE", details);
    
    return file;
}

ErrorCode delete_file(StorageServer* ss, const char* filename) {
    pthread_mutex_lock(&ss->files_lock);
    
    for (int i = 0; i < ss->file_count; i++) {
        if (ss->files[i] && strcmp(ss->files[i]->filename, filename) == 0) {
            FileEntry* file = ss->files[i];
            
            // Delete file from disk
            unlink(file->filepath);
            
            // Free sentences
            for (int j = 0; j < file->sentence_count; j++) {
                if (file->sentences[j].content) {
                    free(file->sentences[j].content);
                }
                pthread_mutex_destroy(&file->sentences[j].lock);
            }
            free(file->sentences);
            
            pthread_rwlock_destroy(&file->file_lock);
            free(file);
            
            // Shift array
            for (int j = i; j < ss->file_count - 1; j++) {
                ss->files[j] = ss->files[j + 1];
            }
            ss->file_count--;
            
            pthread_mutex_unlock(&ss->files_lock);
            
            char details[256];
            snprintf(details, sizeof(details), "File=%s", filename);
            log_message(ss, "INFO", "DELETE", details);
            
            return ERR_SUCCESS;
        }
    }
    
    pthread_mutex_unlock(&ss->files_lock);
    return ERR_FILE_NOT_FOUND;
}

ErrorCode read_file(StorageServer* ss, const char* filename, char* content, size_t* size) {
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }
    
    pthread_rwlock_rdlock(&file->file_lock);
    
    rebuild_file_content(file, content);
    *size = strlen(content);
    
    file->last_accessed = time(NULL);
    
    pthread_rwlock_unlock(&file->file_lock);
    
    return ERR_SUCCESS;
}

// Continued in next part...
