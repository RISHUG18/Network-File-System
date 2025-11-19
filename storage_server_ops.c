#include "storage_server.h"

// ==================== DRAFT MANAGEMENT HELPERS ====================

static DraftSentence* create_draft_sentence_from_words(char** words, int word_count, char delimiter) {
    DraftSentence* draft = (DraftSentence*)calloc(1, sizeof(DraftSentence));
    if (!draft) {
        return NULL;
    }

    draft->word_capacity = (word_count > 0) ? word_count : 4;
    if (draft->word_capacity > 0) {
        draft->words = (char**)calloc(draft->word_capacity, sizeof(char*));
        if (!draft->words) {
            free(draft);
            return NULL;
        }

        for (int i = 0; i < word_count; i++) {
            draft->words[i] = strdup(words[i]);
            if (!draft->words[i]) {
                for (int j = 0; j < i; j++) {
                    free(draft->words[j]);
                }
                free(draft->words);
                free(draft);
                return NULL;
            }
        }
    }

    draft->word_count = word_count;
    draft->delimiter = delimiter;
    draft->next = NULL;
    return draft;
}

static DraftSentence* clone_sentence_to_draft(SentenceNode* sentence) {
    if (!sentence) {
        return NULL;
    }
    return create_draft_sentence_from_words(sentence->words, sentence->word_count, sentence->delimiter);
}

static void append_draft_sentence(DraftSentence** head, DraftSentence** tail, DraftSentence* node) {
    if (!node) return;
    if (!*head) {
        *head = node;
        *tail = node;
    } else {
        (*tail)->next = node;
        *tail = node;
    }
}

static bool ensure_sentence_draft(SentenceNode* sentence) {
    if (!sentence->draft_head) {
        sentence->draft_head = clone_sentence_to_draft(sentence);
        sentence->draft_dirty = false;
    }
    return sentence->draft_head != NULL;
}

static bool draft_ensure_capacity(DraftSentence* draft, int needed) {
    if (needed <= draft->word_capacity) {
        return true;
    }

    int new_capacity = draft->word_capacity > 0 ? draft->word_capacity : 4;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    char** new_words = (char**)realloc(draft->words, new_capacity * sizeof(char*));
    if (!new_words) {
        return false;
    }
    draft->words = new_words;
    draft->word_capacity = new_capacity;
    return true;
}

static bool insert_word_into_draft(DraftSentence* draft, int index, const char* word) {
    if (!draft || index < 0 || index > draft->word_count) {
        return false;
    }

    if (!draft_ensure_capacity(draft, draft->word_count + 1)) {
        return false;
    }

    for (int i = draft->word_count; i > index; i--) {
        draft->words[i] = draft->words[i - 1];
    }

    draft->words[index] = strdup(word);
    if (!draft->words[index]) {
        for (int i = index; i < draft->word_count; i++) {
            draft->words[i] = draft->words[i + 1];
        }
        return false;
    }

    draft->word_count++;
    return true;
}

static int total_draft_words(SentenceNode* sentence) {
    int total = 0;
    DraftSentence* current = sentence->draft_head;
    while (current) {
        total += current->word_count;
        current = current->next;
    }
    return total;
}

static bool insert_word_into_drafts(SentenceNode* sentence, int absolute_index, const char* word) {
    if (!sentence->draft_head) {
        return false;
    }
    DraftSentence* current = sentence->draft_head;
    int idx = absolute_index;

    while (current) {
        if (idx < current->word_count) {
            return insert_word_into_draft(current, idx, word);
        }
        if (idx == current->word_count) {
            if (current->next) {
                current = current->next;
                idx = 0;
                continue;
            }
            return insert_word_into_draft(current, current->word_count, word);
        }
        idx -= current->word_count;
        current = current->next;
    }

    return false;
}

static bool contains_sentence_delimiter(const char* text) {
    for (size_t i = 0; text && text[i] != '\0'; i++) {
        if (is_sentence_delimiter(text[i])) {
            return true;
        }
    }
    return false;
}

static void build_draft_text(SentenceNode* sentence, char* buffer, size_t bufsize) {
    buffer[0] = '\0';
    size_t offset = 0;
    bool first_sentence = true;

    DraftSentence* current = sentence->draft_head;
    while (current) {
        if (!first_sentence && offset > 0 && buffer[offset - 1] != ' ' && offset < bufsize - 1) {
            buffer[offset++] = ' ';
        }
        first_sentence = false;

        for (int w = 0; w < current->word_count; w++) {
            if (offset > 0 && buffer[offset - 1] != ' ' && offset < bufsize - 1) {
                buffer[offset++] = ' ';
            }

            const char* word = current->words[w];
            size_t len = strlen(word);
            if (offset + len >= bufsize - 1) {
                len = bufsize - 1 - offset;
            }
            memcpy(buffer + offset, word, len);
            offset += len;
        }

        if (current->delimiter != '\0' && offset < bufsize - 1) {
            buffer[offset++] = current->delimiter;
        }

        current = current->next;
    }

    buffer[offset] = '\0';
}

