#include "name_server.h"

// ==================== FILE OPERATION HANDLERS ====================

ErrorCode handle_view_files(NameServer* nm, Client* client, const char* flags, char* response) {
    bool show_all = (flags && strstr(flags, "a") != NULL);
    bool detailed = (flags && strstr(flags, "l") != NULL);
    
    char buffer[BUFFER_SIZE * 4] = {0};
    int offset = 0;
    
    pthread_mutex_lock(&nm->trie_lock);
    
    // Helper function to traverse trie and collect files
    void collect_files(TrieNode* node, char* prefix, int depth) {
        if (!node) return;
        
        if (node->is_end_of_word && node->file_metadata) {
            FileMetadata* metadata = (FileMetadata*)node->file_metadata;
            AccessRight access = check_access(metadata, client->username);
            
            if (show_all || access != ACCESS_NONE) {
                if (detailed) {
                    char time_buf[64];
                    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", 
                            localtime(&metadata->last_accessed));
                    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                                     "%s %10zu %5d %5d %s %s\n",
                                     metadata->owner[0] ? metadata->owner : "none",
                                     metadata->file_size,
                                     metadata->word_count,
                                     metadata->char_count,
                                     time_buf,
                                     metadata->filename);
                } else {
                    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                                     "%s\n", metadata->filename);
                }
            }
        }
        
        for (int i = 0; i < 256; i++) {
            if (node->children[i]) {
                prefix[depth] = (char)i;
                prefix[depth + 1] = '\0';
                collect_files(node->children[i], prefix, depth + 1);
            }
        }
    }
    
    char prefix[MAX_FILENAME] = {0};
    if (detailed) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                         "%-10s %10s %5s %5s %19s %s\n",
                         "OWNER", "SIZE", "WORDS", "CHARS", "LAST_ACCESS", "FILENAME");
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                         "------------------------------------------------------------\n");
    }
    
    collect_files(nm->file_trie, prefix, 0);
    
    pthread_mutex_unlock(&nm->trie_lock);
    
    if (offset == 0 || (detailed && offset < 100)) {
        strcpy(response, "No files found\n");
    } else {
        strncpy(response, buffer, BUFFER_SIZE * 4 - 1);
    }
    
    log_message(nm, "INFO", client->ip, client->nm_port, client->username,
               "VIEW", flags ? flags : "default");
    
    return ERR_SUCCESS;
}

ErrorCode handle_create_file(NameServer* nm, Client* client, const char* filename) {
    // Check if file already exists
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* existing = search_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);
    
    if (existing) {
        return ERR_FILE_EXISTS;
    }
    
    // Find a storage server to create the file (use round-robin or random)
    pthread_mutex_lock(&nm->ss_lock);
    StorageServer* ss = NULL;
    for (int i = 0; i < nm->ss_count; i++) {
        if (nm->storage_servers[i] && nm->storage_servers[i]->is_active) {
            ss = nm->storage_servers[i];
            break;
        }
    }
    pthread_mutex_unlock(&nm->ss_lock);
    
    if (!ss) {
        return ERR_SS_NOT_FOUND;
    }
    
    // Send create command to storage server
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "CREATE %s", filename);
    
    char response[BUFFER_SIZE];
    if (forward_to_ss(nm, ss->id, command, response) < 0) {
        return ERR_SS_DISCONNECTED;
    }
    
    // Parse response
    if (strncmp(response, "SUCCESS", 7) != 0) {
        return ERR_SYSTEM_ERROR;
    }
    
    // Add to trie
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* metadata = (FileMetadata*)malloc(sizeof(FileMetadata));
    strncpy(metadata->filename, filename, MAX_FILENAME);
    strncpy(metadata->owner, client->username, MAX_USERNAME);
    metadata->ss_id = ss->id;
    metadata->created_time = time(NULL);
    metadata->last_modified = time(NULL);
    metadata->last_accessed = time(NULL);
    metadata->file_size = 0;
    metadata->word_count = 0;
    metadata->char_count = 0;
    metadata->acl = NULL;
    
    insert_file_trie(nm->file_trie, filename, metadata);
    put_in_cache(nm->cache, filename, metadata);
    pthread_mutex_unlock(&nm->trie_lock);
    
    char details[256];
    snprintf(details, sizeof(details), "File=%s SS_ID=%d", filename, ss->id);
    log_message(nm, "INFO", client->ip, client->nm_port, client->username,
               "CREATE", details);
    
    return ERR_SUCCESS;
}

