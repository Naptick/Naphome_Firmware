#!/usr/bin/env python3
"""
Naptick Voice Assistant Dashboard
Monitors serial logs and displays status indicators with meaningful labels.
"""

import serial
import re
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext
from datetime import datetime
from collections import deque
import queue

# Status tracking
class StatusTracker:
    def __init__(self):
        self.wifi_status = "Unknown"
        self.spotify_status = "Unknown"
        self.aws_status = "Unknown"
        self.wake_word_detected = False
        self.last_wake_time = None
        self.muted = False
        self.audio_playing = False
        self.log_lines = deque(maxlen=500)
        self.stats = {
            'wake_events': 0,
            'aws_connects': 0,
            'aws_disconnects': 0,
            'wifi_connects': 0,
            'errors': 0,
        }

# Color mappings
STATUS_COLORS = {
    'connected': '#00FF00',      # Green
    'connecting': '#FFA500',      # Orange/Amber
    'disconnected': '#FF0000',    # Red
    'ready': '#00FFFF',           # Cyan
    'error': '#FF0000',           # Red
    'unknown': '#808080',         # Gray
}

def parse_log_line(line, tracker):
    """Parse a log line and update status tracker."""
    if not line or len(line.strip()) == 0:
        return
    
    line_lower = line.lower()
    
    # Wi-Fi status
    if 'wifi' in line_lower or 'wifi' in line:
        if 'connected' in line_lower and 'the chateau' in line_lower:
            tracker.wifi_status = "Connected"
            tracker.stats['wifi_connects'] += 1
        elif 'disconnect' in line_lower:
            tracker.wifi_status = "Disconnected"
        elif 'connect' in line_lower and 'ssid' in line_lower:
            tracker.wifi_status = "Connecting"
    
    # AWS IoT status
    if 'aws' in line_lower or 'mqtt' in line_lower:
        if 'connection established' in line_lower or 'connected to aws iot' in line_lower:
            tracker.aws_status = "Connected"
            tracker.stats['aws_connects'] += 1
        elif 'connect failed' in line_lower or 'disconnect' in line_lower:
            tracker.aws_status = "Disconnected"
            tracker.stats['aws_disconnects'] += 1
        elif 'reconnect' in line_lower or 'connecting' in line_lower:
            tracker.aws_status = "Reconnecting"
        elif 'aws led: connected' in line_lower:
            tracker.aws_status = "Connected"
        elif 'aws led: disconnected' in line_lower:
            tracker.aws_status = "Reconnecting"
    
    # Spotify status
    if 'spotify' in line_lower:
        if 'init failed' in line_lower or 'failed to start' in line_lower:
            tracker.spotify_status = "Error"
        elif 'client init' in line_lower and 'failed' not in line_lower:
            tracker.spotify_status = "Ready"
        elif 'play' in line_lower or 'resume' in line_lower:
            tracker.spotify_status = "Playing"
        elif 'pause' in line_lower or 'stop' in line_lower:
            tracker.spotify_status = "Paused"
    
    # Wake word detection
    if 'wake' in line_lower and ('detected' in line_lower or 'energy' in line_lower or 'simulated' in line_lower):
        tracker.wake_word_detected = True
        tracker.last_wake_time = datetime.now()
        tracker.stats['wake_events'] += 1
    
    # Mute status
    if 'muted' in line_lower or 'mute' in line_lower:
        tracker.muted = 'muted' in line_lower or 'true' in line_lower
    
    # Audio playback
    if 'audio playback' in line_lower or 'tts' in line_lower:
        if 'start' in line_lower:
            tracker.audio_playing = True
        elif 'stop' in line_lower:
            tracker.audio_playing = False
    
    # Errors
    if 'error' in line_lower or 'failed' in line_lower:
        if 'stack overflow' not in line_lower:  # Don't count stack overflow as regular error
            tracker.stats['errors'] += 1
    
    # LED status messages
    if 'led' in line_lower:
        if 'wifi led' in line_lower:
            if 'connected' in line_lower or 'cyan' in line_lower:
                tracker.wifi_status = "Connected"
            elif 'connecting' in line_lower:
                tracker.wifi_status = "Connecting"
            elif 'failed' in line_lower:
                tracker.wifi_status = "Error"
        elif 'spotify led' in line_lower:
            if 'green' in line_lower or 'connected' in line_lower:
                tracker.spotify_status = "Ready"
            elif 'amber' in line_lower or 'starting' in line_lower:
                tracker.spotify_status = "Connecting"
            elif 'red' in line_lower or 'failed' in line_lower:
                tracker.spotify_status = "Error"
        elif 'aws led' in line_lower:
            if 'connected' in line_lower or 'green' in line_lower:
                tracker.aws_status = "Connected"
            elif 'disconnected' in line_lower or 'amber' in line_lower or 'reconnecting' in line_lower:
                tracker.aws_status = "Reconnecting"