static DraftSentence* parse_text_to_drafts(const char* text) {
    size_t len = strlen(text);
    size_t idx = 0;
    DraftSentence* head = NULL;
    DraftSentence* tail = NULL;

    while (idx < len) {
        while (idx < len && (text[idx] == ' ' || text[idx] == '\n' || text[idx] == '\t')) {
            idx++;
        }
        if (idx >= len) {
            break;
        }

        size_t start = idx;
        while (idx < len && !is_sentence_delimiter(text[idx])) {
            idx++;
        }

        char delimiter = '\0';
        if (idx < len && is_sentence_delimiter(text[idx])) {
            delimiter = text[idx];
            idx++;
        }

        size_t sentence_len = (delimiter != '\0') ? (idx - start - 1) : (idx - start);
        if ((int)sentence_len < 0) {
            sentence_len = 0;
        }

        char sentence_text[MAX_CONTENT_SIZE];
        if (sentence_len >= MAX_CONTENT_SIZE) {
            sentence_len = MAX_CONTENT_SIZE - 1;
        }
        memcpy(sentence_text, text + start, sentence_len);
        sentence_text[sentence_len] = '\0';

        char sentence_copy[MAX_CONTENT_SIZE];
        strncpy(sentence_copy, sentence_text, MAX_CONTENT_SIZE - 1);
        sentence_copy[MAX_CONTENT_SIZE - 1] = '\0';

        char* words[1000];
        int word_count = 0;
        char* token = strtok(sentence_copy, " \t\n");
        while (token && word_count < 1000) {
            words[word_count++] = token;
            token = strtok(NULL, " \t\n");
        }

        DraftSentence* draft = create_draft_sentence_from_words(words, word_count, delimiter);
        if (!draft) {
            free_draft_sentences(head);
            return NULL;
        }

        append_draft_sentence(&head, &tail, draft);
    }

    if (!head) {
        // Ensure at least one empty draft node exists
        DraftSentence* empty = (DraftSentence*)calloc(1, sizeof(DraftSentence));
        if (!empty) {
            return NULL;
        }
        empty->word_capacity = 4;
        empty->words = (char**)calloc(empty->word_capacity, sizeof(char*));
        head = empty;
    } else if (tail && tail->delimiter != '\0') {
        DraftSentence* empty = (DraftSentence*)calloc(1, sizeof(DraftSentence));
        if (!empty) {
            free_draft_sentences(head);
            return NULL;
        }
        empty->word_capacity = 4;
        empty->words = (char**)calloc(empty->word_capacity, sizeof(char*));
        tail->next = empty;
    }

    return head;
}

static bool rebuild_draft_structure(SentenceNode* sentence) {
    char buffer[MAX_CONTENT_SIZE];
    build_draft_text(sentence, buffer, sizeof(buffer));

    DraftSentence* new_head = parse_text_to_drafts(buffer);
    if (!new_head) {
        return false;
    }

    free_draft_sentences(sentence->draft_head);
    sentence->draft_head = new_head;
    return true;
}

static bool overwrite_sentence_with_draft(SentenceNode* sentence, DraftSentence* draft) {
    int required_capacity = draft->word_count > 0 ? draft->word_count : 4;
    char** new_words = (char**)calloc(required_capacity, sizeof(char*));
    if (!new_words) {
        return false;
    }

    for (int i = 0; i < draft->word_count; i++) {
        new_words[i] = strdup(draft->words[i]);
        if (!new_words[i]) {
            for (int j = 0; j < i; j++) {
                free(new_words[j]);
            }
            free(new_words);
            return false;
        }
    }

    if (sentence->words) {
        for (int i = 0; i < sentence->word_count; i++) {
            free(sentence->words[i]);
        }
        free(sentence->words);
    }

    sentence->words = new_words;
    sentence->word_capacity = required_capacity;
    sentence->word_count = draft->word_count;
    sentence->delimiter = draft->delimiter;
    return true;
}

