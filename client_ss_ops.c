#include "client.h"

// ==================== DIRECT STORAGE SERVER OPERATIONS ====================

void cmd_read_file(Client* client, const char* filename) {
    // Step 1: Get SS info from Name Server
    char command[512];
    snprintf(command, sizeof(command), "READ %s", filename);
    
    char ss_ip[64];
    int ss_port;
    
    if (!get_ss_info(client, command, ss_ip, &ss_port)) {
        return;
    }
    
    // Step 2: Connect to Storage Server
    printf("Connecting to Storage Server...\n");
    int ss_socket = connect_to_ss(ss_ip, ss_port);
    if (ss_socket < 0) {
        printf("✗ Failed to connect to Storage Server\n");
        return;
    }
    
    // Step 3: Send READ command to SS
    char ss_command[512];
    snprintf(ss_command, sizeof(ss_command), "READ %s", filename);
    
    char response[MAX_CONTENT];
    int bytes = send_ss_command(ss_socket, ss_command, response, sizeof(response));
    
    close(ss_socket);
    
    if (bytes < 0) {
        printf("✗ Failed to read from Storage Server\n");
        return;
    }
    
    // Check if response is an error
    if (strncmp(response, "ERROR:", 6) == 0) {
        printf("✗ %s\n", response + 6);
    } else {
        printf("\n--- File Content ---\n");
        printf("%s", response);
        if (response[strlen(response) - 1] != '\n') {
            printf("\n");
        }
        printf("--- End of File ---\n");
    }
}

void cmd_write_file(Client* client, const char* filename, int sentence_num) {
    // Step 1: Get SS info from Name Server
    char command[512];
    snprintf(command, sizeof(command), "WRITE %s %d", filename, sentence_num);
    
    char ss_ip[64];
    int ss_port;
    
    if (!get_ss_info(client, command, ss_ip, &ss_port)) {
        return;
    }
    
    // Step 2: Connect to Storage Server
    printf("Connecting to Storage Server...\n");
    int ss_socket = connect_to_ss(ss_ip, ss_port);
    if (ss_socket < 0) {
        printf("✗ Failed to connect to Storage Server\n");
        return;
    }
    
    // Step 3: Send WRITE command (this locks the sentence)
    printf("Locking sentence %d for writing...\n", sentence_num);
    char write_command[512];
    snprintf(write_command, sizeof(write_command), "WRITE %s %d", filename, sentence_num);
    
    char response[BUFFER_SIZE];
    send_ss_command(ss_socket, write_command, response, sizeof(response));
    
    // Check if lock was acquired
    if (strncmp(response, "LOCKED", 6) != 0 && strncmp(response, "0:", 2) != 0 && strncmp(response, "SUCCESS", 7) != 0) {
        printf("✗ Failed to lock sentence: %s\n", response);
        close(ss_socket);
        return;
    }
    
    printf("✓ Sentence locked\n");
    printf("\nWrite Mode - Direct Protocol\n");
    printf("Format: <word_index> <content>\n");
    printf("Type 'ETIRW' to finalize changes and unlock\n\n");
    
    char line[BUFFER_SIZE];
    bool done = false;
    
    while (!done) {
        printf("write> ");
        fflush(stdout);
        
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        
        if (strlen(line) == 0) {
            continue;
        }
        
        // Check for ETIRW (finalize)
        if (strcmp(line, "ETIRW") == 0) {
            printf("Finalizing changes...\n");
            send_ss_command(ss_socket, "ETIRW", response, sizeof(response));
            
            if (strncmp(response, "SUCCESS", 7) == 0 || strncmp(response, "0:", 2) == 0) {
                printf("✓ Changes finalized and sentence unlocked\n");
            } else {
                printf("✗ Failed to finalize: %s\n", response);
            }
            done = true;
        }
        // Parse as "<word_index> <content>"
        else {
            char* word_index_str = strtok(line, " ");
            char* new_content = strtok(NULL, "");  // Get rest of line
            
            if (!word_index_str || !new_content) {
                printf("Usage: <word_index> <content>\n");
                continue;
            }
            
            // Skip leading whitespace in new_content
            while (*new_content == ' ' || *new_content == '\t') {
                new_content++;
            }
            
            // Send the word update command directly
            char update_cmd[BUFFER_SIZE];
            snprintf(update_cmd, sizeof(update_cmd), "%s %s", word_index_str, new_content);
            
            send_ss_command(ss_socket, update_cmd, response, sizeof(response));
            
            if (strncmp(response, "SUCCESS", 7) == 0 || strncmp(response, "0:", 2) == 0) {
                printf("✓ Word updated\n");
            } else {
                printf("✗ Update failed: %s\n", response);
            }
        }
    }
    
    close(ss_socket);
}

