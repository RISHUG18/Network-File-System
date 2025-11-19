#include "storage_server.h"

#ifndef DT_REG
#define DT_REG 8
#endif

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

// ==================== UTILITY FUNCTIONS ====================

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

// ==================== SENTENCE NODE OPERATIONS (LINKED LIST) ====================

SentenceNode* create_sentence_node(const char** word_array, int word_count, char delimiter) {
    SentenceNode* node = (SentenceNode*)calloc(1, sizeof(SentenceNode));
    if (!node) {
        return NULL;
    }
    
    node->word_count = word_count;
    node->word_capacity = (word_count > 0) ? word_count : 4;
    node->delimiter = delimiter;
    node->is_locked = false;
    node->lock_holder_id = -1;
    node->next = NULL;
    node->prev = NULL;
    node->draft_head = NULL;
    node->draft_dirty = false;
    
    pthread_mutex_init(&node->lock, NULL);
    
    // Allocate words array
    if (node->word_capacity > 0) {
        node->words = (char**)calloc(node->word_capacity, sizeof(char*));
        if (!node->words) {
            pthread_mutex_destroy(&node->lock);
            free(node);
            return NULL;
        }
        
        // Copy words
        for (int i = 0; i < word_count; i++) {
            node->words[i] = strdup(word_array[i]);
            if (!node->words[i]) {
                // Cleanup on failure
                for (int j = 0; j < i; j++) {
                    free(node->words[j]);
                }
                free(node->words);
                pthread_mutex_destroy(&node->lock);
                free(node);
                return NULL;
            }
        }
    } else {
        node->words = NULL;
    }
    
    return node;
}

SentenceNode* create_empty_sentence_node() {
    SentenceNode* node = (SentenceNode*)calloc(1, sizeof(SentenceNode));
    if (!node) {
        return NULL;
    }
    
    node->word_count = 0;
    node->word_capacity = 4;
    node->delimiter = '\0';
    node->is_locked = false;
    node->lock_holder_id = -1;
    node->next = NULL;
    node->prev = NULL;
    node->draft_head = NULL;
    node->draft_dirty = false;
    
    pthread_mutex_init(&node->lock, NULL);
    
    node->words = (char**)calloc(node->word_capacity, sizeof(char*));
    if (!node->words) {
        pthread_mutex_destroy(&node->lock);
        free(node);
        return NULL;
    }
    
    return node;
}

void free_draft_sentences(DraftSentence* head) {
    DraftSentence* current = head;
    while (current) {
        for (int i = 0; i < current->word_count; i++) {
            free(current->words[i]);
        }
        free(current->words);
        DraftSentence* next = current->next;
        free(current);
        current = next;
    }
}

void free_sentence_node(SentenceNode* node) {
    if (!node) return;
    
    // Free all words
    if (node->words) {
        for (int i = 0; i < node->word_count; i++) {
            if (node->words[i]) {
                free(node->words[i]);
            }
        }
        free(node->words);
    }

    if (node->draft_head) {
        free_draft_sentences(node->draft_head);
    }
    
    pthread_mutex_destroy(&node->lock);
    free(node);
}

void append_sentence(FileEntry* file, SentenceNode* node) {
    if (!file || !node) return;
    
    // Lock structure modification
    pthread_mutex_lock(&file->structure_lock);
    
    if (!file->head) {
        // First sentence
        file->head = node;
        file->tail = node;
        node->prev = NULL;
        node->next = NULL;
    } else {
        // Append to tail
        node->prev = file->tail;
        node->next = NULL;
        file->tail->next = node;
        file->tail = node;
    }
    
    file->sentence_count++;
    
    pthread_mutex_unlock(&file->structure_lock);
}

void insert_sentence_after(FileEntry* file, SentenceNode* prev, SentenceNode* new_node) {
    if (!file || !new_node) return;
    
    pthread_mutex_lock(&file->structure_lock);
    
    if (!prev || !file->head) {
        // Insert at beginning if prev is NULL
        new_node->next = file->head;
        new_node->prev = NULL;
        
        if (file->head) {
            file->head->prev = new_node;
        } else {
            file->tail = new_node;
        }
        
        file->head = new_node;
    } else {
        // Insert after prev
        new_node->next = prev->next;
        new_node->prev = prev;
        
        if (prev->next) {
            prev->next->prev = new_node;
        } else {
            file->tail = new_node;
        }
        
        prev->next = new_node;
    }
    
    file->sentence_count++;
    
    pthread_mutex_unlock(&file->structure_lock);
}

