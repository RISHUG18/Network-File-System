# Makefile for Name Server

CC = gcc
CFLAGS = -Wall -Wextra -pthread -O2 -g
LDFLAGS = -pthread

# Target executable
TARGET = name_server

# Source files
SRCS = name_server.c name_server_ops.c name_server_main.c

# Object files
OBJS = $(SRCS:.c=.o)

# Header files
HEADERS = name_server.h

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "Name Server built successfully!"

# Compile source files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJS) $(TARGET) nm_log.txt
	@echo "Cleaned build artifacts"

# Run the name server (default port 8080)
run: $(TARGET)
	./$(TARGET) 8080

# Run with custom port
run-port: $(TARGET)
	@echo "Usage: make run-port PORT=<port_number>"
	./$(TARGET) $(PORT)

# Debug build
debug: CFLAGS += -DDEBUG -g3
debug: clean $(TARGET)

# Install (optional)
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/
	@echo "Name Server installed to /usr/local/bin/"

# Uninstall
uninstall:
	rm -f /usr/local/bin/$(TARGET)
	@echo "Name Server uninstalled"

# Help
help:
	@echo "Available targets:"
	@echo "  all       - Build the name server (default)"
	@echo "  clean     - Remove build artifacts"
	@echo "  run       - Build and run on default port 8080"
	@echo "  run-port  - Build and run on custom port (usage: make run-port PORT=9000)"
	@echo "  debug     - Build with debug symbols"
	@echo "  install   - Install to /usr/local/bin"
	@echo "  uninstall - Remove from /usr/local/bin"
	@echo "  help      - Show this help message"

.PHONY: all clean run run-port debug install uninstall help
