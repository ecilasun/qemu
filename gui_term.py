import tkinter as tk
from tkinter import font
import socket
import threading
import sys
import os
import queue
import re

class TerminalGUI:
    def __init__(self, master):
        self.master = master
        master.title("QEMU Terminal")
        master.geometry("800x600")

        # Queue for thread-safe GUI updates
        self.queue = queue.Queue()

        # Configure font
        self.custom_font = font.Font(family="Segoe UI", size=11)

        # Text widget with black background
        self.text_area = tk.Text(master, bg="#1e1e1e", fg="#cccccc", 
                                 font=self.custom_font, insertbackground="white",
                                 state="disabled")
        self.text_area.pack(expand=True, fill="both")

        # Define tags for ANSI colors
        self.colors = {
            # Foreground
            '30': '#000000', '31': '#cd3131', '32': '#0dbc79', '33': '#e5e510',
            '34': '#2472c8', '35': '#bc3fbc', '36': '#11a8cd', '37': '#e5e5e5',
            '90': '#666666', '91': '#f14c4c', '92': '#23d18b', '93': '#f5f543',
            '94': '#3b8eea', '95': '#d670d6', '96': '#29b8db', '97': '#ffffff',
            # Background
            '40': '#000000', '41': '#cd3131', '42': '#0dbc79', '43': '#e5e510',
            '44': '#2472c8', '45': '#bc3fbc', '46': '#11a8cd', '47': '#e5e5e5',
            '100': '#666666', '101': '#f14c4c', '102': '#23d18b', '103': '#f5f543',
            '104': '#3b8eea', '105': '#d670d6', '106': '#29b8db', '107': '#ffffff',
        }
        for code, color in self.colors.items():
            if int(code) >= 40: # Background
                self.text_area.tag_config(f"bg_{code}", background=color)
            else: # Foreground
                self.text_area.tag_config(f"fg_{code}", foreground=color)

        self.current_tags = []
        self.ansi_buffer = ""

        # Bind key events to the main window so they work even if text area is disabled
        master.bind("<Key>", self.on_key)
        master.bind("<Up>", lambda e: self.send_key(b'\x1b[A'))
        master.bind("<Down>", lambda e: self.send_key(b'\x1b[B'))
        master.bind("<Right>", lambda e: self.send_key(b'\x1b[C'))
        master.bind("<Left>", lambda e: self.send_key(b'\x1b[D'))
        
        self.socket = None
        self.connect()

        # Start checking queue
        self.master.after(100, self.process_queue)

    def connect(self):
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect(('localhost', 5555))
            self.append_text("Connected to QEMU.\n")
            
            # Start receiver thread
            self.thread = threading.Thread(target=self.receive_loop, daemon=True)
            self.thread.start()
        except Exception as e:
            self.append_text(f"Connection failed: {e}\n")

    def receive_loop(self):
        import codecs
        decoder = codecs.getincrementaldecoder("utf-8")(errors='replace')
        text_buffer = ""
        
        # Prefixes of keywords E0B[0-3] and <E0B[0-3]>
        prefixes = {
            "E", "E0", "E0B", 
            "<", "<E", "<E0", "<E0B", 
            "<E0B0", "<E0B1", "<E0B2", "<E0B3"
        }
        
        while True:
            try:
                data = self.socket.recv(1024)
                if not data:
                    break
                
                # Decode with replacement for errors
                text = decoder.decode(data, final=False)
                text_buffer += text
                
                # Process buffer for replacements
                
                # Map Powerline Private Use Area characters to standard Unicode
                text_buffer = text_buffer.replace('\ue0b0', '\u25b6') 
                text_buffer = text_buffer.replace('\ue0b1', '\u276f')
                text_buffer = text_buffer.replace('\ue0b2', '\u25c0')
                text_buffer = text_buffer.replace('\ue0b3', '\u276e')

                # Also handle literal hex strings if the guest is sending them
                text_buffer = text_buffer.replace('E0B0', '\u25b6')
                text_buffer = text_buffer.replace('E0B1', '\u276f')
                text_buffer = text_buffer.replace('E0B2', '\u25c0')
                text_buffer = text_buffer.replace('E0B3', '\u276e')
                
                # Handle <E0B0> format
                text_buffer = text_buffer.replace('<E0B0>', '\u25b6')
                text_buffer = text_buffer.replace('<E0B1>', '\u276f')
                text_buffer = text_buffer.replace('<E0B2>', '\u25c0')
                text_buffer = text_buffer.replace('<E0B3>', '\u276e')

                # Determine how much to keep in buffer (if end looks like a partial keyword)
                keep_len = 0
                # Max partial length is 5 (e.g. "<E0B0")
                for i in range(5, 0, -1):
                    if i > len(text_buffer): continue
                    suffix = text_buffer[-i:]
                    if suffix in prefixes:
                        keep_len = i
                        break

                if keep_len > 0:
                    to_send = text_buffer[:-keep_len]
                    text_buffer = text_buffer[-keep_len:]
                    if to_send:
                        self.queue.put(to_send)
                else:
                    self.queue.put(text_buffer)
                    text_buffer = ""
                
            except Exception:
                break
        
        # Flush remaining buffer
        if text_buffer:
            self.queue.put(text_buffer)
        self.queue.put(None) # Signal disconnect

    def process_queue(self):
        try:
            while True:
                text = self.queue.get_nowait()
                if text is None:
                    self.append_text("\nDisconnected.")
                else:
                    self.append_text(text)
        except queue.Empty:
            pass
        self.master.after(50, self.process_queue)

    def append_text(self, text):
        self.text_area.config(state="normal")
        
        # Combine with any leftover buffer from previous chunk
        full_text = self.ansi_buffer + text
        self.ansi_buffer = ""

        # Split by ANSI escape codes
        # Regex matches \x1b[...m
        parts = re.split(r'(\x1b\[[0-9;]*m)', full_text)
        
        # If the last part looks like an incomplete ANSI code, buffer it
        if parts[-1].startswith('\x1b') and not parts[-1].endswith('m'):
            self.ansi_buffer = parts.pop()

        for part in parts:
            if part.startswith('\x1b['):
                # Parse code
                code_seq = part[2:-1]
                if code_seq == '0' or code_seq == '':
                    self.current_tags = []
                else:
                    codes = code_seq.split(';')
                    for c in codes:
                        if c in self.colors:
                            if int(c) >= 40: # Background
                                # Remove existing bg tags
                                self.current_tags = [t for t in self.current_tags if not t.startswith('bg_')]
                                self.current_tags.append(f"bg_{c}")
                            else: # Foreground
                                # Remove existing fg tags
                                self.current_tags = [t for t in self.current_tags if not t.startswith('fg_')]
                                self.current_tags.append(f"fg_{c}")
            else:
                if part:
                    self.text_area.insert(tk.END, part, tuple(self.current_tags))
        
        self.text_area.see(tk.END)
        self.text_area.config(state="disabled")

    def on_key(self, event):
        # Ignore arrow keys (handled by specific bindings)
        if event.keysym in ('Up', 'Down', 'Left', 'Right'):
            return "break"

        if event.char:
            try:
                self.send_key(event.char.encode('utf-8'))
            except:
                pass
        return "break"

    def send_key(self, data):
        if self.socket:
            try:
                self.socket.sendall(data)
            except:
                pass
        return "break"

if __name__ == "__main__":
    root = tk.Tk()
    gui = TerminalGUI(root)
    root.mainloop()
