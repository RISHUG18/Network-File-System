#include "client.h"

// ==================== CORE CLIENT FUNCTIONS ====================

Client* create_client(const char* username, const char* nm_host, int nm_port) {
    Client* client = (Client*)malloc(sizeof(Client));
    if (!client) {
        perror("Failed to allocate client");
        return NULL;
    }
    
    strncpy(client->username, username, MAX_USERNAME - 1);
    client->username[MAX_USERNAME - 1] = '\0';
    
    strncpy(client->nm_host, nm_host, 63);
    client->nm_host[63] = '\0';
    
    client->nm_port = nm_port;
    client->client_nm_port = 7001;  // Default port for NM communication
    client->client_ss_port = 7002;  // Default port for SS communication
    client->nm_socket = -1;
    client->connected = false;
    
    return client;
}

bool connect_to_nm(Client* client) {
    // Create socket
    client->nm_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client->nm_socket < 0) {
        perror("Failed to create socket");
        return false;
    }
    
    // Connect to Name Server
    struct sockaddr_in nm_addr;
    memset(&nm_addr, 0, sizeof(nm_addr));
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(client->nm_port);
    
    if (inet_pton(AF_INET, client->nm_host, &nm_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(client->nm_socket);
        client->nm_socket = -1;
        return false;
    }
    
    if (connect(client->nm_socket, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection failed");
        close(client->nm_socket);
        client->nm_socket = -1;
        return false;
    }
    
    // Register with Name Server
    char register_cmd[256];
    snprintf(register_cmd, sizeof(register_cmd), "REGISTER_CLIENT %s %d %d\n",
             client->username, client->client_nm_port, client->client_ss_port);
    
    if (send(client->nm_socket, register_cmd, strlen(register_cmd), 0) < 0) {
        perror("Failed to send registration");
        close(client->nm_socket);
        client->nm_socket = -1;
        return false;
    }
    
    // Receive registration response
    char response[BUFFER_SIZE];
    ssize_t bytes = recv(client->nm_socket, response, sizeof(response) - 1, 0);
    if (bytes <= 0) {
        perror("Failed to receive registration response");
        close(client->nm_socket);
        client->nm_socket = -1;
        return false;
    }
    response[bytes] = '\0';
    
    // Parse response
    int error_code;
    char message[BUFFER_SIZE];
    if (parse_nm_response(response, &error_code, message)) {
        if (error_code == 0) {
            printf("✓ Connected: %s\n", message);
            client->connected = true;
            return true;
        } else {
            printf("✗ Registration failed: %s\n", message);
        }
    } else {
        printf("✗ Invalid response from Name Server\n");
    }
    
    close(client->nm_socket);
    client->nm_socket = -1;
    return false;
}

void disconnect_client(Client* client) {
    if (client->connected && client->nm_socket >= 0) {
        // Send QUIT command
        const char* quit_cmd = "QUIT\n";
        send(client->nm_socket, quit_cmd, strlen(quit_cmd), 0);
        
        close(client->nm_socket);
        client->nm_socket = -1;
        client->connected = false;
        printf("✓ Disconnected from Name Server\n");
    }
}

void destroy_client(Client* client) {
    if (client) {
        disconnect_client(client);
        free(client);
    }
}

// ==================== COMMUNICATION FUNCTIONS ====================

int send_nm_command(Client* client, const char* command, char* response, size_t response_size) {
    if (!client->connected || client->nm_socket < 0) {
        snprintf(response, response_size, "99:Not connected to Name Server");
        return -1;
    }
    
    // Send command with newline
    char cmd_with_newline[BUFFER_SIZE];
    snprintf(cmd_with_newline, sizeof(cmd_with_newline), "%s\n", command);
    
    if (send(client->nm_socket, cmd_with_newline, strlen(cmd_with_newline), 0) < 0) {
        snprintf(response, response_size, "99:Failed to send command");
        return -1;
    }
    
    // Receive response
    ssize_t bytes = recv(client->nm_socket, response, response_size - 1, 0);
    if (bytes <= 0) {
        snprintf(response, response_size, "99:Failed to receive response");
        return -1;
    }
    response[bytes] = '\0';
    
    return bytes;
}

bool parse_nm_response(const char* response, int* error_code, char* message) {
    // Response format: "error_code:message"
    const char* colon = strchr(response, ':');
    if (!colon) {
        return false;
    }
    
    *error_code = atoi(response);
    
    // Copy message (skip colon and any leading whitespace)
    const char* msg_start = colon + 1;
    while (*msg_start == ' ' || *msg_start == '\t') {
        msg_start++;
    }
    
    strcpy(message, msg_start);
    
    // Remove trailing newline
    size_t len = strlen(message);
    if (len > 0 && message[len - 1] == '\n') {
        message[len - 1] = '\0';
    }
    
    return true;
}

int connect_to_ss(const char* ss_ip, int ss_port) {
    int ss_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ss_socket < 0) {
        perror("Failed to create SS socket");
        return -1;
    }
    
    struct sockaddr_in ss_addr;
    memset(&ss_addr, 0, sizeof(ss_addr));
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(ss_port);
    
    if (inet_pton(AF_INET, ss_ip, &ss_addr.sin_addr) <= 0) {
        perror("Invalid SS address");
        close(ss_socket);
        return -1;
    }
    
    if (connect(ss_socket, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
        perror("Failed to connect to Storage Server");
        close(ss_socket);
        return -1;
    }
    
    return ss_socket;
}

int send_ss_command(int ss_socket, const char* command, char* response, size_t response_size) {
    // Send command with newline
    char cmd_with_newline[BUFFER_SIZE];
    snprintf(cmd_with_newline, sizeof(cmd_with_newline), "%s\n", command);
    
    if (send(ss_socket, cmd_with_newline, strlen(cmd_with_newline), 0) < 0) {
        snprintf(response, response_size, "ERROR:Failed to send command");
        return -1;
    }
    
    // Receive response
    ssize_t bytes = recv(ss_socket, response, response_size - 1, 0);
    if (bytes <= 0) {
        snprintf(response, response_size, "ERROR:Failed to receive response");
        return -1;
    }
    response[bytes] = '\0';
    
    return bytes;
}

bool get_ss_info(Client* client, const char* command, char* ss_ip, int* ss_port) {
    char response[BUFFER_SIZE];
    int bytes = send_nm_command(client, command, response, sizeof(response));
    
    if (bytes < 0) {
        return false;
    }
    
    int error_code;
    char message[BUFFER_SIZE];
    if (!parse_nm_response(response, &error_code, message)) {
        printf("✗ Invalid response from Name Server\n");
        return false;
    }
    
    if (error_code != 0) {
        printf("✗ Error: %s\n", message);
        return false;
    }
    
    // Parse "SS_INFO <ip> <port>"
    if (strncmp(message, "SS_INFO ", 8) == 0) {
        if (sscanf(message + 8, "%s %d", ss_ip, ss_port) == 2) {
            printf("✓ Storage Server: %s:%d\n", ss_ip, *ss_port);
            return true;
        }
    }
    
    printf("✗ Invalid SS_INFO response: %s\n", message);
    return false;
}
