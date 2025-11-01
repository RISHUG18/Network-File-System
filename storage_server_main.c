#include "storage_server.h"

// ==================== NETWORKING ====================

void send_response(int socket_fd, const char* message) {
    send(socket_fd, message, strlen(message), 0);
}

bool register_with_nm(StorageServer* ss) {
    // Connect to Name Server
    ss->nm_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ss->nm_socket_fd < 0) {
        perror("Failed to create NM socket");
        return false;
    }
    
    struct sockaddr_in nm_addr;
    memset(&nm_addr, 0, sizeof(nm_addr));
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(ss->nm_port);
    
    if (inet_pton(AF_INET, ss->nm_ip, &nm_addr.sin_addr) <= 0) {
        perror("Invalid NM address");
        close(ss->nm_socket_fd);
        return false;
    }
    
    printf("Connecting to Name Server at %s:%d...\n", ss->nm_ip, ss->nm_port);
    
    if (connect(ss->nm_socket_fd, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Failed to connect to Name Server");
        close(ss->nm_socket_fd);
        return false;
    }
    
    // Build registration message
    char reg_msg[BUFFER_SIZE * 4];
    int offset = snprintf(reg_msg, sizeof(reg_msg), "REGISTER_SS %d %d %d", 
                         ss->nm_port, ss->client_port, ss->file_count);
    
    // Add file list
    for (int i = 0; i < ss->file_count && offset < (int)sizeof(reg_msg) - 256; i++) {
        offset += snprintf(reg_msg + offset, sizeof(reg_msg) - offset, 
                          " %s", ss->files[i]->filename);
    }
    
    strcat(reg_msg, "\n");
    
    // Send registration
    if (send(ss->nm_socket_fd, reg_msg, strlen(reg_msg), 0) < 0) {
        perror("Failed to send registration");
        close(ss->nm_socket_fd);
        return false;
    }
    
    // Receive response
    char response[BUFFER_SIZE];
    int bytes = recv(ss->nm_socket_fd, response, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) {
        perror("Failed to receive registration response");
        close(ss->nm_socket_fd);
        return false;
    }
    response[bytes] = '\0';
    
    // Parse response
    int error_code;
    char message[BUFFER_SIZE];
    if (sscanf(response, "%d:%[^\n]", &error_code, message) >= 1) {
        if (error_code == 0) {
            // Extract SS ID from message: "SS registered with ID X"
            sscanf(message, "SS registered with ID %d", &ss->ss_id);
            printf("✓ Registered with Name Server (SS ID: %d)\n", ss->ss_id);
            log_message(ss, "INFO", "REGISTER_NM", message);
            return true;
        } else {
            printf("✗ Registration failed: %s\n", message);
            close(ss->nm_socket_fd);
            return false;
        }
    }
    
    close(ss->nm_socket_fd);
    return false;
}

// ==================== NM CONNECTION HANDLER ====================

