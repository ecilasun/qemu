import tkinter as tk
from tkinter import font
import socket
import threading
import sys
import os
import queue

class TerminalGUI:
    def __init__(self, master):
        self.master = master
        master.title("QEMU Terminal (fbcon emulation)")
        master.geometry("800x600")

        # Queue for thread-safe GUI updates
        self.queue = queue.Queue()

        # Configure font
        # Prioritize fonts with good Unicode/Powerline support
        available_fonts = set(font.families(master))
        if "Cascadia Code" in available_fonts:
            font_family = "Cascadia Code"
        elif "Consolas" in available_fonts:
            font_family = "Consolas"
        else:
            font_family = "Courier New"
        
        self.custom_font = font.Font(family=font_family, size=11)

        # Text widget
        # blockcursor=True gives a block cursor like a console
        self.text_area = tk.Text(master, bg="#000000", fg="#aaaaaa", 
                                 font=self.custom_font, insertbackground="white",
                                 state="normal", blockcursor=True)
        self.text_area.pack(expand=True, fill="both")
        self.text_area.focus_set()

        # ANSI Colors (Standard VGA)
        self.colors = {
            # FG
            '30': '#000000', '31': '#aa0000', '32': '#00aa00', '33': '#aa5500',
            '34': '#0000aa', '35': '#aa00aa', '36': '#00aaaa', '37': '#aaaaaa',
            '90': '#555555', '91': '#ff5555', '92': '#55ff55', '93': '#ffff55',
            '94': '#5555ff', '95': '#ff55ff', '96': '#55ffff', '97': '#ffffff',
            # BG
            '40': '#000000', '41': '#aa0000', '42': '#00aa00', '43': '#aa5500',
            '44': '#0000aa', '45': '#aa00aa', '46': '#00aaaa', '47': '#aaaaaa',
            '100': '#555555', '101': '#ff5555', '102': '#55ff55', '103': '#ffff55',
            '104': '#5555ff', '105': '#ff55ff', '106': '#55ffff', '107': '#ffffff',
        }
        
        # Initialize tags
        for code, color in self.colors.items():
            if int(code) >= 40:
                self.text_area.tag_config(f"bg_{code}", background=color)
            else:
                self.text_area.tag_config(f"fg_{code}", foreground=color)

        # Parser State
        self.state = 'NORMAL'
        self.csi_params = []
        self.csi_curr_param = ""
        self.current_tags = [] # List of tag names to apply to new text

        # Bindings
        self.text_area.bind("<Key>", self.on_key)
        
        self.socket = None
        self.connect()
        self.master.after(50, self.process_queue)

    def connect(self):
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect(('localhost', 5555))
            self.queue.put(b"Connected to QEMU.\r\n")
            
            self.thread = threading.Thread(target=self.receive_loop, daemon=True)
            self.thread.start()
        except Exception as e:
            self.queue.put(f"Connection failed: {e}\r\n".encode('utf-8'))

    def receive_loop(self):
        while True:
            try:
                data = self.socket.recv(4096)
                if not data: break
                self.queue.put(data)
            except:
                break
        self.queue.put(None)

    def process_queue(self):
        try:
            while True:
                data = self.queue.get_nowait()
                if data is None:
                    self.text_area.insert(tk.END, "\nDisconnected.")
                    self.text_area.config(state="disabled")
                    return
                
                # Decode
                if isinstance(data, bytes):
                    text = data.decode('utf-8', errors='replace')
                else:
                    text = data
                
                self.parse_ansi(text)
                self.text_area.see("insert")
        except queue.Empty:
            pass
        self.master.after(10, self.process_queue)

    def parse_ansi(self, text):
        for char in text:
            if self.state == 'NORMAL':
                if char == '\x1b':
                    self.state = 'ESC'
                elif char == '\r':
                    # Carriage Return: Move cursor to start of current line
                    self.text_area.mark_set("insert", "insert linestart")
                elif char == '\n':
                    # Line Feed: Move down
                    # If at bottom, add new line
                    if self.text_area.compare("insert", ">=", "end-1c linestart"):
                        self.text_area.insert("end", "\n")
                    self.text_area.mark_set("insert", "insert+1l")
                elif char == '\b':
                    # Backspace: Move left
                    # Check if we are at start of line
                    if self.text_area.compare("insert", "!=", "insert linestart"):
                        self.text_area.mark_set("insert", "insert-1c")
                elif char == '\t':
                    self.text_area.insert("insert", "\t")
                elif char == '\x07': # Bell
                    pass
                else:
                    # Normal character
                    # VT100 behavior: Overwrite character at cursor
                    # Check if we are at end of line
                    if self.text_area.compare("insert", "==", "insert lineend"):
                        # Append
                        self.text_area.insert("insert", char, tuple(self.current_tags))
                    else:
                        # Overwrite
                        self.text_area.delete("insert")
                        self.text_area.insert("insert", char, tuple(self.current_tags))
            
            elif self.state == 'ESC':
                if char == '[':
                    self.state = 'CSI'
                    self.csi_params = []
                    self.csi_curr_param = ""
                else:
                    # Ignore other sequences (like G0 sets) for now, reset
                    self.state = 'NORMAL'
            
            elif self.state == 'CSI':
                if char.isdigit():
                    self.csi_curr_param += char
                elif char == ';':
                    self.csi_params.append(self.csi_curr_param)
                    self.csi_curr_param = ""
                elif char == '?':
                    # Private mode, ignore for now but consume
                    pass
                else:
                    # Final byte
                    if self.csi_curr_param:
                        self.csi_params.append(self.csi_curr_param)
                    
                    self.handle_csi(char, self.csi_params)
                    self.state = 'NORMAL'

    def handle_csi(self, cmd, params):
        # Default param is usually 0 or 1 depending on command
        # Helper to get int param
        def get_param(idx, default=1):
            if idx < len(params) and params[idx]:
                return int(params[idx])
            return default

        if cmd == 'm': # SGR - Select Graphic Rendition
            if not params:
                self.current_tags = []
            else:
                for p in params:
                    if not p: continue
                    code = int(p)
                    if code == 0:
                        self.current_tags = []
                    elif 30 <= code <= 37 or 90 <= code <= 97:
                        # Remove existing FG
                        self.current_tags = [t for t in self.current_tags if not t.startswith('fg_')]
                        self.current_tags.append(f"fg_{code}")
                    elif 40 <= code <= 47 or 100 <= code <= 107:
                        # Remove existing BG
                        self.current_tags = [t for t in self.current_tags if not t.startswith('bg_')]
                        self.current_tags.append(f"bg_{code}")
        
        elif cmd == 'K': # EL - Erase in Line
            mode = get_param(0, 0)
            if mode == 0: # Cursor to end
                self.text_area.delete("insert", "insert lineend")
            elif mode == 1: # Start to cursor
                self.text_area.delete("insert linestart", "insert")
            elif mode == 2: # Whole line
                self.text_area.delete("insert linestart", "insert lineend")
        
        elif cmd == 'J': # ED - Erase in Display
            mode = get_param(0, 0)
            if mode == 0: # Cursor to end of screen
                self.text_area.delete("insert", tk.END)
            elif mode == 1: # Start of screen to cursor
                self.text_area.delete("1.0", "insert")
            elif mode == 2: # Clear entire screen
                self.text_area.delete("1.0", tk.END)
        
        elif cmd == 'H' or cmd == 'f': # CUP - Cursor Position
            row = get_param(0, 1)
            col = get_param(1, 1)
            # Tkinter indices are line.col (line is 1-based, col is 0-based)
            self.text_area.mark_set("insert", f"{row}.{col-1}")

        elif cmd == 'A': # Cursor Up
            n = get_param(0, 1)
            self.text_area.mark_set("insert", f"insert-{n}l")
        elif cmd == 'B': # Cursor Down
            n = get_param(0, 1)
            self.text_area.mark_set("insert", f"insert+{n}l")
        elif cmd == 'C': # Cursor Forward
            n = get_param(0, 1)
            self.text_area.mark_set("insert", f"insert+{n}c")
        elif cmd == 'D': # Cursor Back
            n = get_param(0, 1)
            self.text_area.mark_set("insert", f"insert-{n}c")

    def on_key(self, event):
        # Map special keys
        if event.keysym == 'Return':
            self.send(b'\r')
        elif event.keysym == 'BackSpace':
            self.send(b'\x7f') # Usually DEL/Backspace
        elif event.keysym == 'Tab':
            self.send(b'\t')
        elif event.keysym == 'Up':
            self.send(b'\x1b[A')
        elif event.keysym == 'Down':
            self.send(b'\x1b[B')
        elif event.keysym == 'Right':
            self.send(b'\x1b[C')
        elif event.keysym == 'Left':
            self.send(b'\x1b[D')
        elif event.keysym == 'Home':
            self.send(b'\x1b[1~') # VT100/Linux
        elif event.keysym == 'End':
            self.send(b'\x1b[4~')
        elif event.char:
            # Handle Ctrl+C etc
            self.send(event.char.encode('utf-8'))
        return "break"

    def send(self, data):
        if self.socket:
            try:
                self.socket.sendall(data)
            except: pass

if __name__ == "__main__":
    root = tk.Tk()
    gui = TerminalGUI(root)
    root.mainloop()
