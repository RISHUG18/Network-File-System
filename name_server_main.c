#include "name_server.h"

// Thread argument structure
typedef struct {
    NameServer* nm;
    int socket_fd;
    struct sockaddr_in addr;
} ConnectionArgs;

// ==================== CONNECTION HANDLER ====================

void* handle_connection(void* arg) {
    ConnectionArgs* conn_args = (ConnectionArgs*)arg;
    NameServer* nm = conn_args->nm;
    int socket_fd = conn_args->socket_fd;
    struct sockaddr_in addr = conn_args->addr;
    
    char client_ip[MAX_IP_LEN];
    inet_ntop(AF_INET, &addr.sin_addr, client_ip, MAX_IP_LEN);
    int client_port = ntohs(addr.sin_port);
    
    free(conn_args);
    
    char buffer[BUFFER_SIZE];
    int bytes = recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes <= 0) {
        close(socket_fd);
        return NULL;
    }
    
    buffer[bytes] = '\0';
    
    // Parse initial message
    char cmd[64];
    char* args[10];
    int arg_count;
    parse_command(buffer, cmd, args, &arg_count);
    
    // Handle registration
    if (strcmp(cmd, "REGISTER_SS") == 0) {
        // Format: REGISTER_SS <nm_port> <client_port> <file_count> <file1> <file2> ...
        if (arg_count < 3) {
            send_response(socket_fd, ERR_INVALID_OPERATION, "Invalid SS registration");
            close(socket_fd);
            return NULL;
        }
        
        int nm_port = atoi(args[0]);
        int client_port = atoi(args[1]);
        int file_count = atoi(args[2]);
        
        char** files = NULL;
        if (file_count > 0 && arg_count >= 3 + file_count) {
            files = (char**)malloc(sizeof(char*) * file_count);
            for (int i = 0; i < file_count; i++) {
                files[i] = strdup(args[3 + i]);
            }
        } else {
            files = (char**)malloc(sizeof(char*) * 1);
            file_count = 0;
        }
        
        int ss_id = register_storage_server(nm, client_ip, nm_port, client_port, 
                                            files, file_count, socket_fd);
        
        if (ss_id < 0) {
            send_response(socket_fd, ERR_SYSTEM_ERROR, "Failed to register SS");
            for (int i = 0; i < file_count; i++) free(files[i]);
            free(files);
            close(socket_fd);
            return NULL;
        }
        
        char response[256];
        snprintf(response, sizeof(response), "SS registered with ID %d", ss_id);
        send_response(socket_fd, ERR_SUCCESS, response);
        
        // Keep connection open for SS
        // The SS will keep sending updates and we'll keep the socket open
        // For now, we'll just handle the registration and close
        
        for (int i = 0; i < arg_count; i++) {
            free(args[i]);
        }
        
        return NULL;
    }
    else if (strcmp(cmd, "REGISTER_CLIENT") == 0) {
        // Format: REGISTER_CLIENT <username> <nm_port> <ss_port>
        if (arg_count < 3) {
            send_response(socket_fd, ERR_INVALID_OPERATION, "Invalid client registration");
            for (int i = 0; i < arg_count; i++) free(args[i]);
            close(socket_fd);
            return NULL;
        }
        
        char* username = args[0];
        int nm_port = atoi(args[1]);
        int ss_port = atoi(args[2]);
        
        int client_id = register_client(nm, username, client_ip, nm_port, ss_port, socket_fd);
        
        if (client_id < 0) {
            send_response(socket_fd, ERR_SYSTEM_ERROR, "Failed to register client");
            for (int i = 0; i < arg_count; i++) free(args[i]);
            close(socket_fd);
            return NULL;
        }
        
        char response[256];
        snprintf(response, sizeof(response), "Client registered with ID %d", client_id);
        send_response(socket_fd, ERR_SUCCESS, response);
        
        Client* client = get_client(nm, client_id);
        
        // Handle client commands in a loop
        while (nm->is_running && client->is_active) {
            bytes = recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
            if (bytes <= 0) {
                deregister_client(nm, client_id);
                break;
            }
            
            buffer[bytes] = '\0';
            parse_command(buffer, cmd, args, &arg_count);
            
            ErrorCode error = ERR_SUCCESS;
            char response_msg[BUFFER_SIZE * 4] = {0};
            
            // Handle different commands
            if (strcmp(cmd, "VIEW") == 0) {
                char* flags = (arg_count > 0) ? args[0] : NULL;
                error = handle_view_files(nm, client, flags, response_msg);
            }
            else if (strcmp(cmd, "CREATE") == 0) {
                if (arg_count < 1) {
                    error = ERR_INVALID_OPERATION;
                    strcpy(response_msg, "Usage: CREATE <filename>");
                } else {
                    error = handle_create_file(nm, client, args[0]);
                    if (error == ERR_SUCCESS) {
                        snprintf(response_msg, sizeof(response_msg), 
                                "File '%s' created successfully", args[0]);
                    }
                }
            }
            else if (strcmp(cmd, "DELETE") == 0) {
                if (arg_count < 1) {
                    error = ERR_INVALID_OPERATION;
                    strcpy(response_msg, "Usage: DELETE <filename>");
                } else {
                    error = handle_delete_file(nm, client, args[0]);
                    if (error == ERR_SUCCESS) {
                        snprintf(response_msg, sizeof(response_msg), 
                                "File '%s' deleted successfully", args[0]);
                    }
                }
            }
            else if (strcmp(cmd, "READ") == 0) {
                if (arg_count < 1) {
                    error = ERR_INVALID_OPERATION;
                    strcpy(response_msg, "Usage: READ <filename>");
                } else {
                    error = handle_read_file(nm, client, args[0], response_msg);
                }
            }
            else if (strcmp(cmd, "WRITE") == 0) {
                if (arg_count < 2) {
                    error = ERR_INVALID_OPERATION;
                    strcpy(response_msg, "Usage: WRITE <filename> <sentence_number>");
                } else {
                    int sentence_num = atoi(args[1]);
                    error = handle_write_file(nm, client, args[0], sentence_num);
                    
                    if (error == ERR_SUCCESS) {
                        // Get SS info for client
                        pthread_mutex_lock(&nm->trie_lock);
                        FileMetadata* metadata = search_file_trie(nm->file_trie, args[0]);
                        pthread_mutex_unlock(&nm->trie_lock);
                        
                        if (metadata) {
                            StorageServer* ss = get_storage_server(nm, metadata->ss_id);
                            if (ss) {
                                snprintf(response_msg, sizeof(response_msg), 
                                        "SS_INFO %s %d", ss->ip, ss->client_port);
                            }
                        }
                    }
                }
            }
            else if (strcmp(cmd, "INFO") == 0) {
                if (arg_count < 1) {
                    error = ERR_INVALID_OPERATION;
                    strcpy(response_msg, "Usage: INFO <filename>");
                } else {
                    error = handle_info_file(nm, client, args[0], response_msg);
                }
            }
            else if (strcmp(cmd, "STREAM") == 0) {
                if (arg_count < 1) {
                    error = ERR_INVALID_OPERATION;
                    strcpy(response_msg, "Usage: STREAM <filename>");
                } else {
                    error = handle_stream_file(nm, client, args[0], response_msg);
                }
            }
            else if (strcmp(cmd, "EXEC") == 0) {
                if (arg_count < 1) {
                    error = ERR_INVALID_OPERATION;
                    strcpy(response_msg, "Usage: EXEC <filename>");
                } else {
                    error = handle_exec_file(nm, client, args[0], response_msg);
                }
            }
            else if (strcmp(cmd, "UNDO") == 0) {
                if (arg_count < 1) {
                    error = ERR_INVALID_OPERATION;
                    strcpy(response_msg, "Usage: UNDO <filename>");
                } else {
                    error = handle_undo_file(nm, client, args[0]);
                    if (error == ERR_SUCCESS) {
                        snprintf(response_msg, sizeof(response_msg), 
                                "Last change to '%s' undone", args[0]);
                    }
                }
            }
            else if (strcmp(cmd, "LIST") == 0) {
                error = handle_list_users(nm, response_msg);
            }
            else if (strcmp(cmd, "ADDACCESS") == 0) {
                if (arg_count < 3) {
                    error = ERR_INVALID_OPERATION;
                    strcpy(response_msg, "Usage: ADDACCESS -R|-W <filename> <username>");
                } else {
                    AccessRight access = (strcmp(args[0], "-W") == 0) ? 
                                        ACCESS_WRITE : ACCESS_READ;
                    error = add_access(nm, client, args[1], args[2], access);
                    if (error == ERR_SUCCESS) {
                        snprintf(response_msg, sizeof(response_msg), 
                                "Access granted to %s for file '%s'", args[2], args[1]);
                    }
                }
            }
            else if (strcmp(cmd, "REMACCESS") == 0) {
                if (arg_count < 2) {
                    error = ERR_INVALID_OPERATION;
                    strcpy(response_msg, "Usage: REMACCESS <filename> <username>");
                } else {
                    error = remove_access(nm, client, args[0], args[1]);
                    if (error == ERR_SUCCESS) {
                        snprintf(response_msg, sizeof(response_msg), 
                                "Access removed from %s for file '%s'", args[1], args[0]);
                    }
                }
            }
            else if (strcmp(cmd, "QUIT") == 0 || strcmp(cmd, "EXIT") == 0) {
                strcpy(response_msg, "Goodbye!");
                send_response(socket_fd, ERR_SUCCESS, response_msg);
                deregister_client(nm, client_id);
                break;
            }
            else {
                error = ERR_INVALID_OPERATION;
                snprintf(response_msg, sizeof(response_msg), 
                        "Unknown command: %s", cmd);
            }
            
            // Send response
            if (error != ERR_SUCCESS && strlen(response_msg) == 0) {
                strcpy(response_msg, error_to_string(error));
            }
            
            send_response(socket_fd, error, response_msg);
            
            // Free args
            for (int i = 0; i < arg_count; i++) {
                free(args[i]);
            }
        }
        
        close(socket_fd);
        return NULL;
    }
    else {
        send_response(socket_fd, ERR_INVALID_OPERATION, "Invalid registration type");
        for (int i = 0; i < arg_count; i++) free(args[i]);
        close(socket_fd);
        return NULL;
    }
    
    return NULL;
}

