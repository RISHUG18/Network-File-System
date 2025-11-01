#include "client.h"

// ==================== HELP AND INTERACTIVE MODE ====================

void print_help() {
    printf("\nLangOS Distributed File System - Client\n");
    printf("============================================================\n");
    printf("Commands:\n");
    printf("  view [flags]                  - List files (-a for all, -l for detailed)\n");
    printf("  create <file>                 - Create a file\n");
    printf("  delete <file>                 - Delete a file\n");
    printf("  info <file>                   - Get file information\n");
    printf("  read <file>                   - Read file content (direct SS)\n");
    printf("  write <file> <sentence#>      - Write to file (direct SS, ETIRW protocol)\n");
    printf("  stream <file>                 - Stream file word-by-word (direct SS)\n");
    printf("  exec <file>                   - Execute file as script\n");
    printf("  undo <file>                   - Undo last change\n");
    printf("  addaccess <R|W> <file> <user> - Grant access\n");
    printf("  remaccess <file> <user>       - Revoke access\n");
    printf("  users                         - List all users\n");
    printf("  help                          - Show this help\n");
    printf("  quit                          - Disconnect\n");
    printf("============================================================\n");
}

void interactive_mode(Client* client) {
    print_help();
    
    char line[BUFFER_SIZE];
    
    while (1) {
        printf("\n%s> ", client->username);
        fflush(stdout);
        
        if (!fgets(line, sizeof(line), stdin)) {
            break;  // EOF
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
        
        // Convert to lowercase for comparison
        for (char* p = cmd; *p; p++) {
            *p = tolower(*p);
        }
        
        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
            break;
        }
        else if (strcmp(cmd, "help") == 0) {
            print_help();
        }
        else if (strcmp(cmd, "view") == 0) {
            char* flags = strtok(NULL, "");
            if (flags) {
                // Skip leading whitespace
                while (*flags == ' ' || *flags == '\t') {
                    flags++;
                }
            }
            cmd_view_files(client, flags ? flags : "");
        }
        else if (strcmp(cmd, "create") == 0) {
            char* filename = strtok(NULL, " ");
            if (!filename) {
                printf("Usage: create <filename>\n");
            } else {
                cmd_create_file(client, filename);
            }
        }
        else if (strcmp(cmd, "delete") == 0) {
            char* filename = strtok(NULL, " ");
            if (!filename) {
                printf("Usage: delete <filename>\n");
            } else {
                cmd_delete_file(client, filename);
            }
        }
        else if (strcmp(cmd, "info") == 0) {
            char* filename = strtok(NULL, " ");
            if (!filename) {
                printf("Usage: info <filename>\n");
            } else {
                cmd_file_info(client, filename);
            }
        }
        else if (strcmp(cmd, "read") == 0) {
            char* filename = strtok(NULL, " ");
            if (!filename) {
                printf("Usage: read <filename>\n");
            } else {
                cmd_read_file(client, filename);
            }
        }
        else if (strcmp(cmd, "write") == 0) {
            char* filename = strtok(NULL, " ");
            char* sentence_str = strtok(NULL, " ");
            if (!filename || !sentence_str) {
                printf("Usage: write <filename> <sentence_number>\n");
            } else {
                int sentence_num = atoi(sentence_str);
                cmd_write_file(client, filename, sentence_num);
            }
        }
        else if (strcmp(cmd, "stream") == 0) {
            char* filename = strtok(NULL, " ");
            if (!filename) {
                printf("Usage: stream <filename>\n");
            } else {
                cmd_stream_file(client, filename);
            }
        }
        else if (strcmp(cmd, "exec") == 0) {
            char* filename = strtok(NULL, " ");
            if (!filename) {
                printf("Usage: exec <filename>\n");
            } else {
                cmd_exec_file(client, filename);
            }
        }
        else if (strcmp(cmd, "undo") == 0) {
            char* filename = strtok(NULL, " ");
            if (!filename) {
                printf("Usage: undo <filename>\n");
            } else {
                cmd_undo_file(client, filename);
            }
        }
        else if (strcmp(cmd, "addaccess") == 0) {
            char* access_type = strtok(NULL, " ");
            char* filename = strtok(NULL, " ");
            char* target_user = strtok(NULL, " ");
            if (!access_type || !filename || !target_user) {
                printf("Usage: addaccess <R|W> <filename> <username>\n");
            } else {
                cmd_add_access(client, filename, target_user, access_type[0]);
            }
        }
        else if (strcmp(cmd, "remaccess") == 0) {
            char* filename = strtok(NULL, " ");
            char* target_user = strtok(NULL, " ");
            if (!filename || !target_user) {
                printf("Usage: remaccess <filename> <username>\n");
            } else {
                cmd_remove_access(client, filename, target_user);
            }
        }
        else if (strcmp(cmd, "users") == 0) {
            cmd_list_users(client);
        }
        else {
            printf("Unknown command: %s\n", cmd);
            printf("Type 'help' for available commands\n");
        }
    }
}

// ==================== MAIN ====================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <username> [nm_host] [nm_port]\n", argv[0]);
        fprintf(stderr, "Example: %s alice localhost 8080\n", argv[0]);
        return 1;
    }
    
    const char* username = argv[1];
    const char* nm_host = (argc > 2) ? argv[2] : "localhost";
    int nm_port = (argc > 3) ? atoi(argv[3]) : 8080;
    
    // Create client
    Client* client = create_client(username, nm_host, nm_port);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    // Connect to Name Server
    printf("Connecting to Name Server at %s:%d...\n", nm_host, nm_port);
    if (!connect_to_nm(client)) {
        fprintf(stderr, "Failed to connect to Name Server\n");
        destroy_client(client);
        return 1;
    }
    
    // Run interactive mode
    interactive_mode(client);
    
    // Cleanup
    destroy_client(client);
    
    return 0;
}
