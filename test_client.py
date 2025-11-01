#!/usr/bin/env python3
"""
Test Client for Name Server
Demonstrates how to interact with the LangOS Name Server
"""

import socket
import sys

class NameServerClient:
    def __init__(self, host='localhost', port=8080):
        self.host = host
        self.port = port
        self.socket = None
        self.client_id = None
        self.username = None
        
    def connect(self, username, nm_port=7001, ss_port=7002):
        """Connect to the Name Server and register"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.host, self.port))
            
            # Register
            self.username = username
            register_msg = f"REGISTER_CLIENT {username} {nm_port} {ss_port}\n"
            self.socket.send(register_msg.encode())
            
            # Receive response
            response = self.socket.recv(4096).decode()
            error_code, message = response.split(':', 1)
            
            if error_code == '0':
                print(f"✓ Connected: {message.strip()}")
                return True
            else:
                print(f"✗ Connection failed: {message.strip()}")
                return False
                
        except Exception as e:
            print(f"✗ Connection error: {e}")
            return False
    
    def send_command(self, command):
        """Send a command to the Name Server"""
        try:
            self.socket.send(f"{command}\n".encode())
            response = self.socket.recv(16384).decode()
            
            error_code, message = response.split(':', 1)
            return int(error_code), message.strip()
            
        except Exception as e:
            print(f"✗ Error sending command: {e}")
            return 99, str(e)
    
    def view_files(self, flags=''):
        """View files"""
        cmd = f"VIEW {flags}".strip()
        error, message = self.send_command(cmd)
        
        if error == 0:
            print("\n" + message)
        else:
            print(f"✗ Error: {message}")
    
    def create_file(self, filename):
        """Create a new file"""
        error, message = self.send_command(f"CREATE {filename}")
        
        if error == 0:
            print(f"✓ {message}")
        else:
            print(f"✗ Error: {message}")
    
    def delete_file(self, filename):
        """Delete a file"""
        error, message = self.send_command(f"DELETE {filename}")
        
        if error == 0:
            print(f"✓ {message}")
        else:
            print(f"✗ Error: {message}")
    
    def file_info(self, filename):
        """Get file information"""
        error, message = self.send_command(f"INFO {filename}")
        
        if error == 0:
            print("\n" + message)
        else:
            print(f"✗ Error: {message}")
    
    def list_users(self):
        """List all users"""
        error, message = self.send_command("LIST")
        
        if error == 0:
            print("\n" + message)
        else:
            print(f"✗ Error: {message}")
    
    def add_access(self, filename, target_user, access_type='R'):
        """Grant access to a user"""
        flag = '-W' if access_type.upper() == 'W' else '-R'
        error, message = self.send_command(f"ADDACCESS {flag} {filename} {target_user}")
        
        if error == 0:
            print(f"✓ {message}")
        else:
            print(f"✗ Error: {message}")
    
    def remove_access(self, filename, target_user):
        """Revoke access from a user"""
        error, message = self.send_command(f"REMACCESS {filename} {target_user}")
        
        if error == 0:
            print(f"✓ {message}")
        else:
            print(f"✗ Error: {message}")
    
    def get_ss_info_for_read(self, filename):
        """Get Storage Server info for reading"""
        error, message = self.send_command(f"READ {filename}")
        
        if error == 0 and message.startswith("SS_INFO"):
            parts = message.split()
            if len(parts) >= 3:
                ss_ip = parts[1]
                ss_port = int(parts[2])
                print(f"✓ Storage Server: {ss_ip}:{ss_port}")
                return ss_ip, ss_port
        else:
            print(f"✗ Error: {message}")
        
        return None, None
    
    def get_ss_info_for_write(self, filename, sentence_num):
        """Get Storage Server info for writing"""
        error, message = self.send_command(f"WRITE {filename} {sentence_num}")
        
        if error == 0 and message.startswith("SS_INFO"):
            parts = message.split()
            if len(parts) >= 3:
                ss_ip = parts[1]
                ss_port = int(parts[2])
                print(f"✓ Storage Server: {ss_ip}:{ss_port}")
                return ss_ip, ss_port
        else:
            print(f"✗ Error: {message}")
        
        return None, None
    
    def get_ss_info_for_stream(self, filename):
        """Get Storage Server info for streaming"""
        error, message = self.send_command(f"STREAM {filename}")
        
        if error == 0 and message.startswith("SS_INFO"):
            parts = message.split()
            if len(parts) >= 3:
                ss_ip = parts[1]
                ss_port = int(parts[2])
                print(f"✓ Storage Server: {ss_ip}:{ss_port}")
                return ss_ip, ss_port
        else:
            print(f"✗ Error: {message}")
        
        return None, None
    
    def exec_file(self, filename):
        """Execute file as shell script"""
        error, message = self.send_command(f"EXEC {filename}")
        
        if error == 0:
            print("\n" + message)
        else:
            print(f"✗ Error: {message}")
    
    def undo_file(self, filename):
        """Undo last change to file"""
        error, message = self.send_command(f"UNDO {filename}")
        
        if error == 0:
            print(f"✓ {message}")
        else:
            print(f"✗ Error: {message}")
    
    def disconnect(self):
        """Disconnect from Name Server"""
        if self.socket:
            try:
                self.send_command("QUIT")
                self.socket.close()
                print("✓ Disconnected")
            except:
                pass

def interactive_mode(client):
    """Interactive command-line interface"""
    print("\nLangOS Name Server Test Client")
    print("=" * 50)
    print("Commands:")
    print("  view [flags]              - List files (-a for all, -l for detailed)")
    print("  create <file>             - Create a file")
    print("  delete <file>             - Delete a file")
    print("  info <file>               - Get file information")
    print("  read <file>               - Get SS info for reading")
    print("  write <file> <sent#>      - Get SS info for writing")
    print("  stream <file>             - Get SS info for streaming")
    print("  exec <file>               - Execute file as script")
    print("  undo <file>               - Undo last change")
    print("  addaccess <R|W> <file> <user> - Grant access")
    print("  remaccess <file> <user>   - Revoke access")
    print("  users                     - List all users")
    print("  quit                      - Disconnect")
    print("=" * 50)
    
    while True:
        try:
            cmd = input(f"\n{client.username}> ").strip()
            
            if not cmd:
                continue
            
            parts = cmd.split()
            command = parts[0].lower()
            
            if command == 'quit' or command == 'exit':
                break
            elif command == 'view':
                flags = parts[1] if len(parts) > 1 else ''
                client.view_files(flags)
            elif command == 'create':
                if len(parts) < 2:
                    print("Usage: create <filename>")
                else:
                    client.create_file(parts[1])
            elif command == 'delete':
                if len(parts) < 2:
                    print("Usage: delete <filename>")
                else:
                    client.delete_file(parts[1])
            elif command == 'info':
                if len(parts) < 2:
                    print("Usage: info <filename>")
                else:
                    client.file_info(parts[1])
            elif command == 'read':
                if len(parts) < 2:
                    print("Usage: read <filename>")
                else:
                    client.get_ss_info_for_read(parts[1])
            elif command == 'write':
                if len(parts) < 3:
                    print("Usage: write <filename> <sentence_number>")
                else:
                    client.get_ss_info_for_write(parts[1], parts[2])
            elif command == 'stream':
                if len(parts) < 2:
                    print("Usage: stream <filename>")
                else:
                    client.get_ss_info_for_stream(parts[1])
            elif command == 'exec':
                if len(parts) < 2:
                    print("Usage: exec <filename>")
                else:
                    client.exec_file(parts[1])
            elif command == 'undo':
                if len(parts) < 2:
                    print("Usage: undo <filename>")
                else:
                    client.undo_file(parts[1])
            elif command == 'addaccess':
                if len(parts) < 4:
                    print("Usage: addaccess <R|W> <filename> <username>")
                else:
                    client.add_access(parts[2], parts[3], parts[1])
            elif command == 'remaccess':
                if len(parts) < 3:
                    print("Usage: remaccess <filename> <username>")
                else:
                    client.remove_access(parts[1], parts[2])
            elif command == 'users':
                client.list_users()
            else:
                print(f"Unknown command: {command}")
                
        except KeyboardInterrupt:
            print("\nUse 'quit' to exit")
        except Exception as e:
            print(f"Error: {e}")

def run_test_suite(client):
    """Run automated test suite"""
    print("\n" + "=" * 50)
    print("Running Test Suite")
    print("=" * 50)
    
    # Test 1: Create file
    print("\n[Test 1] Creating test file...")
    client.create_file("test_file.txt")
    
    # Test 2: View files
    print("\n[Test 2] Viewing all files...")
    client.view_files("-a")
    
    # Test 3: Get file info
    print("\n[Test 3] Getting file info...")
    client.file_info("test_file.txt")
    
    # Test 4: List users
    print("\n[Test 4] Listing users...")
    client.list_users()
    
    # Test 5: Add access (will fail if no other user)
    print("\n[Test 5] Adding access to 'testuser'...")
    client.add_access("test_file.txt", "testuser", "R")
    
    # Test 6: Get SS info for read
    print("\n[Test 6] Getting SS info for reading...")
    client.get_ss_info_for_read("test_file.txt")
    
    # Test 7: Delete file
    print("\n[Test 7] Deleting test file...")
    client.delete_file("test_file.txt")
    
    print("\n" + "=" * 50)
    print("Test Suite Complete")
    print("=" * 50)

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 test_client.py <username> [host] [port]")
        print("Example: python3 test_client.py alice localhost 8080")
        sys.exit(1)
    
    username = sys.argv[1]
    host = sys.argv[2] if len(sys.argv) > 2 else 'localhost'
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 8080
    
    client = NameServerClient(host, port)
    
    if client.connect(username):
        if '--test' in sys.argv:
            run_test_suite(client)
        else:
            interactive_mode(client)
        
        client.disconnect()
    else:
        print("Failed to connect to Name Server")
        sys.exit(1)

if __name__ == '__main__':
    main()
