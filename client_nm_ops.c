#include "client.h"

// ==================== NAME SERVER OPERATIONS ====================

void cmd_view_files(Client* client, const char* flags) {
    char command[256];
    if (flags && strlen(flags) > 0) {
        snprintf(command, sizeof(command), "VIEW %s", flags);
    } else {
        snprintf(command, sizeof(command), "VIEW");
    }
    
    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));
    
    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }
    
    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("\n%s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_create_file(Client* client, const char* filename) {
    char command[512];
    snprintf(command, sizeof(command), "CREATE %s", filename);
    
    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));
    
    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }
    
    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("✓ %s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_delete_file(Client* client, const char* filename) {
    char command[512];
    snprintf(command, sizeof(command), "DELETE %s", filename);
    
    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));
    
    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }
    
    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("✓ %s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_file_info(Client* client, const char* filename) {
    char command[512];
    snprintf(command, sizeof(command), "INFO %s", filename);
    
    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));
    
    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }
    
    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("\n%s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_exec_file(Client* client, const char* filename) {
    char command[512];
    snprintf(command, sizeof(command), "EXEC %s", filename);
    
    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));
    
    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }
    
    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("\n%s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_undo_file(Client* client, const char* filename) {
    char command[512];
    snprintf(command, sizeof(command), "UNDO %s", filename);
    
    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));
    
    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }
    
    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("✓ %s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_list_users(Client* client) {
    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, "LIST", response, sizeof(response));
    
    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }
    
    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("\n%s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_add_access(Client* client, const char* filename, const char* target_user, char access_type) {
    char command[512];
    char flag[4];
    
    if (access_type == 'W' || access_type == 'w') {
        snprintf(flag, sizeof(flag), "-W");
    } else {
        snprintf(flag, sizeof(flag), "-R");
    }
    
    snprintf(command, sizeof(command), "ADDACCESS %s %s %s", flag, filename, target_user);
    
    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));
    
    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }
    
    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("✓ %s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_remove_access(Client* client, const char* filename, const char* target_user) {
    char command[512];
    snprintf(command, sizeof(command), "REMACCESS %s %s", filename, target_user);
    
    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));
    
    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }
    
    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("✓ %s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_request_access(Client* client, const char* filename, char access_type) {
    char command[512];
    const char* flag = (access_type == 'W' || access_type == 'w') ? "-W" : "-R";
    snprintf(command, sizeof(command), "REQACCESS %s %s", flag, filename);

    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));

    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }

    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("✓ %s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_list_requests(Client* client, const char* filename) {
    char command[512];
    snprintf(command, sizeof(command), "LISTREQUESTS %s", filename);

    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));

    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }

    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("\n%s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_process_request(Client* client, const char* filename, const char* target_user, bool approve) {
    char command[512];
    snprintf(command, sizeof(command), "PROCESSREQUEST %s %s %s", filename, target_user,
             approve ? "APPROVE" : "DENY");

    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));

    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }

    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("✓ %s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_checkpoint(Client* client, const char* filename, const char* tag) {
    char command[512];
    snprintf(command, sizeof(command), "CHECKPOINT %s %s", filename, tag);

    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));

    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }

    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("✓ %s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_view_checkpoint(Client* client, const char* filename, const char* tag) {
    char command[512];
    snprintf(command, sizeof(command), "VIEWCHECKPOINT %s %s", filename, tag);

    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));

    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }

    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("\n--- Checkpoint %s:%s ---\n%s\n", filename, tag, message);
            printf("--- End Checkpoint ---\n");
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_revert_checkpoint(Client* client, const char* filename, const char* tag) {
    char command[512];
    snprintf(command, sizeof(command), "REVERT %s %s", filename, tag);

    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));

    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }

    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("✓ %s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}

void cmd_list_checkpoints(Client* client, const char* filename) {
    char command[512];
    snprintf(command, sizeof(command), "LISTCHECKPOINTS %s", filename);

    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));

    if (bytes < 0) {
        printf("✗ Failed to send command\n");
        return;
    }

    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("\n%s\n", message);
        } else {
            printf("✗ Error: %s\n", message);
        }
    } else {
        printf("✗ Invalid response\n");
    }
}