void* handle_nm_connection(void* arg) {
    StorageServer* ss = (StorageServer*)arg;
    char buffer[BUFFER_SIZE];
    
    while (ss->is_running) {
        int bytes = recv(ss->nm_socket_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            if (ss->is_running) {
                printf("Name Server disconnected\n");
                log_message(ss, "WARN", "NM_DISCONNECT", "Connection lost");
            }
            break;
        }
        
        buffer[bytes] = '\0';
        
        char cmd[64];
        char* args[10];
        int arg_count;
        parse_command(buffer, cmd, args, &arg_count);
        
        char response[BUFFER_SIZE];
        
        if (strcmp(cmd, "CREATE") == 0 && arg_count >= 1) {
            FileEntry* file = create_file(ss, args[0]);
            if (file) {
                strcpy(response, "SUCCESS\n");
            } else {
                strcpy(response, "ERROR:File already exists\n");
            }
            send_response(ss->nm_socket_fd, response);
        }
        else if (strcmp(cmd, "DELETE") == 0 && arg_count >= 1) {
            ErrorCode err = delete_file(ss, args[0]);
            if (err == ERR_SUCCESS) {
                strcpy(response, "SUCCESS\n");
            } else {
                snprintf(response, sizeof(response), "ERROR:%s\n", error_to_string(err));
            }
            send_response(ss->nm_socket_fd, response);
        }
        else if (strcmp(cmd, "INFO") == 0 && arg_count >= 1) {
            size_t size;
            int words, chars;
            ErrorCode err = get_file_info(ss, args[0], &size, &words, &chars);
            if (err == ERR_SUCCESS) {
                snprintf(response, sizeof(response), "SIZE:%zu WORDS:%d CHARS:%d\n",
                        size, words, chars);
            } else {
                snprintf(response, sizeof(response), "ERROR:%s\n", error_to_string(err));
            }
            send_response(ss->nm_socket_fd, response);
        }
        else if (strcmp(cmd, "READ") == 0 && arg_count >= 1) {
            char content[MAX_CONTENT_SIZE];
            size_t size;
            ErrorCode err = read_file(ss, args[0], content, &size);
            if (err == ERR_SUCCESS) {
                send_response(ss->nm_socket_fd, content);
                send_response(ss->nm_socket_fd, "\n");
            } else {
                snprintf(response, sizeof(response), "ERROR:%s\n", error_to_string(err));
                send_response(ss->nm_socket_fd, response);
            }
        }
        else if (strcmp(cmd, "UNDO") == 0 && arg_count >= 1) {
            ErrorCode err = handle_undo(ss, args[0]);
            if (err == ERR_SUCCESS) {
                strcpy(response, "SUCCESS\n");
            } else {
                snprintf(response, sizeof(response), "ERROR:%s\n", error_to_string(err));
            }
            send_response(ss->nm_socket_fd, response);
        }
        else {
            strcpy(response, "ERROR:Unknown command\n");
            send_response(ss->nm_socket_fd, response);
        }
        
        // Free args
        for (int i = 0; i < arg_count; i++) {
            free(args[i]);
        }
    }
    
    return NULL;
}

// ==================== CLIENT CONNECTION HANDLER ====================

void* handle_client_connection(void* arg) {
    ConnectionArgs* conn_args = (ConnectionArgs*)arg;
    StorageServer* ss = conn_args->ss;
    int client_fd = conn_args->socket_fd;
    
    char client_ip[16];
    inet_ntop(AF_INET, &conn_args->addr.sin_addr, client_ip, sizeof(client_ip));
    
    free(conn_args);
    
    char buffer[BUFFER_SIZE];
    int client_id = client_fd;  // Use socket FD as client ID for simplicity
    
    while (ss->is_running) {
        int bytes = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            break;
        }
        
        buffer[bytes] = '\0';
        
        char cmd[64];
        char* args[10];
        int arg_count;
        parse_command(buffer, cmd, args, &arg_count);
        
        if (strcmp(cmd, "READ") == 0 && arg_count >= 1) {
            char content[MAX_CONTENT_SIZE];
            size_t size;
            ErrorCode err = read_file(ss, args[0], content, &size);
            if (err == ERR_SUCCESS) {
                send_response(client_fd, content);
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "ERROR:%s\n", error_to_string(err));
                send_response(client_fd, error_msg);
            }
        }
        else if (strcmp(cmd, "STREAM") == 0 && arg_count >= 1) {
            ErrorCode err = stream_file(ss, client_fd, args[0]);
            if (err != ERR_SUCCESS) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "ERROR:%s\n", error_to_string(err));
                send_response(client_fd, error_msg);
            }
        }
        else if (strcmp(cmd, "WRITE_LOCK") == 0 && arg_count >= 2) {
            int sentence_num = atoi(args[1]);
            ErrorCode err = lock_sentence(ss, args[0], sentence_num, client_id);
            
            if (err == ERR_SUCCESS) {
                send_response(client_fd, "LOCKED\n");
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "ERROR:%s\n", error_to_string(err));
                send_response(client_fd, error_msg);
            }
        }
        else if (strcmp(cmd, "WRITE") == 0 && arg_count >= 4) {
            // WRITE <filename> <sentence_num> <word_index> <content>
            int sentence_num = atoi(args[1]);
            int word_index = atoi(args[2]);
            
            // Reconstruct content (may have spaces)
            char content[BUFFER_SIZE] = {0};
            for (int i = 3; i < arg_count; i++) {
                if (i > 3) strcat(content, " ");
                strcat(content, args[i]);
            }
            
            ErrorCode err = write_sentence(ss, args[0], sentence_num, word_index, content, client_id);
            
            if (err == ERR_SUCCESS) {
                send_response(client_fd, "SUCCESS\n");
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "ERROR:%s\n", error_to_string(err));
                send_response(client_fd, error_msg);
            }
        }
        else if (strcmp(cmd, "WRITE_UNLOCK") == 0 && arg_count >= 2) {
            int sentence_num = atoi(args[1]);
            ErrorCode err = unlock_sentence(ss, args[0], sentence_num, client_id);
            
            if (err == ERR_SUCCESS) {
                send_response(client_fd, "UNLOCKED\n");
            } else {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg), "ERROR:%s\n", error_to_string(err));
                send_response(client_fd, error_msg);
            }
        }
        else {
            send_response(client_fd, "ERROR:Unknown command\n");
        }
        
        // Free args
        for (int i = 0; i < arg_count; i++) {
            free(args[i]);
        }
    }
    
    close(client_fd);
    return NULL;
}

