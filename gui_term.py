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
        self.palette = {
            # Standard
            0: '#000000', 1: '#aa0000', 2: '#00aa00', 3: '#aa5500',
            4: '#0000aa', 5: '#aa00aa', 6: '#00aaaa', 7: '#aaaaaa',
            # Bright
            8: '#555555', 9: '#ff5555', 10: '#55ff55', 11: '#ffff55',
            12: '#5555ff', 13: '#ff55ff', 14: '#55ffff', 15: '#ffffff',
        }
        
        # State
        self.fg_idx = 7  # Default FG (White/Gray)
        self.bg_idx = 0  # Default BG (Black)
        self.reverse_video = False
        self.bold_mode = False

        # Parser State
        self.state = 'NORMAL'
        self.csi_params = []
        self.csi_curr_param = ""
        self.current_tags = [] # List of tag names to apply to new text
        self.update_tags()

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
                elif ord(char) < 32:
                    # Ignore other control characters (like SI/SO \x0e \x0f)
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
                elif char == ']':
                    self.state = 'OSC'
                    self.osc_buffer = ""
                elif char in '()':
                    self.state = 'CHARSET'
                else:
                    # Ignore other sequences (like G0 sets) for now, reset
                    self.state = 'NORMAL'
            
            elif self.state == 'OSC':
                if char == '\x07': # BEL terminates OSC
                    self.state = 'NORMAL'
                elif char == '\x1b': # ESC might be start of ST (ESC \)
                    self.state = 'OSC_ESC'
                else:
                    self.osc_buffer += char

            elif self.state == 'OSC_ESC':
                if char == '\\': # ST terminates OSC
                    self.state = 'NORMAL'
                else:
                    self.state = 'NORMAL'

            elif self.state == 'CHARSET':
                # Consume one char (the charset designator)
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

    def update_tags(self):
        fg = self.fg_idx
        bg = self.bg_idx
        
        if self.bold_mode and fg < 8:
            fg += 8
            
        if self.reverse_video:
            fg, bg = bg, fg
        
        fg_hex = self.palette.get(fg, '#aaaaaa')
        bg_hex = self.palette.get(bg, '#000000')
        
        fg_tag = f"fg_{fg_hex}"
        bg_tag = f"bg_{bg_hex}"
        
        # Configure tags if they don't exist (or re-configure, it's cheap)
        self.text_area.tag_config(fg_tag, foreground=fg_hex)
        self.text_area.tag_config(bg_tag, background=bg_hex)
        
        self.current_tags = [fg_tag, bg_tag]

    def move_cursor(self, row, col):
        # Extend rows if needed
        # Tkinter text always has a trailing newline, so 'end' is (last_line + 1).0
        # We want to ensure we have at least 'row' lines.
        current_lines = int(self.text_area.index("end-1c").split('.')[0])
        if row > current_lines:
            self.text_area.insert("end", "\n" * (row - current_lines))
        
        # Extend columns if needed (pad with spaces)
        # This ensures that if we jump to column 50 on an empty line, we are actually at 50
        line_len = len(self.text_area.get(f"{row}.0", f"{row}.end"))
        if col - 1 > line_len:
            self.text_area.insert(f"{row}.end", " " * (col - 1 - line_len))

        # Tkinter indices are line.col (line is 1-based, col is 0-based)
        self.text_area.mark_set("insert", f"{row}.{col-1}")

    def handle_csi(self, cmd, params):
        # Default param is usually 0 or 1 depending on command
        # Helper to get int param
        def get_param(idx, default=1):
            if idx < len(params) and params[idx]:
                return int(params[idx])
            return default

        if cmd == 'm': # SGR - Select Graphic Rendition
            if not params:
                self.fg_idx = 7
                self.bg_idx = 0
                self.reverse_video = False
                self.bold_mode = False
            else:
                for p in params:
                    if not p: continue
                    code = int(p)
                    if code == 0:
                        self.fg_idx = 7
                        self.bg_idx = 0
                        self.reverse_video = False
                        self.bold_mode = False
                    elif code == 1:
                        self.bold_mode = True
                    elif code == 7:
                        self.reverse_video = True
                    elif code == 27:
                        self.reverse_video = False
                    elif 30 <= code <= 37:
                        self.fg_idx = code - 30
                    elif 40 <= code <= 47:
                        self.bg_idx = code - 40
                    elif 90 <= code <= 97:
                        self.fg_idx = code - 90 + 8
                    elif 100 <= code <= 107:
                        self.bg_idx = code - 100 + 8
            self.update_tags()
        
        elif cmd == 'h': # SM - Set Mode
            if params == ['25']: # Show Cursor
                self.text_area.config(insertwidth=2)
        
        elif cmd == 'l': # RM - Reset Mode
            if params == ['25']: # Hide Cursor
                self.text_area.config(insertwidth=0)

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
            self.move_cursor(row, col)

        elif cmd == 'd': # VPA - Vertical Position Absolute
            row = get_param(0, 1)
            # Keep current col
            current_col = int(self.text_area.index("insert").split('.')[1]) + 1
            self.move_cursor(row, current_col)
        
        elif cmd == 'G': # CHA - Cursor Horizontal Absolute
            col = get_param(0, 1)
            # Keep current row
            current_row = int(self.text_area.index("insert").split('.')[0])
            self.move_cursor(current_row, col)

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