void delete_sentence_node(FileEntry* file, SentenceNode* node) {
    if (!file || !node) return;
    
    pthread_mutex_lock(&file->structure_lock);
    
    // Update links
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        // Deleting head
        file->head = node->next;
    }
    
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        // Deleting tail
        file->tail = node->prev;
    }
    
    file->sentence_count--;
    
    pthread_mutex_unlock(&file->structure_lock);
    
    // Free the node (done outside structure lock to avoid holding lock too long)
    free_sentence_node(node);
}

SentenceNode* get_sentence_by_index(FileEntry* file, int index) {
    if (!file || index < 0) return NULL;
    
    pthread_mutex_lock(&file->structure_lock);
    
    SentenceNode* current = file->head;
    int i = 0;
    
    while (current && i < index) {
        current = current->next;
        i++;
    }
    
    pthread_mutex_unlock(&file->structure_lock);
    
    return current;
}

void free_all_sentences(FileEntry* file) {
    if (!file) return;
    
    pthread_mutex_lock(&file->structure_lock);
    
    SentenceNode* current = file->head;
    while (current) {
        SentenceNode* next = current->next;
        free_sentence_node(current);
        current = next;
    }
    
    file->head = NULL;
    file->tail = NULL;
    file->sentence_count = 0;
    
    pthread_mutex_unlock(&file->structure_lock);
}

// ==================== WORD OPERATIONS WITHIN SENTENCE ====================

bool insert_word_in_sentence(SentenceNode* sentence, int index, const char* word) {
    if (!sentence || !word || index < 0 || index > sentence->word_count) {
        return false;
    }
    
    // Expand capacity if needed
    if (sentence->word_count >= sentence->word_capacity) {
        int new_capacity = sentence->word_capacity * 2;
        char** new_words = (char**)realloc(sentence->words, new_capacity * sizeof(char*));
        if (!new_words) {
            return false;
        }
        sentence->words = new_words;
        sentence->word_capacity = new_capacity;
    }
    
    // Shift words to the right
    for (int i = sentence->word_count; i > index; i--) {
        sentence->words[i] = sentence->words[i - 1];
    }
    
    // Insert new word
    sentence->words[index] = strdup(word);
    if (!sentence->words[index]) {
        // Shift back on failure
        for (int i = index; i < sentence->word_count; i++) {
            sentence->words[i] = sentence->words[i + 1];
        }
        return false;
    }
    
    sentence->word_count++;
    return true;
}

bool delete_word_in_sentence(SentenceNode* sentence, int index) {
    if (!sentence || index < 0 || index >= sentence->word_count) {
        return false;
    }
    
    // Free the word
    free(sentence->words[index]);
    
    // Shift words to the left
    for (int i = index; i < sentence->word_count - 1; i++) {
        sentence->words[i] = sentence->words[i + 1];
    }
    
    sentence->word_count--;
    sentence->words[sentence->word_count] = NULL;
    
    return true;
}

bool replace_word_in_sentence(SentenceNode* sentence, int index, const char* word) {
    if (!sentence || !word || index < 0 || index >= sentence->word_count) {
        return false;
    }
    
    char* new_word = strdup(word);
    if (!new_word) {
        return false;
    }
    
    free(sentence->words[index]);
    sentence->words[index] = new_word;
    
    return true;
}

void parse_sentences(FileEntry* file, const char* content) {
    if (!file) return;
    
    // Clear existing sentences
    free_all_sentences(file);
    
    if (!content || strlen(content) == 0) {
        // Create one empty sentence for new files
        SentenceNode* node = create_empty_sentence_node();
        if (node) {
            append_sentence(file, node);
        }
        file->total_size = 0;
        file->total_words = 0;
        file->total_chars = 0;
        return;
    }
    
    size_t len = strlen(content);
    size_t index = 0;
    
    while (index < len) {
        // Skip leading whitespace
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

        char delimiter = '\0';
        if (index < len && is_sentence_delimiter(content[index])) {
            delimiter = content[index];
            index++; // Move past delimiter
        }

        // Extract sentence text (without delimiter)
        size_t sentence_len = (delimiter != '\0') ? (index - start - 1) : (index - start);
        
        // Extract and parse words from sentence
        char sentence_text[MAX_CONTENT_SIZE];
        if (sentence_len >= MAX_CONTENT_SIZE) {
            sentence_len = MAX_CONTENT_SIZE - 1;
        }
        memcpy(sentence_text, content + start, sentence_len);
        sentence_text[sentence_len] = '\0';

        // Parse into words
        char* words[1000];
        int word_count = 0;
        
        char sentence_copy[MAX_CONTENT_SIZE];
        strncpy(sentence_copy, sentence_text, MAX_CONTENT_SIZE - 1);
        sentence_copy[MAX_CONTENT_SIZE - 1] = '\0';
        
        char* token = strtok(sentence_copy, " \t\n");
        while (token && word_count < 1000) {
            words[word_count++] = token;
            token = strtok(NULL, " \t\n");
        }

        // Create sentence node
        SentenceNode* node = create_sentence_node((const char**)words, word_count, delimiter);
        if (node) {
            append_sentence(file, node);
        }
    }

    // If the last sentence has a delimiter, append an empty sentence
    if (file->tail && file->tail->delimiter != '\0') {
        SentenceNode* node = create_empty_sentence_node();
        if (node) {
            append_sentence(file, node);
        }
    }

    // If no sentences were parsed, create one empty sentence
    if (file->sentence_count == 0) {
        SentenceNode* node = create_empty_sentence_node();
        if (node) {
            append_sentence(file, node);
        }
    }

    refresh_file_stats(file);
}

