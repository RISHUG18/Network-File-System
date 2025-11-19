#include "name_server.h"

static void record_last_access(FileMetadata* metadata, const char* username) {
    if (!metadata) return;
    if (username && *username) {
        strncpy(metadata->last_accessed_by, username, MAX_USERNAME - 1);
        metadata->last_accessed_by[MAX_USERNAME - 1] = '\0';
    }
}

static const char* access_to_string(AccessRight access) {
    return (access == ACCESS_WRITE) ? "WRITE" : "READ";
}

static AccessRequest* find_access_request(FileMetadata* metadata, const char* username, AccessRequest** prev_out) {
    if (prev_out) {
        *prev_out = NULL;
    }
    AccessRequest* current = metadata ? metadata->pending_requests : NULL;
    AccessRequest* prev = NULL;
    while (current) {
        if (strcmp(current->username, username) == 0) {
            if (prev_out) {
                *prev_out = prev;
            }
            return current;
        }
        prev = current;
        current = current->next;
    }
    return NULL;
}

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
        size_t response_cap = BUFFER_SIZE * 4;
        size_t copy_len = strlen(buffer);
        if (copy_len >= response_cap) {
            copy_len = response_cap - 1;
        }
        memcpy(response, buffer, copy_len);
        response[copy_len] = '\0';
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
    
    // Find a storage server to create the file (try all active servers)
    pthread_mutex_lock(&nm->ss_lock);
    StorageServer* ss = NULL;
    int start_index = 0; // Could be random or round-robin state
    
    // Try to find a working server
    for (int i = 0; i < nm->ss_count; i++) {
        int idx = (start_index + i) % nm->ss_count;
        if (nm->storage_servers[idx] && nm->storage_servers[idx]->is_active) {
            ss = nm->storage_servers[idx];
            
            // Try to create file on this server
            char command[BUFFER_SIZE];
            snprintf(command, sizeof(command), "CREATE %s", filename);
            
            char response[BUFFER_SIZE];
            
            // Release lock temporarily while communicating
            pthread_mutex_unlock(&nm->ss_lock);
            
            if (forward_to_ss(nm, ss->id, command, response) >= 0) {
                // Communication successful
                if (strncmp(response, "SUCCESS", 7) == 0) {
                    // File created successfully!
                    
                    // Add to trie
                    pthread_mutex_lock(&nm->trie_lock);
                    FileMetadata* metadata = (FileMetadata*)malloc(sizeof(FileMetadata));
                    strncpy(metadata->filename, filename, MAX_FILENAME - 1);
                    metadata->filename[MAX_FILENAME - 1] = '\0';
                    strncpy(metadata->owner, client->username, MAX_USERNAME - 1);
                    metadata->owner[MAX_USERNAME - 1] = '\0';
                    metadata->ss_id = ss->id;
                    metadata->created_time = time(NULL);
                    metadata->last_modified = time(NULL);
                    metadata->last_accessed = time(NULL);
                    record_last_access(metadata, client->username);
                    strncpy(metadata->last_accessed_by, client->username, MAX_USERNAME - 1);
                    metadata->last_accessed_by[MAX_USERNAME - 1] = '\0';
                    metadata->file_size = 0;
                    metadata->word_count = 0;
                    metadata->char_count = 0;
                    metadata->acl = NULL;
                    metadata->pending_requests = NULL;
                    
                    insert_file_trie(nm->file_trie, filename, metadata);
                    put_in_cache(nm->cache, filename, metadata);
                    pthread_mutex_unlock(&nm->trie_lock);
                    
                    char details[256];
                    snprintf(details, sizeof(details), "File=%s SS_ID=%d", filename, ss->id);
                    log_message(nm, "INFO", client->ip, client->nm_port, client->username,
                               "CREATE", details);
                    
                    return ERR_SUCCESS;
                } else {
                    // Server responded but failed (e.g. file exists?)
                    // If file exists on SS but not in NM, that's an inconsistency.
                    // But we should probably stop trying if SS says "ERROR:File already exists"
                    if (strstr(response, "exists")) {
                        return ERR_FILE_EXISTS;
                    }
                    // Other error? Try next server?
                }
            }
            
            // If we are here, either forward_to_ss failed (disconnected) or creation failed
            // Re-acquire lock to continue loop
            pthread_mutex_lock(&nm->ss_lock);
        }
    }
    pthread_mutex_unlock(&nm->ss_lock);
    
    return ERR_SS_NOT_FOUND;
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
    record_last_access(metadata, client->username);
    
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
    record_last_access(metadata, client->username);
    
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
            long last_access_ts = 0;
            sscanf(ss_response, "SIZE:%zu WORDS:%d CHARS:%d LAST_ACCESS:%ld",
                   &metadata->file_size, &metadata->word_count, &metadata->char_count, &last_access_ts);
            if (last_access_ts > 0) {
                metadata->last_accessed = (time_t)last_access_ts;
            }
        }
    }
    
    char time_created[64], time_modified[64], time_accessed[64];
    strftime(time_created, sizeof(time_created), "%Y-%m-%d %H:%M:%S", 
        localtime(&metadata->created_time));
    strftime(time_modified, sizeof(time_modified), "%Y-%m-%d %H:%M:%S", 
        localtime(&metadata->last_modified));
    strftime(time_accessed, sizeof(time_accessed), "%Y-%m-%d %H:%M:%S", 
        localtime(&metadata->last_accessed));

    const char* owner_display = metadata->owner[0] ? metadata->owner : "none";
    const char* last_user = metadata->last_accessed_by[0] ? metadata->last_accessed_by : "unknown";

    char access_summary[BUFFER_SIZE / 2];
    size_t access_len = 0;
    access_len += snprintf(access_summary + access_len, sizeof(access_summary) - access_len,
               "%s (RW)", owner_display);
    AccessEntry* acl_entry = metadata->acl;
    while (acl_entry && access_len < sizeof(access_summary) - 1) {
    const char* perm = (acl_entry->access == ACCESS_WRITE) ? "RW" : "R";
    access_len += snprintf(access_summary + access_len,
                   sizeof(access_summary) - access_len,
                   ", %s (%s)", acl_entry->username, perm);
    acl_entry = acl_entry->next;
    }
    if (access_len == 0) {
    strncpy(access_summary, "None", sizeof(access_summary) - 1);
    access_summary[sizeof(access_summary) - 1] = '\0';
    }

    size_t offset = 0;
    offset += snprintf(response + offset, BUFFER_SIZE - offset, "--> File: %s\n", metadata->filename);
    offset += snprintf(response + offset, BUFFER_SIZE - offset, "--> Owner: %s\n", owner_display);
    offset += snprintf(response + offset, BUFFER_SIZE - offset, "--> Created: %s\n", time_created);
    offset += snprintf(response + offset, BUFFER_SIZE - offset, "--> Last Modified: %s\n", time_modified);
    offset += snprintf(response + offset, BUFFER_SIZE - offset, "--> Size: %zu bytes\n", metadata->file_size);
    offset += snprintf(response + offset, BUFFER_SIZE - offset, "--> Words: %d\n", metadata->word_count);
    offset += snprintf(response + offset, BUFFER_SIZE - offset, "--> Characters: %d\n", metadata->char_count);
    offset += snprintf(response + offset, BUFFER_SIZE - offset, "--> Access: %s\n", access_summary);
    offset += snprintf(response + offset, BUFFER_SIZE - offset,
               "--> Last Accessed: %s by %s\n",
               time_accessed,
               last_user);
    offset += snprintf(response + offset, BUFFER_SIZE - offset, "--> Storage Server: %d\n", metadata->ss_id);
    offset += snprintf(response + offset, BUFFER_SIZE - offset, "--> Your Access: %s\n",
               access == ACCESS_WRITE ? "READ/WRITE" : "READ");

    // Add ACL info for owner (ensure owner appears in list)
    if (is_owner(metadata, client->username)) {
    offset += snprintf(response + offset, BUFFER_SIZE - offset, "--> Access List:\n");
    offset += snprintf(response + offset, BUFFER_SIZE - offset, "    - %s (RW)\n", owner_display);
    AccessEntry* entry = metadata->acl;
    while (entry && offset < BUFFER_SIZE - 1) {
        const char* perm = (entry->access == ACCESS_WRITE) ? "RW" : "R";
        offset += snprintf(response + offset, BUFFER_SIZE - offset,
                   "    - %s (%s)\n",
                   entry->username,
                   perm);
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
    size_t offset = 0;
    char line[1024];
    
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (offset >= sizeof(output) - 1) {
            break;
        }

        size_t len = strlen(line);
        size_t space = sizeof(output) - offset - 1;

        if (len > space) {
            len = space;
        }

        if (len == 0) {
            continue;
        }

        memcpy(output + offset, line, len);
        offset += len;
        output[offset] = '\0';
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

// ==================== ACCESS REQUESTS ====================

static bool has_sufficient_access(AccessRight current, AccessRight requested) {
    if (current == ACCESS_WRITE) {
        return true;
    }
    return current == requested;
}

ErrorCode handle_request_access(NameServer* nm, Client* client, const char* filename,
                               AccessRight requested_access, char* response) {
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* metadata = search_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);

    if (!metadata) {
        return ERR_FILE_NOT_FOUND;
    }

    if (is_owner(metadata, client->username)) {
        strcpy(response, "You already own this file");
        return ERR_INVALID_OPERATION;
    }

    AccessRight current_access = check_access(metadata, client->username);
    if (has_sufficient_access(current_access, requested_access)) {
        strcpy(response, "You already have the requested access");
        return ERR_INVALID_OPERATION;
    }

    AccessRequest* existing = find_access_request(metadata, client->username, NULL);
    if (existing) {
        existing->requested_access = requested_access;
        existing->requested_time = time(NULL);
        snprintf(response, BUFFER_SIZE, "Updated existing request for %s access",
                 access_to_string(requested_access));
    } else {
    AccessRequest* new_request = (AccessRequest*)calloc(1, sizeof(AccessRequest));
    snprintf(new_request->username, MAX_USERNAME, "%s", client->username);
        new_request->requested_access = requested_access;
        new_request->requested_time = time(NULL);
        new_request->next = metadata->pending_requests;
        metadata->pending_requests = new_request;
        snprintf(response, BUFFER_SIZE, "Requested %s access", access_to_string(requested_access));
    }

    char details[256];
    snprintf(details, sizeof(details), "File=%s Requested=%s", filename,
             access_to_string(requested_access));
    log_message(nm, "INFO", client->ip, client->nm_port, client->username,
                "REQUEST_ACCESS", details);

    return ERR_SUCCESS;
}

ErrorCode handle_list_requests(NameServer* nm, Client* client, const char* filename, char* response) {
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* metadata = search_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);

    if (!metadata) {
        return ERR_FILE_NOT_FOUND;
    }

    if (!is_owner(metadata, client->username)) {
        return ERR_PERMISSION_DENIED;
    }

    AccessRequest* current = metadata->pending_requests;
    if (!current) {
        strcpy(response, "No pending requests\n");
        return ERR_SUCCESS;
    }

    size_t offset = 0;
    offset += snprintf(response + offset, BUFFER_SIZE - offset,
                       "Pending requests for %s:\n", filename);
    offset += snprintf(response + offset, BUFFER_SIZE - offset,
                       "%-16s %-8s %-20s\n", "USERNAME", "ACCESS", "REQUESTED_AT");
    offset += snprintf(response + offset, BUFFER_SIZE - offset,
                       "------------------------------------------------\n");

    while (current && offset < BUFFER_SIZE - 1) {
        char time_buf[32];
        struct tm* tm_info = localtime(&current->requested_time);
        if (tm_info) {
            strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        } else {
            strncpy(time_buf, "-", sizeof(time_buf));
            time_buf[sizeof(time_buf) - 1] = '\0';
        }
        offset += snprintf(response + offset, BUFFER_SIZE - offset,
                           "%-16s %-8s %-20s\n",
                           current->username,
                           access_to_string(current->requested_access),
                           time_buf);
        current = current->next;
    }

    response[offset] = '\0';
    return ERR_SUCCESS;
}