ErrorCode handle_delete_file(NameServer* nm, Client* client, const char* filename) {
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* metadata = search_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);
    
    if (!metadata) {
        return ERR_FILE_NOT_FOUND;
    }
    
    if (!is_owner(metadata, client->username)) {
        return ERR_PERMISSION_DENIED;
    }
    
    StorageServer* ss = get_storage_server(nm, metadata->ss_id);
    if (!ss || !ss->is_active) {
        return ERR_SS_NOT_FOUND;
    }
    
    // Send delete command to storage server
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "DELETE %s", filename);
    
    char response[BUFFER_SIZE];
    if (forward_to_ss(nm, ss->id, command, response) < 0) {
        return ERR_SS_DISCONNECTED;
    }
    
    // Remove from trie
    pthread_mutex_lock(&nm->trie_lock);
    delete_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);
    
    char details[256];
    snprintf(details, sizeof(details), "File=%s", filename);
    log_message(nm, "INFO", client->ip, client->nm_port, client->username,
               "DELETE", details);
    
    return ERR_SUCCESS;
}

ErrorCode handle_read_file(NameServer* nm, Client* client, const char* filename, char* response) {
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* metadata = search_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);
    
    if (!metadata) {
        return ERR_FILE_NOT_FOUND;
    }
    
    AccessRight access = check_access(metadata, client->username);
    if (access == ACCESS_NONE) {
        return ERR_UNAUTHORIZED;
    }
    
    StorageServer* ss = get_storage_server(nm, metadata->ss_id);
    if (!ss || !ss->is_active) {
        return ERR_SS_NOT_FOUND;
    }
    
    // Return SS information to client for direct connection
    snprintf(response, BUFFER_SIZE, "SS_INFO %s %d", ss->ip, ss->client_port);
    
    metadata->last_accessed = time(NULL);
    
    char details[256];
    snprintf(details, sizeof(details), "File=%s SS_ID=%d", filename, ss->id);
    log_message(nm, "INFO", client->ip, client->nm_port, client->username,
               "READ", details);
    
    return ERR_SUCCESS;
}

ErrorCode handle_write_file(NameServer* nm, Client* client, const char* filename, int sentence_num) {
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* metadata = search_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);
    
    if (!metadata) {
        return ERR_FILE_NOT_FOUND;
    }
    
    AccessRight access = check_access(metadata, client->username);
    if (access != ACCESS_WRITE) {
        return ERR_PERMISSION_DENIED;
    }
    
    StorageServer* ss = get_storage_server(nm, metadata->ss_id);
    if (!ss || !ss->is_active) {
        return ERR_SS_NOT_FOUND;
    }
    
    metadata->last_modified = time(NULL);
    metadata->last_accessed = time(NULL);
    
    char details[256];
    snprintf(details, sizeof(details), "File=%s Sentence=%d SS_ID=%d", 
            filename, sentence_num, ss->id);
    log_message(nm, "INFO", client->ip, client->nm_port, client->username,
               "WRITE", details);
    
    return ERR_SUCCESS;
}