void rebuild_file_content(FileEntry* file, char* content) {
    if (!file || !content) return;
    
    size_t offset = 0;
    content[0] = '\0';
    
    pthread_mutex_lock(&file->structure_lock);
    
    SentenceNode* current = file->head;
    bool first = true;
    
    while (current) {
        // Add space before sentence (except first)
        if (!first && offset > 0 && content[offset - 1] != ' ') {
            content[offset++] = ' ';
        }
        first = false;
        
        pthread_mutex_lock(&current->lock);
        
        // Add all words in sentence
        for (int i = 0; i < current->word_count; i++) {
            if (i > 0) {
                content[offset++] = ' ';
            }
            
            const char* word = current->words[i];
            size_t word_len = strlen(word);
            
            if (offset + word_len < MAX_CONTENT_SIZE - 2) {
                memcpy(content + offset, word, word_len);
                offset += word_len;
            }
        }
        
        // Add delimiter if present
        if (current->delimiter != '\0') {
            content[offset++] = current->delimiter;
        }
        
        pthread_mutex_unlock(&current->lock);
        
        current = current->next;
    }
    
    pthread_mutex_unlock(&file->structure_lock);
    
    content[offset] = '\0';
}

void refresh_file_stats(FileEntry* file) {
    if (!file) return;
    
    size_t total_chars = 0;
    int total_words = 0;
    
    pthread_mutex_lock(&file->structure_lock);
    
    SentenceNode* current = file->head;
    bool first = true;
    
    while (current) {
        // Count space before sentence (except first)
        if (!first) {
            total_chars += 1;
        }
        first = false;
        
        pthread_mutex_lock(&current->lock);
        
        // Count words and characters
        total_words += current->word_count;
        
        for (int i = 0; i < current->word_count; i++) {
            if (i > 0) {
                total_chars += 1; // Space between words
            }
            total_chars += strlen(current->words[i]);
        }
        
        // Count delimiter
        if (current->delimiter != '\0') {
            total_chars += 1;
        }
        
        pthread_mutex_unlock(&current->lock);
        
        current = current->next;
    }
    
    pthread_mutex_unlock(&file->structure_lock);
    
    file->total_chars = (int)total_chars;
    file->total_size = total_chars;
    file->total_words = total_words;
}

// ==================== FILE PERSISTENCE ====================

static bool write_content_to_path(FileEntry* file, const char* path) {
    char content[MAX_CONTENT_SIZE];
    rebuild_file_content(file, content);

    FILE* fp = fopen(path, "w");
    if (!fp) {
        return false;
    }

    fwrite(content, 1, strlen(content), fp);
    fclose(fp);
    return true;
}

bool save_file_to_disk(FileEntry* file) {
    return write_content_to_path(file, file->filepath);
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

// ==================== CHECKPOINT MANAGEMENT ====================

static bool ensure_directory_exists(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    if (mkdir(path, 0700) == 0) {
        return true;
    }
    return errno == EEXIST;
}

static bool sanitize_checkpoint_tag(const char* tag, char* sanitized, size_t size) {
    if (!tag || !sanitized || size == 0) {
        return false;
    }
    size_t len = strlen(tag);
    if (len == 0 || len >= size) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)tag[i];
        if (isalnum(c) || c == '_' || c == '-' || c == '.') {
            sanitized[i] = (char)c;
        } else {
            return false;
        }
    }
    sanitized[len] = '\0';
    return true;
}