ErrorCode handle_process_request(NameServer* nm, Client* client, const char* filename,
                                const char* target_user, bool approve, char* response) {
    pthread_mutex_lock(&nm->trie_lock);
    FileMetadata* metadata = search_file_trie(nm->file_trie, filename);
    pthread_mutex_unlock(&nm->trie_lock);

    if (!metadata) {
        return ERR_FILE_NOT_FOUND;
    }

    if (!is_owner(metadata, client->username)) {
        return ERR_PERMISSION_DENIED;
    }

    AccessRequest* prev = NULL;
    AccessRequest* request = find_access_request(metadata, target_user, &prev);
    if (!request) {
        strcpy(response, "No pending request from that user");
        return ERR_INVALID_OPERATION;
    }

    AccessRight requested_access = request->requested_access;

    if (approve) {
        ErrorCode grant_err = add_access(nm, client, filename, target_user, requested_access);
        if (grant_err != ERR_SUCCESS) {
            return grant_err;
        }
        snprintf(response, BUFFER_SIZE, "Granted %s access to %s",
                 access_to_string(requested_access), target_user);
    } else {
        snprintf(response, BUFFER_SIZE, "Denied access request from %s", target_user);
    }

    if (prev) {
        prev->next = request->next;
    } else {
        metadata->pending_requests = request->next;
    }
    free(request);

    char details[256];
    snprintf(details, sizeof(details), "File=%s User=%s Action=%s", filename,
             target_user, approve ? "APPROVE" : "DENY");
    log_message(nm, "INFO", client->ip, client->nm_port, client->username,
                approve ? "APPROVE_REQUEST" : "DENY_REQUEST", details);

    return ERR_SUCCESS;
}

