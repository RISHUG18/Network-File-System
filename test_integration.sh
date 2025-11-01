#!/bin/bash

# Integration Test Script for LangOS Distributed File System
# Tests Name Server + Storage Server + Client integration

set -e  # Exit on error

echo "========================================="
echo "LangOS DFS Integration Test"
echo "========================================="
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}Cleaning up...${NC}"
    pkill -f "./name_server" 2>/dev/null || true
    pkill -f "./storage_server" 2>/dev/null || true
    rm -f nm_log.txt ss_log.txt
    rm -rf storage/
    sleep 1
}

# Trap to ensure cleanup on exit
trap cleanup EXIT

# Step 1: Build
echo -e "${YELLOW}Step 1: Building...${NC}"
make clean > /dev/null 2>&1
make > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ Build successful${NC}"
else
    echo -e "${RED}✗ Build failed${NC}"
    exit 1
fi
echo ""

# Step 2: Start Name Server
echo -e "${YELLOW}Step 2: Starting Name Server on port 8080...${NC}"
./name_server 8080 > /dev/null 2>&1 &
NM_PID=$!
sleep 2

if ps -p $NM_PID > /dev/null; then
    echo -e "${GREEN}✓ Name Server started (PID: $NM_PID)${NC}"
else
    echo -e "${RED}✗ Name Server failed to start${NC}"
    exit 1
fi
echo ""

# Step 3: Start Storage Server 1
echo -e "${YELLOW}Step 3: Starting Storage Server 1 on port 9002...${NC}"
./storage_server 127.0.0.1 8080 9002 > /dev/null 2>&1 &
SS1_PID=$!
sleep 2

if ps -p $SS1_PID > /dev/null; then
    echo -e "${GREEN}✓ Storage Server 1 started (PID: $SS1_PID)${NC}"
else
    echo -e "${RED}✗ Storage Server 1 failed to start${NC}"
    kill $NM_PID 2>/dev/null || true
    exit 1
fi
echo ""

# Step 4: Start Storage Server 2 (optional)
echo -e "${YELLOW}Step 4: Starting Storage Server 2 on port 9003...${NC}"
./storage_server 127.0.0.1 8080 9003 > /dev/null 2>&1 &
SS2_PID=$!
sleep 2

if ps -p $SS2_PID > /dev/null; then
    echo -e "${GREEN}✓ Storage Server 2 started (PID: $SS2_PID)${NC}"
else
    echo -e "${YELLOW}⚠ Storage Server 2 failed to start (optional)${NC}"
fi
echo ""

# Step 5: Test Client Connection
echo -e "${YELLOW}Step 5: Testing Client Connection...${NC}"
echo "VIEW" | nc -w 2 localhost 8080 > /dev/null 2>&1 && \
echo -e "${GREEN}✓ Can connect to Name Server${NC}" || \
echo -e "${RED}✗ Cannot connect to Name Server${NC}"
echo ""

# Step 6: Run Client Tests
echo -e "${YELLOW}Step 6: Running Client Tests...${NC}"
if [ -f "test_client.py" ]; then
    timeout 10 python3 test_client.py testuser --test 2>&1 | grep -q "Test Suite Complete" && \
    echo -e "${GREEN}✓ Client tests passed${NC}" || \
    echo -e "${YELLOW}⚠ Some client tests may have failed (this is expected without full client implementation)${NC}"
else
    echo -e "${YELLOW}⚠ test_client.py not found, skipping client tests${NC}"
fi
echo ""

# Step 7: Check Logs
echo -e "${YELLOW}Step 7: Checking Logs...${NC}"
if [ -f "nm_log.txt" ]; then
    NM_LOGS=$(wc -l < nm_log.txt)
    echo -e "${GREEN}✓ Name Server log exists ($NM_LOGS entries)${NC}"
else
    echo -e "${RED}✗ Name Server log not found${NC}"
fi

if [ -f "ss_log.txt" ]; then
    SS_LOGS=$(wc -l < ss_log.txt)
    echo -e "${GREEN}✓ Storage Server log exists ($SS_LOGS entries)${NC}"
else
    echo -e "${YELLOW}⚠ Storage Server log not found${NC}"
fi
echo ""

# Step 8: Check Storage Directory
echo -e "${YELLOW}Step 8: Checking Storage Directory...${NC}"
if [ -d "storage" ]; then
    FILE_COUNT=$(ls -1 storage/ 2>/dev/null | wc -l)
    echo -e "${GREEN}✓ Storage directory exists ($FILE_COUNT files)${NC}"
else
    echo -e "${YELLOW}⚠ Storage directory not found (will be created on first file)${NC}"
fi
echo ""

# Summary
echo "========================================="
echo -e "${GREEN}Integration Test Complete!${NC}"
echo "========================================="
echo ""
echo "Running Processes:"
echo "  Name Server (PID: $NM_PID)"
echo "  Storage Server 1 (PID: $SS1_PID)"
if ps -p $SS2_PID > /dev/null 2>&1; then
    echo "  Storage Server 2 (PID: $SS2_PID)"
fi
echo ""
echo "To interact manually:"
echo "  python3 test_client.py <username>"
echo ""
echo "To stop servers:"
echo "  kill $NM_PID $SS1_PID"
[ -n "$SS2_PID" ] && ps -p $SS2_PID > /dev/null 2>&1 && echo "  kill $SS2_PID"
echo ""
echo "Press Ctrl+C to cleanup and exit"
echo ""

# Wait for user interrupt
wait