static bool build_checkpoint_dir(char* buffer, size_t size, const char* filename) {
    if (!buffer || size == 0 || !filename) {
        return false;
    }
    int written = snprintf(buffer, size, "%s/%s", CHECKPOINT_BASE_DIR, filename);
    return written > 0 && (size_t)written < size;
}

static bool build_checkpoint_path(char* buffer, size_t size, const char* filename, const char* tag) {
    char safe_tag[MAX_CHECKPOINT_TAG];
    if (!sanitize_checkpoint_tag(tag, safe_tag, sizeof(safe_tag))) {
        return false;
    }
    if (!buffer || size == 0 || !filename) {
        return false;
    }
    int written = snprintf(buffer, size, "%s/%s/%s.chk", CHECKPOINT_BASE_DIR, filename, safe_tag);
    return written > 0 && (size_t)written < size;
}

static bool ensure_checkpoint_directory(const char* filename) {
    if (!ensure_directory_exists(STORAGE_DIR)) {
        return false;
    }
    if (!ensure_directory_exists(CHECKPOINT_BASE_DIR)) {
        return false;
    }
    char dir_path[MAX_PATH];
    if (!build_checkpoint_dir(dir_path, sizeof(dir_path), filename)) {
        return false;
    }
    return ensure_directory_exists(dir_path);
}

ErrorCode create_checkpoint(StorageServer* ss, const char* filename, const char* tag) {
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }
    if (!ensure_checkpoint_directory(filename)) {
        return ERR_SYSTEM_ERROR;
    }

    char checkpoint_path[MAX_PATH];
    if (!build_checkpoint_path(checkpoint_path, sizeof(checkpoint_path), filename, tag)) {
        return ERR_INVALID_OPERATION;
    }

    struct stat st;
    if (stat(checkpoint_path, &st) == 0) {
        return ERR_FILE_EXISTS;
    }

    char content[MAX_CONTENT_SIZE];
    size_t size = 0;
    ErrorCode read_err = read_file(ss, filename, content, &size);
    if (read_err != ERR_SUCCESS) {
        return read_err;
    }

    FILE* fp = fopen(checkpoint_path, "w");
    if (!fp) {
        return ERR_SYSTEM_ERROR;
    }
    fwrite(content, 1, size, fp);
    fclose(fp);

    char details[256];
    snprintf(details, sizeof(details), "File=%s Tag=%s", filename, tag);
    log_message(ss, "INFO", "CHECKPOINT_CREATE", details);

    return ERR_SUCCESS;
}

ErrorCode view_checkpoint(StorageServer* ss, const char* filename, const char* tag,
                         char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return ERR_INVALID_OPERATION;
    }
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }

    char checkpoint_path[MAX_PATH];
    if (!build_checkpoint_path(checkpoint_path, sizeof(checkpoint_path), filename, tag)) {
        return ERR_INVALID_OPERATION;
    }

    FILE* fp = fopen(checkpoint_path, "r");
    if (!fp) {
        return ERR_FILE_NOT_FOUND;
    }

    size_t bytes_read = fread(buffer, 1, buffer_size - 1, fp);
    buffer[bytes_read] = '\0';
    bool truncated = !feof(fp);
    fclose(fp);

    if (truncated) {
        const char* suffix = "\n...[truncated]\n";
        size_t current_len = strlen(buffer);
        if (current_len + strlen(suffix) < buffer_size) {
            strncat(buffer, suffix, buffer_size - current_len - 1);
        } else if (buffer_size > 1) {
            buffer[buffer_size - 2] = '\n';
            buffer[buffer_size - 1] = '\0';
        }
    }

    return ERR_SUCCESS;
}

ErrorCode revert_to_checkpoint(StorageServer* ss, const char* filename, const char* tag) {
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }

    char checkpoint_path[MAX_PATH];
    if (!build_checkpoint_path(checkpoint_path, sizeof(checkpoint_path), filename, tag)) {
        return ERR_INVALID_OPERATION;
    }

    FILE* fp = fopen(checkpoint_path, "r");
    if (!fp) {
        return ERR_FILE_NOT_FOUND;
    }

    char snapshot[MAX_CONTENT_SIZE];
    size_t bytes_read = fread(snapshot, 1, MAX_CONTENT_SIZE - 1, fp);
    snapshot[bytes_read] = '\0';
    fclose(fp);

    pthread_rwlock_wrlock(&file->file_lock);

    char current_content[MAX_CONTENT_SIZE];
    rebuild_file_content(file, current_content);
    push_undo(ss, filename, current_content);

    parse_sentences(file, snapshot);
    file->last_modified = time(NULL);
    file->last_accessed = file->last_modified;
    save_file_to_disk(file);
    pthread_rwlock_unlock(&file->file_lock);

    char details[256];
    snprintf(details, sizeof(details), "File=%s Tag=%s", filename, tag);
    log_message(ss, "INFO", "CHECKPOINT_REVERT", details);

    return ERR_SUCCESS;
}