// ==================== USER MANAGEMENT ====================

ErrorCode handle_list_users(NameServer* nm, char* response) {
    pthread_mutex_lock(&nm->registry_lock);
    if (nm->registered_user_count == 0) {
        pthread_mutex_unlock(&nm->registry_lock);
        strcpy(response, "No users registered\n");
        return ERR_SUCCESS;
    }

    char buffer[BUFFER_SIZE * 2] = "Registered Users:\n";
    size_t offset = strlen(buffer);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                      "%-16s %-16s %-8s %-19s\n",
                      "USERNAME", "LAST_IP", "STATUS", "LAST_SEEN");
    offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                      "------------------------------------------------------------\n");

    for (int i = 0; i < nm->registered_user_count; i++) {
        RegisteredUser* user = nm->user_registry[i];
        if (!user) continue;
        char time_buf[32] = "-";
        if (user->last_seen > 0) {
            struct tm* tm_info = localtime(&user->last_seen);
            if (tm_info) {
                strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
            }
        }
        offset += snprintf(buffer + offset, sizeof(buffer) - offset,
                           "%-16s %-16s %-8s %-19s\n",
                           user->username,
                           user->last_ip[0] ? user->last_ip : "-",
                           user->is_active ? "ONLINE" : "OFFLINE",
                           time_buf);
        if (offset >= sizeof(buffer) - 1) {
            break;
        }
    }
    pthread_mutex_unlock(&nm->registry_lock);

    size_t copy_len = (offset < sizeof(buffer)) ? offset : sizeof(buffer) - 1;
    memcpy(response, buffer, copy_len);
    response[copy_len] = '\0';
    
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