void cmd_stream_file(Client* client, const char* filename) {
    // Step 1: Get SS info from Name Server
    char command[512];
    snprintf(command, sizeof(command), "STREAM %s", filename);
    
    char ss_ip[64];
    int ss_port;
    
    if (!get_ss_info(client, command, ss_ip, &ss_port)) {
        return;
    }
    
    // Step 2: Connect to Storage Server
    printf("Connecting to Storage Server...\n");
    int ss_socket = connect_to_ss(ss_ip, ss_port);
    if (ss_socket < 0) {
        printf("✗ Failed to connect to Storage Server\n");
        return;
    }
    
    // Step 3: Send STREAM command to SS
    char ss_command[512];
    snprintf(ss_command, sizeof(ss_command), "STREAM %s", filename);
    
    if (send(ss_socket, ss_command, strlen(ss_command), 0) < 0) {
        perror("Failed to send STREAM command");
        close(ss_socket);
        return;
    }
    
    // Don't add newline for STREAM - check SS implementation
    const char* newline = "\n";
    send(ss_socket, newline, 1, 0);
    
    // Step 4: Receive and display words one by one
    printf("\n--- Streaming File ---\n");

    char buffer[BUFFER_SIZE];
    char pending[BUFFER_SIZE];
    size_t pending_len = 0;
    bool saw_data = false;
    bool done = false;
    bool first_token = true;

    while (!done) {
        ssize_t bytes = recv(ss_socket, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            break;  // Connection closed or error
        }

        saw_data = true;

        if (pending_len + (size_t)bytes >= sizeof(pending)) {
            pending_len = 0;  // Prevent overflow by dropping stale partial data
        }

        memcpy(pending + pending_len, buffer, (size_t)bytes);
        pending_len += (size_t)bytes;

        size_t processed = 0;
        while (processed < pending_len) {
            char* newline = memchr(pending + processed, '\n', pending_len - processed);
            if (!newline) {
                break;  // Need more data for a complete line
            }

            size_t line_len = (size_t)(newline - (pending + processed));
            char line[BUFFER_SIZE];
            if (line_len >= sizeof(line)) {
                line_len = sizeof(line) - 1;
            }
            memcpy(line, pending + processed, line_len);
            line[line_len] = '\0';

            if (processed == 0 && strncmp(line, "ERROR:", 6) == 0) {
                printf("✗ %s\n", line + 6);
                done = true;
                break;
            }

            if (strcmp(line, "STOP") == 0) {
                done = true;
                break;
            }

            if (line_len > 0) {
                if (!first_token) {
                    printf(" ");
                }

                printf("%s", line);
                fflush(stdout);
                first_token = false;
            }

            processed = (size_t)(newline - pending) + 1;
        }

        if (processed > 0) {
            memmove(pending, pending + processed, pending_len - processed);
            pending_len -= processed;
        }
    }

    if (!done) {
        if (saw_data) {
            if (!first_token) printf("\n");
            printf("✗ Stream interrupted: Storage Server closed connection unexpectedly\n");
        } else {
            printf("✗ No data received\n");
        }
    } else {
        if (!first_token) {
            printf("\n");
        }
        printf("--- End of Stream ---\n");
    }

    close(ss_socket);
}
