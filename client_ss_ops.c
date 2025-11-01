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
    
    // Step 3: Lock the sentence
    printf("Locking sentence %d...\n", sentence_num);
    char lock_command[512];
    snprintf(lock_command, sizeof(lock_command), "WRITE_LOCK %s %d", filename, sentence_num);
    
    char response[BUFFER_SIZE];
    send_ss_command(ss_socket, lock_command, response, sizeof(response));
    
    if (strncmp(response, "LOCKED", 6) != 0) {
        printf("✗ Failed to lock sentence: %s\n", response);
        close(ss_socket);
        return;
    }
    
    printf("✓ Sentence locked\n");
    
    // Step 4: Interactive write loop
    printf("\nWrite Mode (ETIRW Protocol)\n");
    printf("Commands:\n");
    printf("  write <word_index> <new_word>  - Update word at index\n");
    printf("  done                           - Finish and unlock\n");
    printf("  cancel                         - Cancel and unlock\n");
    
    char line[BUFFER_SIZE];
    bool done = false;
    
    while (!done) {
        printf("\nwrite> ");
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
        
        // Parse command
        char* cmd = strtok(line, " ");
        if (!cmd) {
            continue;
        }
        
        if (strcmp(cmd, "done") == 0 || strcmp(cmd, "cancel") == 0) {
            done = true;
        } else if (strcmp(cmd, "write") == 0) {
            char* word_index_str = strtok(NULL, " ");
            char* new_word = strtok(NULL, "");  // Get rest of line
            
            if (!word_index_str || !new_word) {
                printf("Usage: write <word_index> <new_word>\n");
                continue;
            }
            
            // Skip leading whitespace in new_word
            while (*new_word == ' ' || *new_word == '\t') {
                new_word++;
            }
            
            int word_index = atoi(word_index_str);
            
            // Send WRITE command to SS
            char write_command[BUFFER_SIZE];
            snprintf(write_command, sizeof(write_command), "WRITE %s %d %d %s", 
                    filename, sentence_num, word_index, new_word);
            
            send_ss_command(ss_socket, write_command, response, sizeof(response));
            
            if (strncmp(response, "SUCCESS", 7) == 0) {
                printf("✓ Word updated successfully\n");
            } else {
                printf("✗ Write failed: %s\n", response);
            }
        } else {
            printf("Unknown command: %s\n", cmd);
        }
    }
    
    // Step 5: Unlock the sentence
    printf("Unlocking sentence...\n");
    char unlock_command[512];
    snprintf(unlock_command, sizeof(unlock_command), "WRITE_UNLOCK %s %d", filename, sentence_num);
    
    send_ss_command(ss_socket, unlock_command, response, sizeof(response));
    
    if (strncmp(response, "UNLOCKED", 8) == 0) {
        printf("✓ Sentence unlocked\n");
    } else {
        printf("✗ Failed to unlock: %s\n", response);
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
    ssize_t total_bytes = 0;
    
    while (1) {
        ssize_t bytes = recv(ss_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            break;  // Connection closed or error
        }
        
        buffer[bytes] = '\0';
        
        // Check for error message
        if (total_bytes == 0 && strncmp(buffer, "ERROR:", 6) == 0) {
            printf("✗ %s\n", buffer + 6);
            close(ss_socket);
            return;
        }
        
        // Print the received data (words with delays)
        printf("%s", buffer);
        fflush(stdout);
        
        total_bytes += bytes;
    }
    
    if (total_bytes > 0) {
        printf("\n--- End of Stream ---\n");
    } else {
        printf("✗ No data received\n");
    }
    
    close(ss_socket);
}
