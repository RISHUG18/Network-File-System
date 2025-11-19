#ifndef CLIENT_H
#define CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>

#define BUFFER_SIZE 16384
#define MAX_FILENAME 256
#define MAX_USERNAME 64
#define MAX_CONTENT 1048576  // 1MB

// Client structure
typedef struct {
    char username[MAX_USERNAME];
    char nm_host[64];
    int nm_port;
    int client_nm_port;
    int client_ss_port;
    int nm_socket;
    bool connected;
} Client;

// Function declarations

// Core client functions
Client* create_client(const char* username, const char* nm_host, int nm_port);
bool connect_to_nm(Client* client);
void disconnect_client(Client* client);
void destroy_client(Client* client);

// Name Server communication
int send_nm_command(Client* client, const char* command, char* response, size_t response_size);
bool parse_nm_response(const char* response, int* error_code, char* message);

// Storage Server operations
int connect_to_ss(const char* ss_ip, int ss_port);
int send_ss_command(int ss_socket, const char* command, char* response, size_t response_size);

// Name Server operations (through NM)
void cmd_view_files(Client* client, const char* flags);
void cmd_create_file(Client* client, const char* filename);
void cmd_delete_file(Client* client, const char* filename);
void cmd_file_info(Client* client, const char* filename);
void cmd_exec_file(Client* client, const char* filename);
void cmd_undo_file(Client* client, const char* filename);
void cmd_list_users(Client* client);
void cmd_add_access(Client* client, const char* filename, const char* target_user, char access_type);
void cmd_remove_access(Client* client, const char* filename, const char* target_user);
void cmd_request_access(Client* client, const char* filename, char access_type);
void cmd_list_requests(Client* client, const char* filename);
void cmd_process_request(Client* client, const char* filename, const char* target_user, bool approve);
void cmd_checkpoint(Client* client, const char* filename, const char* tag);
void cmd_view_checkpoint(Client* client, const char* filename, const char* tag);
void cmd_revert_checkpoint(Client* client, const char* filename, const char* tag);
void cmd_list_checkpoints(Client* client, const char* filename);

// Direct Storage Server operations
void cmd_read_file(Client* client, const char* filename);
void cmd_write_file(Client* client, const char* filename, int sentence_num);
void cmd_stream_file(Client* client, const char* filename);

// Helper functions
bool get_ss_info(Client* client, const char* command, char* ss_ip, int* ss_port);
void print_help();
void interactive_mode(Client* client);

#endif // CLIENT_H