ErrorCode handle_info_file(NameServer* nm, Client* client, const char* filename, char* response) {
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* metadata = search_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);
    
    if (!metadata) {
        return ERR_FILE_NOT_FOUND;
    }
    
    AccessRight access = check_access(metadata, client->username);
    if (access == ACCESS_NONE) {
        return ERR_UNAUTHORIZED;
    }
    
    // Request updated info from storage server
    StorageServer* ss = get_storage_server(nm, metadata->ss_id);
    if (ss && ss->is_active) {
        char command[BUFFER_SIZE];
        snprintf(command, sizeof(command), "INFO %s", filename);
        
        char ss_response[BUFFER_SIZE];
        if (forward_to_ss(nm, ss->id, command, ss_response) >= 0) {
            // Parse response and update metadata
            sscanf(ss_response, "SIZE:%zu WORDS:%d CHARS:%d",
                   &metadata->file_size, &metadata->word_count, &metadata->char_count);
        }
    }
    
    char time_created[64], time_modified[64], time_accessed[64];
    strftime(time_created, sizeof(time_created), "%Y-%m-%d %H:%M:%S", 
            localtime(&metadata->created_time));
    strftime(time_modified, sizeof(time_modified), "%Y-%m-%d %H:%M:%S", 
            localtime(&metadata->last_modified));
    strftime(time_accessed, sizeof(time_accessed), "%Y-%m-%d %H:%M:%S", 
            localtime(&metadata->last_accessed));
    
    snprintf(response, BUFFER_SIZE,
             "File: %s\n"
             "Owner: %s\n"
             "Size: %zu bytes\n"
             "Word Count: %d\n"
             "Character Count: %d\n"
             "Created: %s\n"
             "Last Modified: %s\n"
             "Last Accessed: %s\n"
             "Storage Server: %d\n"
             "Your Access: %s\n",
             metadata->filename,
             metadata->owner[0] ? metadata->owner : "none",
             metadata->file_size,
             metadata->word_count,
             metadata->char_count,
             time_created,
             time_modified,
             time_accessed,
             metadata->ss_id,
             access == ACCESS_WRITE ? "READ/WRITE" : "READ");
    
    // Add ACL info
    if (is_owner(metadata, client->username)) {
        strcat(response, "\nAccess Control List:\n");
        struct AccessEntry* entry = metadata->acl;
        while (entry) {
            char acl_line[128];
            snprintf(acl_line, sizeof(acl_line), "  %s: %s\n",
                    entry->username,
                    entry->access == ACCESS_WRITE ? "READ/WRITE" : "READ");
            strcat(response, acl_line);
            entry = entry->next;
        }
    }
    
    log_message(nm, "INFO", client->ip, client->nm_port, client->username,
               "INFO", filename);
    
    return ERR_SUCCESS;
}

ErrorCode handle_stream_file(NameServer* nm, Client* client, const char* filename, char* response) {
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* metadata = search_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);
    
    if (!metadata) {
        return ERR_FILE_NOT_FOUND;
    }
    
    AccessRight access = check_access(metadata, client->username);
    if (access == ACCESS_NONE) {
        return ERR_UNAUTHORIZED;
    }
    
    StorageServer* ss = get_storage_server(nm, metadata->ss_id);
    if (!ss || !ss->is_active) {
        return ERR_SS_NOT_FOUND;
    }
    
    // Return SS information to client for direct connection
    snprintf(response, BUFFER_SIZE, "SS_INFO %s %d", ss->ip, ss->client_port);
    
    metadata->last_accessed = time(NULL);
    
    char details[256];
    snprintf(details, sizeof(details), "File=%s SS_ID=%d", filename, ss->id);
    log_message(nm, "INFO", client->ip, client->nm_port, client->username,
               "STREAM", details);
    
    return ERR_SUCCESS;
}

ErrorCode handle_exec_file(NameServer* nm, Client* client, const char* filename, char* response) {
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* metadata = search_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);
    
    if (!metadata) {
        return ERR_FILE_NOT_FOUND;
    }
    
    AccessRight access = check_access(metadata, client->username);
    if (access == ACCESS_NONE) {
        return ERR_UNAUTHORIZED;
    }
    
    StorageServer* ss = get_storage_server(nm, metadata->ss_id);
    if (!ss || !ss->is_active) {
        return ERR_SS_NOT_FOUND;
    }
    
    // Get file content from storage server
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "READ %s", filename);
    
    char file_content[BUFFER_SIZE * 4];
    if (forward_to_ss(nm, ss->id, command, file_content) < 0) {
        return ERR_SS_DISCONNECTED;
    }
    
    // Execute commands on name server
    FILE* fp = popen(file_content, "r");
    if (!fp) {
        strcpy(response, "Error: Failed to execute commands");
        return ERR_SYSTEM_ERROR;
    }
    
    char output[BUFFER_SIZE * 4] = {0};
    int offset = 0;
    char line[1024];
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        int len = strlen(line);
        if (offset + len < sizeof(output) - 1) {
            strcpy(output + offset, line);
            offset += len;
        }
    }
    
    int status = pclose(fp);
    
    if (WIFEXITED(status)) {
        snprintf(response, BUFFER_SIZE * 4, "Exit code: %d\nOutput:\n%s", 
                WEXITSTATUS(status), output);
    } else {
        snprintf(response, BUFFER_SIZE * 4, "Command terminated abnormally\nOutput:\n%s", 
                output);
    }
    
    metadata->last_accessed = time(NULL);
    
    char details[256];
    snprintf(details, sizeof(details), "File=%s ExitCode=%d", 
            filename, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    log_message(nm, "INFO", client->ip, client->nm_port, client->username,
               "EXEC", details);
    
    return ERR_SUCCESS;
}