ErrorCode list_checkpoints(StorageServer* ss, const char* filename, char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return ERR_INVALID_OPERATION;
    }

    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }

    char dir_path[MAX_PATH];
    if (!build_checkpoint_dir(dir_path, sizeof(dir_path), filename)) {
        return ERR_SYSTEM_ERROR;
    }

    DIR* dir = opendir(dir_path);
    if (!dir) {
        snprintf(buffer, buffer_size, "No checkpoints found\n");
        return ERR_SUCCESS;
    }

    size_t offset = 0;
    offset += snprintf(buffer + offset, buffer_size - offset,
                       "Checkpoints for %s:\n", filename);
    offset += snprintf(buffer + offset, buffer_size - offset,
                       "%-20s %-20s\n", "TAG", "CREATED_AT");
    offset += snprintf(buffer + offset, buffer_size - offset,
                       "----------------------------------------\n");

    struct dirent* entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        const char* dot = strrchr(entry->d_name, '.');
        if (!dot || strcmp(dot, ".chk") != 0) {
            continue;
        }

        char tag[MAX_CHECKPOINT_TAG];
        size_t tag_len = (size_t)(dot - entry->d_name);
        if (tag_len >= sizeof(tag)) {
            tag_len = sizeof(tag) - 1;
        }
        memcpy(tag, entry->d_name, tag_len);
        tag[tag_len] = '\0';

    char file_path[MAX_PATH * 2];
    snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);
        struct stat st;
        time_t created = 0;
        if (stat(file_path, &st) == 0) {
            created = st.st_mtime;
        }

        char time_buf[32] = "-";
        if (created > 0) {
            struct tm* tm_info = localtime(&created);
            if (tm_info) {
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
            }
        }

        offset += snprintf(buffer + offset, buffer_size - offset,
                           "%-20s %-20s\n", tag, time_buf);
        if (offset >= buffer_size - 1) {
            break;
        }
        count++;
    }
    closedir(dir);

    if (count == 0) {
        snprintf(buffer, buffer_size, "No checkpoints found\n");
    }

    return ERR_SUCCESS;
}

void remove_all_checkpoints(const char* filename) {
    char dir_path[MAX_PATH];
    if (!build_checkpoint_dir(dir_path, sizeof(dir_path), filename)) {
        return;
    }

    DIR* dir = opendir(dir_path);
    if (!dir) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
    char file_path[MAX_PATH * 2];
    snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry->d_name);
        unlink(file_path);
    }
    closedir(dir);
    rmdir(dir_path);
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
    
    // Initialize empty linked list
    file->head = NULL;
    file->tail = NULL;
    file->sentence_count = 0;
    
    file->total_size = 0;
    file->total_words = 0;
    file->total_chars = 0;
    file->last_modified = time(NULL);
    file->last_accessed = time(NULL);
    
    pthread_rwlock_init(&file->file_lock, NULL);
    pthread_mutex_init(&file->structure_lock, NULL);
    
    // Create one empty sentence
    SentenceNode* empty_node = create_empty_sentence_node();
    if (empty_node) {
        append_sentence(file, empty_node);
    }
    
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
            remove_all_checkpoints(filename);
            
            // Free all sentences in linked list
            free_all_sentences(file);
            
            pthread_rwlock_destroy(&file->file_lock);
            pthread_mutex_destroy(&file->structure_lock);
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
    
    FILE* fp = fopen(file->filepath, "r");
    if (!fp) {
        pthread_rwlock_unlock(&file->file_lock);
        return ERR_SYSTEM_ERROR;
    }

    size_t bytes_read = fread(content, 1, MAX_CONTENT_SIZE - 1, fp);
    content[bytes_read] = '\0';
    *size = bytes_read;
    
    fclose(fp);
    
    file->last_accessed = time(NULL);
    
    pthread_rwlock_unlock(&file->file_lock);
    
    return ERR_SUCCESS;
}

// Continued in next part...
