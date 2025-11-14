#!/bin/bash
# Test script to verify locking of non-existent sentences

echo "=========================================="
echo "Testing Non-Existent Sentence Locking"
echo "=========================================="

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Cleanup
pkill -9 name_server 2>/dev/null
pkill -9 storage_server 2>/dev/null
sleep 1

# Start servers
echo -e "\n${GREEN}Starting Name Server...${NC}"
./name_server &
NS_PID=$!
sleep 2

echo -e "${GREEN}Starting Storage Server...${NC}"
./storage_server 127.0.0.1 8080 9002 &
SS_PID=$!
sleep 2

echo -e "\n${GREEN}Servers started successfully${NC}"
echo "Name Server PID: $NS_PID"
echo "Storage Server PID: $SS_PID"

# Test 1: Create a file and write to sentence 0
echo -e "\n=========================================="
echo "TEST 1: Write to sentence 0 (should exist)"
echo "=========================================="
echo "REGISTER testuser 10300 10400
CREATE test_file.txt
WRITE test_file.txt 0
QUIT" | timeout 5 ./client

# Test 2: Write to sentence 10 (doesn't exist yet)
echo -e "\n=========================================="
echo "TEST 2: Write to sentence 10 (non-existent)"
echo "=========================================="
echo "REGISTER testuser 10301 10401
WRITE test_file.txt 10
QUIT" | timeout 5 ./client

# Test 3: Write to sentence 50 (way beyond)
echo -e "\n=========================================="
echo "TEST 3: Write to sentence 50 (far beyond)"
echo "=========================================="
echo "REGISTER testuser 10302 10402
WRITE test_file.txt 50
QUIT" | timeout 5 ./client

# Test 4: Read the file to see if sentences were created
echo -e "\n=========================================="
echo "TEST 4: Read file to verify sentences"
echo "=========================================="
echo "REGISTER testuser 10303 10403
READ test_file.txt
QUIT" | timeout 5 ./client

# Test 5: INFO to see sentence count
echo -e "\n=========================================="
echo "TEST 5: Get file info"
echo "=========================================="
echo "REGISTER testuser 10304 10404
INFO test_file.txt
QUIT" | timeout 5 ./client

# Test 6: Concurrent writes to different non-existent sentences
echo -e "\n=========================================="
echo "TEST 6: Concurrent writes to sentences 100, 101, 102"
echo "=========================================="

(echo "REGISTER writer1 10305 10405
WRITE test_file.txt 100
QUIT" | timeout 5 ./client) &

(echo "REGISTER writer2 10306 10406
WRITE test_file.txt 101
QUIT" | timeout 5 ./client) &

(echo "REGISTER writer3 10307 10407
WRITE test_file.txt 102
QUIT" | timeout 5 ./client) &

wait

echo -e "\n${GREEN}All tests completed!${NC}"

# Cleanup
echo -e "\n${GREEN}Cleaning up...${NC}"
kill $NS_PID $SS_PID 2>/dev/null
sleep 1

echo -e "\n=========================================="
echo "Check the logs for detailed results:"
echo "  - nm_log.txt"
echo "  - ss_log.txt"
echo "=========================================="
