# Makefile for LangOS Distributed File System

CC = gcc
CFLAGS = -Wall -Wextra -pthread -O2 -g
LDFLAGS = -pthread

# Target executables
NM_TARGET = name_server
SS_TARGET = storage_server
CLIENT_TARGET = client

# Source files
NM_SRCS = name_server.c name_server_ops.c name_server_main.c
SS_SRCS = storage_server.c storage_server_ops.c storage_server_main.c
CLIENT_SRCS = client_core.c client_nm_ops.c client_ss_ops.c client.c

# Object files
NM_OBJS = $(NM_SRCS:.c=.o)
SS_OBJS = $(SS_SRCS:.c=.o)
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

# Header files
NM_HEADERS = name_server.h
SS_HEADERS = storage_server.h
CLIENT_HEADERS = client.h

# Default target - build both
all: $(NM_TARGET) $(SS_TARGET) $(CLIENT_TARGET)

# Build the Name Server
$(NM_TARGET): $(NM_OBJS)
	$(CC) $(NM_OBJS) -o $(NM_TARGET) $(LDFLAGS)
	@echo "Name Server built successfully!"

# Build the Storage Server
$(SS_TARGET): $(SS_OBJS)
	$(CC) $(SS_OBJS) -o $(SS_TARGET) $(LDFLAGS)
	@echo "Storage Server built successfully!"

# Build the Client
$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CC) $(CLIENT_OBJS) -o $(CLIENT_TARGET) $(LDFLAGS)
	@echo "Client built successfully!"

# Compile Name Server source files
name_server.o: name_server.c $(NM_HEADERS)
	$(CC) $(CFLAGS) -c name_server.c -o name_server.o

name_server_ops.o: name_server_ops.c $(NM_HEADERS)
	$(CC) $(CFLAGS) -c name_server_ops.c -o name_server_ops.o

name_server_main.o: name_server_main.c $(NM_HEADERS)
	$(CC) $(CFLAGS) -c name_server_main.c -o name_server_main.o

# Compile Storage Server source files
storage_server.o: storage_server.c $(SS_HEADERS)
	$(CC) $(CFLAGS) -c storage_server.c -o storage_server.o

storage_server_ops.o: storage_server_ops.c $(SS_HEADERS)
	$(CC) $(CFLAGS) -c storage_server_ops.c -o storage_server_ops.o

storage_server_main.o: storage_server_main.c $(SS_HEADERS)
	$(CC) $(CFLAGS) -c storage_server_main.c -o storage_server_main.o

# Compile Client source files
client_core.o: client_core.c $(CLIENT_HEADERS)
	$(CC) $(CFLAGS) -c client_core.c -o client_core.o

client_nm_ops.o: client_nm_ops.c $(CLIENT_HEADERS)
	$(CC) $(CFLAGS) -c client_nm_ops.c -o client_nm_ops.o

client_ss_ops.o: client_ss_ops.c $(CLIENT_HEADERS)
	$(CC) $(CFLAGS) -c client_ss_ops.c -o client_ss_ops.o

client.o: client.c $(CLIENT_HEADERS)
	$(CC) $(CFLAGS) -c client.c -o client.o

# Clean build artifacts
clean:
	rm -f $(NM_OBJS) $(SS_OBJS) $(CLIENT_OBJS) $(NM_TARGET) $(SS_TARGET) $(CLIENT_TARGET) nm_log.txt ss_log.txt nm_users.dat
	rm -rf storage/
	@echo "Cleaned build artifacts"

# Run the name server (default port 8080)
run-nm: $(NM_TARGET)
	./$(NM_TARGET) 8080

# Run the storage server
run-ss: $(SS_TARGET)
	@echo "Usage: make run-ss NM_IP=127.0.0.1 NM_PORT=8080 CLIENT_PORT=9002"
	./$(SS_TARGET) $(NM_IP) $(NM_PORT) $(CLIENT_PORT)

# Run the client
run-client: $(CLIENT_TARGET)
	@echo "Usage: make run-client USERNAME=alice NM_IP=localhost NM_PORT=8080"
	./$(CLIENT_TARGET) $(USERNAME) $(NM_IP) $(NM_PORT)

# Debug build
debug: CFLAGS += -DDEBUG -g3
debug: clean all

# Help
help:
	@echo "Available targets:"
	@echo "  all           - Build Name Server, Storage Server, and Client (default)"
	@echo "  name_server   - Build only Name Server"
	@echo "  storage_server - Build only Storage Server"
	@echo "  client        - Build only Client"
	@echo "  clean         - Remove build artifacts"
	@echo "  run-nm        - Build and run Name Server on port 8080"
	@echo "  run-ss        - Build and run Storage Server (requires NM_IP, NM_PORT, CLIENT_PORT)"
	@echo "  run-client    - Build and run Client (requires USERNAME, NM_IP, NM_PORT)"
	@echo "  debug         - Build with debug symbols"
	@echo "  help          - Show this help message"

.PHONY: all clean run-nm run-ss run-client debug help