// ==================== CLIENT SERVER ====================

void start_client_server(StorageServer* ss) {
    // Create client socket
    ss->client_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ss->client_socket_fd < 0) {
        perror("Failed to create client socket");
        return;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(ss->client_socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(ss->client_socket_fd);
        return;
    }
    
    // Bind socket
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(ss->client_port);
    
    if (bind(ss->client_socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(ss->client_socket_fd);
        return;
    }
    
    // Listen
    if (listen(ss->client_socket_fd, 10) < 0) {
        perror("Listen failed");
        close(ss->client_socket_fd);
        return;
    }
    
    printf("Storage Server listening for clients on port %d\n", ss->client_port);
    log_message(ss, "INFO", "CLIENT_SERVER_START", "Listening for client connections");
    
    // Accept client connections
    while (ss->is_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(ss->client_socket_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (ss->is_running) {
                perror("Accept failed");
            }
            continue;
        }
        
        char client_ip[16];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("Client connected: %s:%d\n", client_ip, ntohs(client_addr.sin_port));
        
        // Create thread to handle client
        pthread_t thread;
        ConnectionArgs* args = (ConnectionArgs*)malloc(sizeof(ConnectionArgs));
        args->ss = ss;
        args->socket_fd = client_fd;
        args->addr = client_addr;
        
        if (pthread_create(&thread, NULL, handle_client_connection, args) != 0) {
            perror("Failed to create client thread");
            free(args);
            close(client_fd);
            continue;
        }
        
        pthread_detach(thread);
    }
}

// ==================== MAIN ====================

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <nm_ip> <nm_port> <client_port>\n", argv[0]);
        fprintf(stderr, "Example: %s 127.0.0.1 8080 9002\n", argv[0]);
        return 1;
    }
    
    const char* nm_ip = argv[1];
    int nm_port = atoi(argv[2]);
    int client_port = atoi(argv[3]);
    
    // Initialize storage server
    StorageServer* ss = init_storage_server(nm_ip, nm_port, client_port);
    if (!ss) {
        fprintf(stderr, "Failed to initialize Storage Server\n");
        return 1;
    }
    
    // Register with Name Server
    if (!register_with_nm(ss)) {
        fprintf(stderr, "Failed to register with Name Server\n");
        destroy_storage_server(ss);
        return 1;
    }
    
    // Start NM connection handler thread
    pthread_t nm_thread;
    if (pthread_create(&nm_thread, NULL, handle_nm_connection, ss) != 0) {
        perror("Failed to create NM thread");
        destroy_storage_server(ss);
        return 1;
    }
    pthread_detach(nm_thread);
    
    // Start client server (blocks)
    start_client_server(ss);
    
    // Cleanup
    destroy_storage_server(ss);
    
    return 0;
}