// ==================== MAIN NAME SERVER LOOP ====================

void start_name_server(NameServer* nm) {
    printf("Name Server started on port %d\n", nm->nm_port);
    printf("Waiting for connections...\n\n");
    
    while (nm->is_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(nm->socket_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (nm->is_running) {
                perror("Accept failed");
            }
            continue;
        }
        
        // Create thread to handle connection
        pthread_t thread;
        ConnectionArgs* args = (ConnectionArgs*)malloc(sizeof(ConnectionArgs));
        args->nm = nm;
        args->socket_fd = client_fd;
        args->addr = client_addr;
        
        if (pthread_create(&thread, NULL, handle_connection, args) != 0) {
            perror("Failed to create thread");
            free(args);
            close(client_fd);
            continue;
        }
        
        pthread_detach(thread);
    }
}

// ==================== SIGNAL HANDLER ====================

void signal_handler(int signum) {
    if (g_nm) {
        printf("\nShutting down Name Server...\n");
        g_nm->is_running = false;
        shutdown(g_nm->socket_fd, SHUT_RDWR);
    }
}

// ==================== MAIN ====================

int main(int argc, char* argv[]) {
    int port = 8080;  // Default port
    
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number. Using default: 8080\n");
            port = 8080;
        }
    }
    
    // Initialize name server
    g_nm = init_name_server(port);
    if (!g_nm) {
        fprintf(stderr, "Failed to initialize Name Server\n");
        return 1;
    }
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Start server
    start_name_server(g_nm);
    
    // Cleanup
    destroy_name_server(g_nm);
    
    return 0;
}