ErrorCode handle_undo_file(NameServer* nm, Client* client, const char* filename) {
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* metadata = search_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);
    
    if (!metadata) {
        return ERR_FILE_NOT_FOUND;
    }
    
    AccessRight access = check_access(metadata, client->username);
    if (access != ACCESS_WRITE) {
        return ERR_PERMISSION_DENIED;
    }
    
    StorageServer* ss = get_storage_server(nm, metadata->ss_id);
    if (!ss || !ss->is_active) {
        return ERR_SS_NOT_FOUND;
    }
    
    // Send undo command to storage server
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "UNDO %s", filename);
    
    char ss_response[BUFFER_SIZE];
    if (forward_to_ss(nm, ss->id, command, ss_response) < 0) {
        return ERR_SS_DISCONNECTED;
    }
    
    if (strncmp(ss_response, "SUCCESS", 7) != 0) {
        return ERR_SYSTEM_ERROR;
    }
    
    metadata->last_modified = time(NULL);
    
    char details[256];
    snprintf(details, sizeof(details), "File=%s", filename);
    log_message(nm, "INFO", client->ip, client->nm_port, client->username,
               "UNDO", details);
    
    return ERR_SUCCESS;
}

// ==================== USER MANAGEMENT ====================

ErrorCode handle_list_users(NameServer* nm, char* response) {
    pthread_mutex_lock(&nm->client_lock);
    
    char buffer[BUFFER_SIZE] = "Registered Users:\n";
    int offset = strlen(buffer);
    
    for (int i = 0; i < nm->client_count; i++) {
        if (nm->clients[i] && nm->clients[i]->is_active) {
            offset += snprintf(buffer + offset, BUFFER_SIZE - offset,
                             "  %s (@%s)\n", 
                             nm->clients[i]->username,
                             nm->clients[i]->ip);
        }
    }
    
    pthread_mutex_unlock(&nm->client_lock);
    
    if (offset == strlen("Registered Users:\n")) {
        strcpy(response, "No users currently connected\n");
    } else {
        strncpy(response, buffer, BUFFER_SIZE - 1);
    }
    
    return ERR_SUCCESS;
}

// ==================== NETWORKING ====================

void send_response(int socket_fd, ErrorCode error, const char* message) {
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "%d:%s", error, message ? message : "");
    send(socket_fd, response, strlen(response), 0);
}

int forward_to_ss(NameServer* nm, int ss_id, const char* command, char* response) {
    StorageServer* ss = get_storage_server(nm, ss_id);
    if (!ss || !ss->is_active) {
        return -1;
    }
    
    pthread_mutex_lock(&ss->lock);
    
    // Send command
    if (send(ss->socket_fd, command, strlen(command), 0) < 0) {
        pthread_mutex_unlock(&ss->lock);
        deregister_storage_server(nm, ss_id);
        return -1;
    }
    
    // Receive response
    int bytes = recv(ss->socket_fd, response, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) {
        pthread_mutex_unlock(&ss->lock);
        deregister_storage_server(nm, ss_id);
        return -1;
    }
    
    response[bytes] = '\0';
    pthread_mutex_unlock(&ss->lock);
    
    return bytes;
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

// To be continued with connection handler...
