#!/usr/bin/env python3
"""
Test helper for comprehensive tests with diagnostic output
"""
import socket
import sys
import json

def send_command(host, port, username, nm_port, ss_port, command, timeout=5):
    """
    Send a command to the server and return diagnostic info
    Returns: dict with 'success', 'response', 'error' keys
    """
    result = {
        'success': False,
        'response': '',
        'error': '',
        'command': command
    }
    
    try:
        s = socket.socket()
        s.settimeout(timeout)
        s.connect((host, port))
        
        # Register
        reg_cmd = f'REGISTER_CLIENT {username} {nm_port} {ss_port}\n'.encode()
        s.send(reg_cmd)
        reg_resp = s.recv(4096).decode().strip()
        result['register'] = reg_resp
        
        # Send command
        s.send(command.encode() if isinstance(command, str) else command)
        if not command.endswith(b'\n') and not command.endswith('\n'):
            s.send(b'\n')
        
        response = s.recv(4096).decode().strip()
        result['response'] = response
        result['success'] = True
        
        s.close()
        
    except Exception as e:
        result['error'] = str(e)
        result['success'] = False
    
    return result

def get_ss_info(response):
    """Parse SS_INFO from response"""
    if 'SS_INFO' not in response and 'ss_info' not in response.lower():
        return None, None
    
    parts = response.split()
    ss_ip = '127.0.0.1'
    ss_port = 9002
    
    for i, p in enumerate(parts):
        if p.count('.') == 3:
            ss_ip = p
            if i+1 < len(parts):
                try:
                    ss_port = int(parts[i+1])
                except:
                    pass
            break
    
    return ss_ip, ss_port

def read_from_ss(ss_ip, ss_port, filename, timeout=5):
    """Read file content from storage server"""
    try:
        ss = socket.socket()
        ss.settimeout(timeout)
        ss.connect((ss_ip, ss_port))
        ss.send(f'READ {filename}\n'.encode())
        content = ss.recv(8192).decode()
        ss.close()
        return {'success': True, 'content': content, 'error': ''}
    except Exception as e:
        return {'success': False, 'content': '', 'error': str(e)}

if __name__ == '__main__':
    # Test mode
    if len(sys.argv) > 1:
        if sys.argv[1] == 'test_create':
            result = send_command('localhost', 8080, 'testuser', 9999, 9998, 'CREATE testfile.txt\n')
            print(json.dumps(result))
            sys.exit(0 if result['success'] and ('success' in result['response'].lower() or '0:' in result['response']) else 1)
