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
        // Create one empty sentence for new files
        file->sentence_count = 1;
        file->sentences = (Sentence*)calloc(1, sizeof(Sentence));
        file->sentences[0].content = strdup("");
        file->sentences[0].length = 0;
        file->sentences[0].word_count = 0;
        file->sentences[0].is_locked = false;
        file->sentences[0].lock_holder_id = -1;
        pthread_mutex_init(&file->sentences[0].lock, NULL);
        file->total_size = 0;
        file->total_words = 0;
        file->total_chars = 0;
        return;
    }
    
    size_t len = strlen(content);
    Sentence* sentences = NULL;
    int capacity = 0;
    int count = 0;

    size_t index = 0;
    while (index < len) {
        while (index < len && (content[index] == ' ' || content[index] == '\n' || content[index] == '\t')) {
            index++;
        }

        if (index >= len) {
            break;
        }

        size_t start = index;

        // Find next delimiter or end of string
        while (index < len && !is_sentence_delimiter(content[index])) {
            index++;
        }

        bool has_delimiter = (index < len && is_sentence_delimiter(content[index]));
        size_t end = index;

        if (has_delimiter) {
            end++; // include delimiter
            index++; // move past delimiter
        }

        size_t sentence_len = end - start;

        // Skip whitespace before the next sentence
        while (index < len && (content[index] == ' ' || content[index] == '\n' || content[index] == '\t')) {
            index++;
        }

        // Ignore empty segments caused by consecutive delimiters or whitespace
        if (sentence_len == 0) {
            continue;
        }

        if (count == capacity) {
            int new_capacity = (capacity == 0) ? 4 : capacity * 2;
            Sentence* resized = (Sentence*)realloc(sentences, new_capacity * sizeof(Sentence));
            if (!resized) {
                for (int i = 0; i < count; i++) {
                    free(sentences[i].content);
                    pthread_mutex_destroy(&sentences[i].lock);
                }
                free(sentences);

                sentences = (Sentence*)calloc(1, sizeof(Sentence));
                sentences[0].content = strdup("");
                sentences[0].length = 0;
                sentences[0].word_count = 0;
                sentences[0].is_locked = false;
                sentences[0].lock_holder_id = -1;
                pthread_mutex_init(&sentences[0].lock, NULL);

                file->sentences = sentences;
                file->sentence_count = 1;
                refresh_file_stats(file);
                return;
            }
            sentences = resized;
            capacity = new_capacity;
        }

        Sentence* sentence = &sentences[count];
        memset(sentence, 0, sizeof(Sentence));

        sentence->content = (char*)malloc(sentence_len + 1);
        memcpy(sentence->content, content + start, sentence_len);
        sentence->content[sentence_len] = '\0';
        sentence->length = (int)sentence_len;
        sentence->word_count = count_words(sentence->content);
        sentence->is_locked = false;
        sentence->lock_holder_id = -1;
        pthread_mutex_init(&sentence->lock, NULL);

        count++;
    }

    if (count == 0) {
        // Fallback to single empty sentence if parsing produced nothing
        sentences = (Sentence*)calloc(1, sizeof(Sentence));
        sentences[0].content = strdup("");
        sentences[0].length = 0;
        sentences[0].word_count = 0;
        sentences[0].is_locked = false;
        sentences[0].lock_holder_id = -1;
        pthread_mutex_init(&sentences[0].lock, NULL);
        count = 1;
    }

    file->sentences = sentences;
    file->sentence_count = count;
    refresh_file_stats(file);
}

void rebuild_file_content(FileEntry* file, char* content) {
    size_t offset = 0;
    content[0] = '\0';
    
    for (int i = 0; i < file->sentence_count; i++) {
        Sentence* sentence = &file->sentences[i];
        if (!sentence->content) {
            continue;
        }

        if (sentence->length > 0) {
            memcpy(content + offset, sentence->content, sentence->length);
            offset += (size_t)sentence->length;
            content[offset] = '\0';
        }

        bool add_space = false;
        if (i < file->sentence_count - 1) {
            Sentence* next_sentence = &file->sentences[i + 1];
            if (sentence->length > 0 &&
                sentence->content[sentence->length - 1] != ' ' &&
                next_sentence->content && next_sentence->length > 0) {
                add_space = true;
            }
        }

        if (add_space) {
            content[offset++] = ' ';
            content[offset] = '\0';
        }
    }

    content[offset] = '\0';
}

void refresh_file_stats(FileEntry* file) {
    size_t total_chars = 0;
    int total_words = 0;

    for (int i = 0; i < file->sentence_count; i++) {
        Sentence* sentence = &file->sentences[i];
        if (!sentence->content) {
            continue;
        }

        if (sentence->length > 0) {
            total_chars += (size_t)sentence->length;
        }

        total_words += sentence->word_count;

        if (i < file->sentence_count - 1) {
            Sentence* next_sentence = &file->sentences[i + 1];
            if (sentence->length > 0 &&
                sentence->content[sentence->length - 1] != ' ' &&
                next_sentence->content && next_sentence->length > 0) {
                total_chars += 1;
            }
        }
    }

    file->total_chars = total_chars;
    file->total_size = total_chars;
    file->total_words = total_words;
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
    
    // Initialize with one empty sentence
    file->sentences = (Sentence*)malloc(sizeof(Sentence));
    file->sentence_count = 1;
    
    // Initialize the first sentence
    file->sentences[0].content = strdup("");
    file->sentences[0].length = 0;
    file->sentences[0].word_count = 0;
    file->sentences[0].is_locked = false;
    file->sentences[0].lock_holder_id = -1;
    pthread_mutex_init(&file->sentences[0].lock, NULL);
    
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