static bool apply_drafts_to_file(FileEntry* file, SentenceNode* sentence) {
    DraftSentence* draft = sentence->draft_head;
    if (!draft) {
        return true;
    }

    DraftSentence* cursor = draft->next;
    SentenceNode* new_head = NULL;
    SentenceNode* new_tail = NULL;

    while (cursor) {
        SentenceNode* new_node = create_sentence_node((const char**)cursor->words, cursor->word_count, cursor->delimiter);
        if (!new_node) {
            // Cleanup any nodes we already allocated
            SentenceNode* tmp = new_head;
            while (tmp) {
                SentenceNode* next = tmp->next;
                free_sentence_node(tmp);
                tmp = next;
            }
            return false;
        }

        if (!new_head) {
            new_head = new_tail = new_node;
        } else {
            new_tail->next = new_node;
            new_node->prev = new_tail;
            new_tail = new_node;
        }

        cursor = cursor->next;
    }

    if (!overwrite_sentence_with_draft(sentence, draft)) {
        SentenceNode* tmp = new_head;
        while (tmp) {
            SentenceNode* next = tmp->next;
            free_sentence_node(tmp);
            tmp = next;
        }
        return false;
    }

    // Insert new sentences directly (caller must hold structure_lock)
    SentenceNode* insertion_point = sentence;
    SentenceNode* iterator = new_head;
    while (iterator) {
        SentenceNode* next = iterator->next;
        iterator->next = NULL;
        iterator->prev = NULL;
        
        // Inline insertion without taking structure_lock (caller holds it)
        iterator->next = insertion_point->next;
        iterator->prev = insertion_point;
        
        if (insertion_point->next) {
            insertion_point->next->prev = iterator;
        } else {
            file->tail = iterator;
        }
        
        insertion_point->next = iterator;
        file->sentence_count++;
        
        insertion_point = iterator;
        iterator = next;
    }

    free_draft_sentences(sentence->draft_head);
    sentence->draft_head = NULL;
    sentence->draft_dirty = false;
    return true;
}

// ==================== SENTENCE LOCKING AND WRITE OPERATIONS ====================

ErrorCode lock_sentence(StorageServer* ss, const char* filename, int sentence_num, int client_id) {
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }

    // Validate sentence number - must exist in file
    if (sentence_num < 0 || sentence_num >= file->sentence_count) {
        return ERR_INVALID_SENTENCE;
    }

    char snapshot[MAX_CONTENT_SIZE];
    bool snapshot_ready = false;

    // Take snapshot before any modifications
    pthread_rwlock_rdlock(&file->file_lock);
    rebuild_file_content(file, snapshot);
    snapshot_ready = true;
    pthread_rwlock_unlock(&file->file_lock);

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
        if (sent->draft_head) {
            free_draft_sentences(sent->draft_head);
            sent->draft_head = NULL;
            sent->draft_dirty = false;
        }
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
    
    // Acquire Read lock on file to prevent concurrent commits
    pthread_rwlock_rdlock(&file->file_lock);
    
    if (sentence_num < 0 || sentence_num >= file->sentence_count) {
        pthread_rwlock_unlock(&file->file_lock);
        return ERR_INVALID_SENTENCE;
    }
    
    SentenceNode* sent = get_sentence_by_index(file, sentence_num);
    if (!sent) {
        pthread_rwlock_unlock(&file->file_lock);
        return ERR_INVALID_SENTENCE;
    }
    
    pthread_mutex_lock(&sent->lock);
    if (sent->is_locked && sent->lock_holder_id != client_id) {
        pthread_mutex_unlock(&sent->lock);
        pthread_rwlock_unlock(&file->file_lock);
        return ERR_FILE_LOCKED;
    }
    
    if (!ensure_sentence_draft(sent)) {
        pthread_mutex_unlock(&sent->lock);
        pthread_rwlock_unlock(&file->file_lock);
        return ERR_SYSTEM_ERROR;
    }
    
    int total_words = total_draft_words(sent);
    if (word_index < 0 || word_index > total_words) {
        pthread_mutex_unlock(&sent->lock);
        pthread_rwlock_unlock(&file->file_lock);
        return ERR_INVALID_OPERATION;
    }
    
    char content_copy[BUFFER_SIZE];
    strncpy(content_copy, new_content, BUFFER_SIZE - 1);
    content_copy[BUFFER_SIZE - 1] = '\0';
    
    char* token;
    char* saveptr;
    int current_index = word_index;
    bool success = true;
    
    token = strtok_r(content_copy, " ", &saveptr);
    while (token && success) {
        success = insert_word_into_drafts(sent, current_index, token);
        if (success) {
            current_index++;
        }
        token = strtok_r(NULL, " ", &saveptr);
    }
    
    if (!success) {
        pthread_mutex_unlock(&sent->lock);
        pthread_rwlock_unlock(&file->file_lock);
        return ERR_INVALID_OPERATION;
    }
    
    sent->draft_dirty = true;
    
    if (contains_sentence_delimiter(new_content)) {
        if (!rebuild_draft_structure(sent)) {
            pthread_mutex_unlock(&sent->lock);
            pthread_rwlock_unlock(&file->file_lock);
            return ERR_SYSTEM_ERROR;
        }
    }
    
    pthread_mutex_unlock(&sent->lock);
    pthread_rwlock_unlock(&file->file_lock);
    
    char details[256];
    snprintf(details, sizeof(details), "File=%s Sentence=%d Word=%d", 
            filename, sentence_num, word_index);
    log_message(ss, "INFO", "WRITE", details);
    
    return ERR_SUCCESS;
}