def get_status_color(status):
    """Get color for a status."""
    status_lower = status.lower()
    if 'connected' in status_lower or 'ready' in status_lower or 'playing' in status_lower:
        return STATUS_COLORS['connected']
    elif 'connecting' in status_lower or 'reconnecting' in status_lower:
        return STATUS_COLORS['connecting']
    elif 'disconnected' in status_lower or 'error' in status_lower:
        return STATUS_COLORS['error']
    elif 'paused' in status_lower:
        return STATUS_COLORS['ready']
    else:
        return STATUS_COLORS['unknown']

class DashboardApp:
    def __init__(self, root, port, baud=115200):
        self.root = root
        self.port = port
        self.baud = baud
        self.tracker = StatusTracker()
        self.serial_conn = None
        self.running = False
        self.log_queue = queue.Queue()
        
        self.setup_ui()
        self.start_serial_monitor()
        self.update_ui()
    
    def setup_ui(self):
        self.root.title("Naptick Voice Assistant Dashboard")
        self.root.geometry("1000x700")
        
        # Main container
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(1, weight=1)
        main_frame.rowconfigure(1, weight=1)
        
        # Title
        title = ttk.Label(main_frame, text="Naptick Voice Assistant Status", 
                         font=('Arial', 16, 'bold'))
        title.grid(row=0, column=0, columnspan=2, pady=(0, 10))
        
        # Status panel (left)
        status_frame = ttk.LabelFrame(main_frame, text="Status Indicators", padding="10")
        status_frame.grid(row=1, column=0, sticky=(tk.W, tk.E, tk.N, tk.S), padx=(0, 10))
        
        # Wi-Fi Status
        self.create_status_indicator(status_frame, "Wi-Fi", 0)
        
        # Spotify Status
        self.create_status_indicator(status_frame, "Spotify", 1)
        
        # AWS IoT Status
        self.create_status_indicator(status_frame, "AWS IoT", 2)
        
        # Wake Word Status
        wake_frame = ttk.Frame(status_frame)
        wake_frame.grid(row=3, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=10)
        ttk.Label(wake_frame, text="Wake Word:", font=('Arial', 10, 'bold')).grid(row=0, column=0, sticky=tk.W)
        self.wake_indicator = tk.Canvas(wake_frame, width=20, height=20, highlightthickness=0)
        self.wake_indicator.grid(row=0, column=1, padx=5)
        self.wake_label = ttk.Label(wake_frame, text="Not detected")
        self.wake_label.grid(row=0, column=2, sticky=tk.W)
        
        # Mute Status
        mute_frame = ttk.Frame(status_frame)
        mute_frame.grid(row=4, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=5)
        ttk.Label(mute_frame, text="Mute:", font=('Arial', 10, 'bold')).grid(row=0, column=0, sticky=tk.W)
        self.mute_indicator = tk.Canvas(mute_frame, width=20, height=20, highlightthickness=0)
        self.mute_indicator.grid(row=0, column=1, padx=5)
        self.mute_label = ttk.Label(mute_frame, text="Not muted")
        self.mute_label.grid(row=0, column=2, sticky=tk.W)
        
        # Audio Playback
        audio_frame = ttk.Frame(status_frame)
        audio_frame.grid(row=5, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=5)
        ttk.Label(audio_frame, text="Audio:", font=('Arial', 10, 'bold')).grid(row=0, column=0, sticky=tk.W)
        self.audio_indicator = tk.Canvas(audio_frame, width=20, height=20, highlightthickness=0)
        self.audio_indicator.grid(row=0, column=1, padx=5)
        self.audio_label = ttk.Label(audio_frame, text="Idle")
        self.audio_label.grid(row=0, column=2, sticky=tk.W)
        
        # Statistics
        stats_frame = ttk.LabelFrame(status_frame, text="Statistics", padding="10")
        stats_frame.grid(row=6, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=10)
        self.stats_labels = {}
        stats = ['wake_events', 'aws_connects', 'aws_disconnects', 'wifi_connects', 'errors']
        for i, stat in enumerate(stats):
            label = ttk.Label(stats_frame, text=f"{stat.replace('_', ' ').title()}: 0")
            label.grid(row=i, column=0, sticky=tk.W, pady=2)
            self.stats_labels[stat] = label
        
        # Log panel (right)
        log_frame = ttk.LabelFrame(main_frame, text="Serial Log", padding="10")
        log_frame.grid(row=1, column=1, sticky=(tk.W, tk.E, tk.N, tk.S))
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)
        
        self.log_text = scrolledtext.ScrolledText(log_frame, width=70, height=30, 
                                                   font=('Courier', 9), wrap=tk.WORD)
        self.log_text.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        self.log_text.config(state=tk.DISABLED)
        
        # Connection status
        self.conn_label = ttk.Label(main_frame, text=f"Connected to {self.port}", 
                                    foreground="green")
        self.conn_label.grid(row=2, column=0, columnspan=2, pady=5)
    
    def create_status_indicator(self, parent, name, row):
        """Create a status indicator with label and color circle."""
        frame = ttk.Frame(parent)
        frame.grid(row=row, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=5)
        
        label = ttk.Label(frame, text=f"{name}:", font=('Arial', 10, 'bold'), width=12)
        label.grid(row=0, column=0, sticky=tk.W)
        
        canvas = tk.Canvas(frame, width=20, height=20, highlightthickness=0)
        canvas.grid(row=0, column=1, padx=5)
        canvas.create_oval(2, 2, 18, 18, fill=STATUS_COLORS['unknown'], outline='black', width=1)
        
        status_label = ttk.Label(frame, text="Unknown")
        status_label.grid(row=0, column=2, sticky=tk.W)
        
        setattr(self, f"{name.lower().replace(' ', '_')}_canvas", canvas)
        setattr(self, f"{name.lower().replace(' ', '_')}_label", status_label)
    
    def start_serial_monitor(self):
        """Start the serial monitoring thread."""
        try:
            self.serial_conn = serial.Serial(self.port, self.baud, timeout=1)
            self.running = True
            thread = threading.Thread(target=self.serial_reader, daemon=True)
            thread.start()
        except Exception as e:
            self.log_message(f"Error connecting to {self.port}: {e}", "error")
            self.conn_label.config(text=f"Error: {e}", foreground="red")
    
    def serial_reader(self):
        """Read from serial port in background thread."""
        while self.running:
            try:
                if self.serial_conn and self.serial_conn.in_waiting:
                    line = self.serial_conn.readline()
                    try:
                        text = line.decode('utf-8', errors='ignore').strip()
                        if text:
                            self.log_queue.put(text)
                    except:
                        pass
                else:
                    import time
                    time.sleep(0.1)
            except Exception as e:
                if self.running:
                    self.log_queue.put(f"Serial error: {e}")
                break
    
    def update_ui(self):
        """Update UI from log queue and status tracker."""
        # Process log queue
        try:
            while True:
                line = self.log_queue.get_nowait()
                self.tracker.log_lines.append(line)
                parse_log_line(line, self.tracker)
                self.log_message(line)
        except queue.Empty:
            pass
        
        # Update status indicators
        self.update_status_indicator("wifi", self.tracker.wifi_status)
        self.update_status_indicator("spotify", self.tracker.spotify_status)
        self.update_status_indicator("aws_iot", self.tracker.aws_status)
        
        # Update wake word indicator
        if self.tracker.wake_word_detected:
            self.wake_indicator.delete("all")
            self.wake_indicator.create_oval(2, 2, 18, 18, fill='#FFA500', outline='black', width=1)
            if self.tracker.last_wake_time:
                elapsed = (datetime.now() - self.tracker.last_wake_time).total_seconds()
                if elapsed < 2:
                    self.wake_label.config(text="Detected!")
                else:
                    self.wake_label.config(text=f"Last: {elapsed:.1f}s ago")
            self.tracker.wake_word_detected = False
        else:
            self.wake_indicator.delete("all")
            self.wake_indicator.create_oval(2, 2, 18, 18, fill='#808080', outline='black', width=1)
            self.wake_label.config(text="Not detected")
        
        # Update mute indicator
        if self.tracker.muted:
            self.mute_indicator.delete("all")
            self.mute_indicator.create_oval(2, 2, 18, 18, fill='#FF0000', outline='black', width=1)
            self.mute_label.config(text="Muted")
        else:
            self.mute_indicator.delete("all")
            self.mute_indicator.create_oval(2, 2, 18, 18, fill='#808080', outline='black', width=1)
            self.mute_label.config(text="Not muted")
        
        # Update audio indicator
        if self.tracker.audio_playing:
            self.audio_indicator.delete("all")
            self.audio_indicator.create_oval(2, 2, 18, 18, fill='#0000FF', outline='black', width=1)
            self.audio_label.config(text="Playing")
        else:
            self.audio_indicator.delete("all")
            self.audio_indicator.create_oval(2, 2, 18, 18, fill='#808080', outline='black', width=1)
            self.audio_label.config(text="Idle")
        
        # Update statistics
        for stat, label in self.stats_labels.items():
            count = self.tracker.stats.get(stat, 0)
            label.config(text=f"{stat.replace('_', ' ').title()}: {count}")
        
        # Schedule next update
        self.root.after(100, self.update_ui)
    
    def update_status_indicator(self, name, status):
        """Update a status indicator."""
        canvas = getattr(self, f"{name}_canvas", None)
        label = getattr(self, f"{name}_label", None)
        if canvas and label:
            color = get_status_color(status)
            canvas.delete("all")
            canvas.create_oval(2, 2, 18, 18, fill=color, outline='black', width=1)
            label.config(text=status)
    
    def log_message(self, message, level="info"):
        """Add a message to the log display."""
        self.log_text.config(state=tk.NORMAL)
        timestamp = datetime.now().strftime("%H:%M:%S")
        
        # Color coding
        if level == "error" or "error" in message.lower() or "failed" in message.lower():
            tag = "error"
            self.log_text.tag_config("error", foreground="red")
        elif "wake" in message.lower() or "detected" in message.lower():
            tag = "wake"
            self.log_text.tag_config("wake", foreground="orange")
        elif "connected" in message.lower() or "ready" in message.lower():
            tag = "success"
            self.log_text.tag_config("success", foreground="green")
        else:
            tag = "normal"
        
        self.log_text.insert(tk.END, f"[{timestamp}] {message}\n", tag)
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)
    
    def on_closing(self):
        """Handle window closing."""
        self.running = False
        if self.serial_conn:
            self.serial_conn.close()
        self.root.destroy()

def main():
    import sys
    import argparse
    
    parser = argparse.ArgumentParser(description="Naptick Voice Assistant Dashboard")
    parser.add_argument("--port", "-p", default="/dev/cu.usbserial-110",
                       help="Serial port (default: /dev/cu.usbserial-110)")
    parser.add_argument("--baud", "-b", type=int, default=115200,
                       help="Baud rate (default: 115200)")
    
    args = parser.parse_args()
    
    root = tk.Tk()
    app = DashboardApp(root, args.port, args.baud)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()

if __name__ == "__main__":
    main()
