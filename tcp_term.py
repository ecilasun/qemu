import socket
import sys
import threading
import msvcrt
import os
import signal

# Prevent Ctrl+C from killing the script immediately
def signal_handler(sig, frame):
    pass

signal.signal(signal.SIGINT, signal_handler)

def receive_data(sock):
    while True:
        try:
            data = sock.recv(1024)
            if not data:
                break
            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
        except Exception:
            break
    # When socket closes, exit
    os._exit(0)

def main():
    host = 'localhost'
    port = 5555
    
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.connect((host, port))
    except ConnectionRefusedError:
        print(f"Could not connect to {host}:{port}. Make sure QEMU is running.")
        return

    print(f"Connected to {host}:{port}. Press Ctrl+] to exit.")

    # Start receiver thread
    t = threading.Thread(target=receive_data, args=(s,))
    t.daemon = True
    t.start()

    while True:
        try:
            ch = msvcrt.getch()
            
            # Handle special keys (arrows, etc)
            if ch in (b'\x00', b'\xe0'):
                ch2 = msvcrt.getch()
                # Basic Arrow Key Mapping for Linux
                if ch2 == b'H': # Up
                    s.sendall(b'\x1b[A')
                elif ch2 == b'P': # Down
                    s.sendall(b'\x1b[B')
                elif ch2 == b'M': # Right
                    s.sendall(b'\x1b[C')
                elif ch2 == b'K': # Left
                    s.sendall(b'\x1b[D')
                else:
                    # Pass through unknown extended keys? 
                    # Might confuse the guest, but better than nothing.
                    pass
                continue

            # Ctrl+] to exit
            if ch == b'\x1d':
                print("\nExiting...")
                break
            
            # Ctrl+C is \x03
            s.sendall(ch)
            
        except Exception as e:
            print(f"Error: {e}")
            break

    s.close()

if __name__ == "__main__":
    main()