ErrorCode commit_sentence_drafts(StorageServer* ss, const char* filename, int sentence_num) {
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }

    pthread_rwlock_wrlock(&file->file_lock);

    if (sentence_num < 0 || sentence_num >= file->sentence_count) {
        pthread_rwlock_unlock(&file->file_lock);
        return ERR_INVALID_SENTENCE;
    }

    SentenceNode* sentence = get_sentence_by_index(file, sentence_num);
    if (!sentence) {
        pthread_rwlock_unlock(&file->file_lock);
        return ERR_INVALID_SENTENCE;
    }

    pthread_mutex_lock(&sentence->lock);

    if (!sentence->draft_dirty || !sentence->draft_head) {
        pthread_mutex_unlock(&sentence->lock);
        pthread_rwlock_unlock(&file->file_lock);
        return ERR_SUCCESS;
    }

    // Lock structure before applying drafts (which may insert new sentences)
    pthread_mutex_lock(&file->structure_lock);
    
    bool applied = apply_drafts_to_file(file, sentence);
    
    pthread_mutex_unlock(&file->structure_lock);
    
    if (!applied) {
        pthread_mutex_unlock(&sentence->lock);
        pthread_rwlock_unlock(&file->file_lock);
        return ERR_SYSTEM_ERROR;
    }

    // Unlock sentence before calling functions that acquire structure_lock
    // to avoid lock ordering violations
    pthread_mutex_unlock(&sentence->lock);

    refresh_file_stats(file);
    file->last_modified = time(NULL);
    file->last_accessed = file->last_modified;
    save_file_to_disk(file);

    char details[256];
    snprintf(details, sizeof(details), "File=%s Sentence=%d", filename, sentence_num);
    log_message(ss, "INFO", "COMMIT", details);

    pthread_rwlock_unlock(&file->file_lock);

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
            const char* word = current->words[i];
            size_t word_len = strlen(word);
            bool append_delim = (i == current->word_count - 1) && (current->delimiter != '\0');

            char word_msg[BUFFER_SIZE];
            size_t msg_len = 0;

            if (!append_delim && word_len < sizeof(word_msg) - 1) {
                memcpy(word_msg, word, word_len);
                word_msg[word_len] = '\n';
                msg_len = word_len + 1;
            } else if (append_delim && word_len < sizeof(word_msg) - 2) {
                memcpy(word_msg, word, word_len);
                word_msg[word_len] = current->delimiter;
                word_msg[word_len + 1] = '\n';
                msg_len = word_len + 2;
            } else {
                // Fallback for very long words: stream in pieces while preserving delimiter
                ssize_t sent_bytes = send(client_fd, word, word_len, MSG_NOSIGNAL);
                if (sent_bytes <= 0) {
                    pthread_mutex_unlock(&file->structure_lock);
                    pthread_rwlock_unlock(&file->file_lock);
                    return ERR_SYSTEM_ERROR;
                }

                if (append_delim) {
                    char tail[2] = { current->delimiter, '\n' };
                    sent_bytes = send(client_fd, tail, sizeof(tail), MSG_NOSIGNAL);
                } else {
                    char newline = '\n';
                    sent_bytes = send(client_fd, &newline, 1, MSG_NOSIGNAL);
                }

                if (sent_bytes <= 0) {
                    pthread_mutex_unlock(&file->structure_lock);
                    pthread_rwlock_unlock(&file->file_lock);
                    return ERR_SYSTEM_ERROR;
                }

                continue;
            }

            ssize_t sent_bytes = send(client_fd, word_msg, msg_len, MSG_NOSIGNAL);
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
                       size_t* size, int* words, int* chars, time_t* last_accessed) {
    FileEntry* file = find_file(ss, filename);
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }
    
    pthread_rwlock_rdlock(&file->file_lock);
    
    *size = file->total_size;
    *words = file->total_words;
    *chars = file->total_chars;
    if (last_accessed) {
        *last_accessed = file->last_accessed;
    }
    
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
