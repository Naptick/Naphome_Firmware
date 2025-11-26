#!/usr/bin/env python3
"""
Naphome Voice Assistant Web Dashboard
A local web-based dashboard for monitoring the Naphome voice assistant with Gemini AI.
"""

import serial
import serial.tools.list_ports
import threading
import time
import re
import os
import subprocess
import json
import ssl
import glob
from datetime import datetime
from collections import deque
from pathlib import Path
from flask import Flask, render_template_string, jsonify, request
from flask_cors import CORS

# MQTT imports
try:
    import paho.mqtt.client as mqtt
    MQTT_AVAILABLE = True
except ImportError:
    MQTT_AVAILABLE = False
    print("WARNING: paho-mqtt not installed. AWS MQTT subscription disabled.")
    print("Install with: pip install paho-mqtt")

# Spotify Web API imports
try:
    import requests
    SPOTIFY_API_AVAILABLE = True
except ImportError:
    SPOTIFY_API_AVAILABLE = False
    print("WARNING: requests not installed. Spotify device scanning disabled.")
    print("Install with: pip install requests")

app = Flask(__name__)
CORS(app)

# Status tracking
status = {
    'wifi': 'Unknown',
    'wifi_ip': None,  # Extracted from logs
    'spotify': 'Unknown',  # Will be updated from logs
    'spotify_detail': None,
    'spotify_device_name': None,
    'spotify_track': None,
    'spotify_artist': None,
    'spotify_volume': None,
    'aws': 'Unknown',  # Will be updated from logs
    'aws_device_id': None,
    'serial_connected': False,
    'serial_port': '',
    'wake_word': False,
    'last_wake_time': None,
    'muted': False,
    'audio_playing': False,
    'gemini': {
        'status': 'Ready',  # Ready, Active, Processing
        'stt_count': 0,
        'transcript_count': 0,
        'last_transcript': None,
        'last_transcript_time': None,
        'llm_processing': False,
        'tts_playing': False,
        'batch_stt_active': False,
        'audio_accumulated': 0,
        'speech_detected': False,
        'vad_active': False,
        'function_calls': 0,
        'last_function': None,
        'last_function_time': None,
        'last_led_color': None,
    },
    'ai_provider': 'gemini',  # Always Gemini
    'ai_review': {
        'last_review_time': None,
        'last_analysis': None,
        'alerts': [],
        'suggestions': [],
        'reviewing': False,
    },
    'leds': {
        'WIFI': {'r': 0, 'g': 0, 'b': 0, 'state': 'OFF'},
        'SPOTIFY': {'r': 0, 'g': 0, 'b': 0, 'state': 'OFF'},
        'AWS': {'r': 0, 'g': 0, 'b': 0, 'state': 'OFF'},
        'WAKE_WORD': {'r': 0, 'g': 0, 'b': 0, 'state': 'OFF'},
        'MUTE': {'r': 0, 'g': 0, 'b': 0, 'state': 'OFF'},
        'AUDIO_PLAYBACK': {'r': 0, 'g': 0, 'b': 0, 'state': 'OFF'},
    },
    'version': {
        'firmware_version': None,
        'git_commit': None,
        'git_date': None,
        'build_time': None,
        'esp_idf_version': None,
        'is_up_to_date': None,
        'latest_build_time': None,
        'status': 'Unknown',
    },
    'sensors': {
        'temperature_c': None,
        'humidity_rh': None,
        'voc_index': None,
        'co2_ppm': None,
        'ambient_lux': None,
        'proximity': None,
        'pm2_5_ug_m3': None,
        'sht45_available': False,
        'sgp40_available': False,
        'scd40_available': False,
        'vcnl4040_available': False,
        'ec10_available': False,
        'last_update_ms': None,
    },
    'stats': {
        'wake_events': 0,
        'wifi_connects': 0,
        'errors': 0,
        'gemini_errors': 0,
    },
    'error_log': [],  # Persistent error log (survives reboots)
    'last_boot_time': None,  # Track boot time to detect reboots
    'reboot_detected': False,  # Flag for reboot detection
    'mqtt': {
        'connected': False,
        'device_id': None,
        'messages_received': 0,
        'last_message_time': None,
        'last_telemetry_time': None,
        'subscribed_topics': [],
    },
    'i2c_scan': {
        'sensor_bus': {
            'port': 'I2C_NUM_0',
            'sda': 44,  # RXD (UART0_RX)
            'scl': 43,  # TXD (UART0_TX)
            'devices': [],
            'scan_time': None,
            'status': 'Not scanned'
        },
        'audio_bus': {
            'port': 'I2C_NUM_1',
            'sda': 1,
            'scl': 2,
            'devices': [],
            'scan_time': None,
            'status': 'Not scanned'
        },
        'conflicts': []
    },
    'logs': deque()  # No limit - keep all logs
}

serial_conn = None
running = True  # Start as True so thread doesn't exit immediately
current_port = None  # Track current port
serial_thread = None  # Track serial reader thread
port_lock = threading.Lock()

# Track which I2C bus is currently being scanned (for device detection)
current_scanning_bus = None  # Lock for port changes

# MQTT client state
mqtt_client = None
mqtt_connected = False
mqtt_device_id = None
mqtt_lock = threading.Lock()

def ansi_to_html(text):
    """Convert ANSI escape codes to HTML spans with CSS classes."""
    if not text:
        return text
    
    # ANSI color code mapping
    color_map = {
        30: 'black', 31: 'red', 32: 'green', 33: 'yellow', 34: 'blue',
        35: 'magenta', 36: 'cyan', 37: 'white',
        90: 'bright-black', 91: 'bright-red', 92: 'bright-green', 93: 'bright-yellow',
        94: 'bright-blue', 95: 'bright-magenta', 96: 'bright-cyan', 97: 'bright-white',
    }
    
    # Reset code
    RESET = '\033[0m'
    
    # Pattern to match ANSI escape sequences
    ansi_pattern = re.compile(r'\033\[([0-9;]+)m')
    
    result = []
    last_pos = 0
    
    for match in ansi_pattern.finditer(text):
        # Add text before the ANSI code
        if match.start() > last_pos:
            result.append(text[last_pos:match.start()])
        
        # Parse the ANSI code
        codes = [int(c) for c in match.group(1).split(';')]
        classes = []
        
        for code in codes:
            if code == 0:  # Reset
                classes = []
            elif code == 1:  # Bold
                classes.append('ansi-bold')
            elif code == 2:  # Dim
                classes.append('ansi-dim')
            elif code == 3:  # Italic
                classes.append('ansi-italic')
            elif code == 4:  # Underline
                classes.append('ansi-underline')
            elif code in color_map:
                classes.append(f'ansi-{color_map[code]}')
        
        if classes:
            result.append(f'<span class="{" ".join(classes)}">')
        else:
            result.append('</span>')
        
        last_pos = match.end()
    
    # Add remaining text
    if last_pos < len(text):
        result.append(text[last_pos:])
    
    # Close any unclosed spans
    open_spans = result.count('<span')
    close_spans = result.count('</span>')
    if open_spans > close_spans:
        result.append('</span>' * (open_spans - close_spans))
    
    return ''.join(result)

def parse_log_line(line):
    """Parse log line and update status."""
    if not line or len(line.strip()) == 0:
        return
    
    line_lower = line.lower()
    
    # Firmware version information
    if 'firmware version:' in line_lower:
        import re
        version_match = re.search(r'firmware version:\s*([^\s]+)', line, re.IGNORECASE)
        if version_match:
            status['firmware']['version'] = version_match.group(1).strip()
    elif 'git commit:' in line_lower:
        import re
        commit_match = re.search(r'git commit:\s*([a-f0-9]+)', line, re.IGNORECASE)
        if commit_match:
            status['firmware']['git_commit'] = commit_match.group(1).strip()
    elif 'commit date:' in line_lower:
        import re
        date_match = re.search(r'commit date:\s*(.+)', line, re.IGNORECASE)
        if date_match:
            status['firmware']['commit_date'] = date_match.group(1).strip()
    elif 'build timestamp:' in line_lower:
        import re
        timestamp_match = re.search(r'build timestamp:\s*(.+)', line, re.IGNORECASE)
        if timestamp_match:
            status['firmware']['build_timestamp'] = timestamp_match.group(1).strip()
            # Update latest build info when we see a new build timestamp
            if status['firmware']['latest_build_time'] != status['firmware']['build_timestamp']:
                status['firmware']['latest_build_time'] = status['firmware']['build_timestamp']
                status['firmware']['latest_build_status'] = 'Running'
    elif 'esp-idf version:' in line_lower:
        import re
        idf_match = re.search(r'esp-idf version:\s*(.+)', line, re.IGNORECASE)
        if idf_match:
            status['firmware']['esp_idf_version'] = idf_match.group(1).strip()
    
    # Version information parsing
    if 'firmware version:' in line_lower or 'firmware version' in line_lower:
        import re
        # Match: "Firmware Version: 5757fe4 (2025-11-23 12:34:56)"
        version_match = re.search(r'firmware version:\s*([^\n]+)', line, re.IGNORECASE)
        if version_match:
            status['version']['firmware_version'] = version_match.group(1).strip()
            status['version']['status'] = 'Detected'
    
    if 'git commit:' in line_lower:
        import re
        commit_match = re.search(r'git commit:\s*([^\n]+)', line, re.IGNORECASE)
        if commit_match:
            status['version']['git_commit'] = commit_match.group(1).strip()
    
    if 'git date:' in line_lower:
        import re
        date_match = re.search(r'git date:\s*([^\n]+)', line, re.IGNORECASE)
        if date_match:
            status['version']['git_date'] = date_match.group(1).strip()
    
    if 'build time:' in line_lower:
        import re
        build_match = re.search(r'build time:\s*([^\n]+)', line, re.IGNORECASE)
        if build_match:
            status['version']['build_time'] = build_match.group(1).strip()
            # Compare with latest build time
            try:
                latest_build = get_latest_build_time()
                if latest_build:
                    status['version']['latest_build_time'] = latest_build
                    device_build = status['version']['build_time']
                    if device_build and device_build == latest_build:
                        status['version']['is_up_to_date'] = True
                        status['version']['status'] = 'Up to Date'
                    elif device_build:
                        status['version']['is_up_to_date'] = False
                        status['version']['status'] = 'Outdated'
            except:
                pass
    
    if 'esp-idf version:' in line_lower:
        import re
        idf_match = re.search(r'esp-idf version:\s*([^\n]+)', line, re.IGNORECASE)
        if idf_match:
            status['version']['esp_idf_version'] = idf_match.group(1).strip()
    
    # Wi-Fi - check multiple patterns to catch all connection states
    # ESP-IDF typically logs: "Wi-Fi connected", "IP:192.168.x.x", "Connecting to Wi-Fi SSID=..."
    if 'wifi' in line_lower or 'wi-fi' in line_lower:
        # Check for "Wi-Fi connected" message (most reliable) - must not contain "failed"
        if 'connected' in line_lower:
            if 'failed' not in line_lower and 'fail' not in line_lower:
                status['wifi'] = 'Connected'
                status['stats']['wifi_connects'] += 1
                print(f"DEBUG: WiFi set to Connected from: {line[:80]}")
        # Check for disconnection
        elif 'disconnect' in line_lower or 'disconnected' in line_lower:
            status['wifi'] = 'Disconnected'
        # Check for connection failure
        elif 'connect failed' in line_lower or 'connect fail' in line_lower:
            status['wifi'] = 'Failed'
        # Check for "Connecting to Wi-Fi SSID=..." message
        elif 'connecting to' in line_lower and 'ssid' in line_lower:
            status['wifi'] = 'Connecting'
    
    # Also check for IP address messages (ESP-IDF logs "IP:192.168.x.x" when connected)
    # This is a strong indicator of WiFi connection
    if 'ip:' in line_lower:
        # Extract IP pattern: "IP:192.168.1.100" or "IP: 192.168.1.100"
        import re
        ip_pattern = r'ip:\s*(\d+\.\d+\.\d+\.\d+)'
        ip_match = re.search(ip_pattern, line_lower)
        if ip_match:
            status['wifi'] = 'Connected'
            status['wifi_ip'] = ip_match.group(1)
            status['stats']['wifi_connects'] += 1
            print(f"DEBUG: WiFi set to Connected from IP message: {line[:80]}")
    
    # Spotify / cspot status (prioritize cspot messages)
    if 'spotify' in line_lower:
        import re
        
        # ===== CSPOT-SPECIFIC MESSAGES (HIGHEST PRIORITY) =====
        
        # Check for cspot disabled message FIRST (overrides everything)
        # Match: "Spotify Connect (cspot) is disabled in configuration"
        if 'cspot' in line_lower and 'disabled' in line_lower and 'configuration' in line_lower:
            status['spotify'] = 'cspot Disabled'
            status['spotify_detail'] = 'cspot not enabled - rebuild firmware with CONFIG_KVA_SPOTIFY_USE_CSPOT=y'
            status['spotify_track'] = None
            status['spotify_artist'] = None
            status['spotify_volume'] = None
            print(f"[DEBUG] Detected cspot disabled: {line[:100]}")
        
        # cspot initialization messages
        elif ('starting spotify connect' in line_lower and 'cspot' in line_lower) or ('starting spotify connect' in line_lower and 'player' in line_lower):
            status['spotify'] = 'Initializing'
            status['spotify_detail'] = 'Starting cspot player...'
        elif 'spotify connect player started successfully' in line_lower:
            # Extract device name if available
            device_match = re.search(r'device:\s*([^\s\)]+)', line_lower)
            if device_match:
                status['spotify_device_name'] = device_match.group(1)
            status['spotify'] = 'Initializing'
            status['spotify_detail'] = 'cspot player started, initializing...'
        # cspot initialization and pairing
        elif 'waiting for spotify app to provision credentials via zeroconf' in line_lower:
            status['spotify'] = 'Waiting for Pairing'
            status['spotify_detail'] = 'Ready to pair - open Spotify app and look for device'
        elif 'received spotify login blob over zeroconf' in line_lower:
            status['spotify'] = 'Pairing Complete'
            status['spotify_detail'] = 'Credentials received, starting session...'
        elif 'cspot mdn' in line_lower or 'mdns initialized' in line_lower:
            # Extract hostname if available
            hostname_match = re.search(r'hostname:\s*([^\s]+)', line_lower)
            if hostname_match:
                status['spotify_device_name'] = hostname_match.group(1)
            status['spotify'] = 'Initializing'
            status['spotify_detail'] = 'mDNS registering...'
        elif 'loaded spotify credentials from disk' in line_lower:
            status['spotify'] = 'Connecting'
            status['spotify_detail'] = 'Using saved credentials...'
        elif 'spotify connect session started as' in line_lower or 'connect session started as' in line_lower:
            # Extract device name if available
            device_match = re.search(r'started as\s+([^\s]+)', line_lower)
            if device_match:
                status['spotify_device_name'] = device_match.group(1)
                status['spotify'] = 'Connected'
                status['spotify_detail'] = f'Connected as: {device_match.group(1)}'
            else:
                status['spotify'] = 'Connected'
                status['spotify_detail'] = 'Spotify Connect active'
        elif 'entering cspot packet handling loop' in line_lower:
            status['spotify'] = 'Connected'
            status['spotify_detail'] = 'cspot active and ready'
        
        # cspot playback status
        elif 'spotify player playing' in line_lower:
            status['spotify'] = 'Playing'
            status['spotify_detail'] = 'Playback active'
        elif 'spotify player paused' in line_lower:
            status['spotify'] = 'Paused'
            status['spotify_detail'] = 'Playback paused'
        elif 'spotify playback started' in line_lower:
            status['spotify'] = 'Playing'
            status['spotify_detail'] = 'Playback started'
        
        # cspot volume changes
        elif 'spotify volume ->' in line_lower or 'spotify volume:' in line_lower:
            vol_match = re.search(r'volume\s*(?:->|:)\s*(\d+)%', line_lower)
            if vol_match:
                status['spotify_volume'] = int(vol_match.group(1))
        
        # cspot track metadata
        elif 'spotify track metadata updated' in line_lower:
            # Track info updated (but we don't have the actual track name in logs yet)
            # This will be updated when we add track logging to spotify_player.cpp
            pass
        
        # cspot errors and disconnections
        elif 'spotify session lost' in line_lower or 'waiting for reconnect' in line_lower:
            status['spotify'] = 'Disconnected'
            status['spotify_detail'] = 'Session lost, attempting to reconnect...'
        elif 'spotify connect player failed to start' in line_lower:
            status['spotify'] = 'Init Failed'
            status['spotify_detail'] = 'cspot player failed to start'
        elif 'unable to obtain spotify credentials' in line_lower:
            status['spotify'] = 'Pairing Failed'
            status['spotify_detail'] = 'Could not obtain credentials - check network'
        elif 'spotify authentication failed' in line_lower:
            status['spotify'] = 'Auth Failed'
            status['spotify_detail'] = 'Authentication failed - may need to re-pair'
        
        # ===== OLD SPOTIFY_CLIENT MESSAGES (LOWER PRIORITY) =====
        # Only process if cspot status hasn't been set yet
        elif status.get('spotify') in ['Unknown', None]:
            if 'spotify client init failed' in line_lower:
                status['spotify'] = 'Init Failed'
                status['spotify_detail'] = 'Spotify client initialization failed'
            elif 'spotify client init' in line_lower and 'failed' not in line_lower:
                status['spotify'] = 'Initializing'
                status['spotify_detail'] = 'Spotify client starting...'
            elif 'init' in line_lower and 'failed' not in line_lower and 'cspot' not in line_lower:
                status['spotify'] = 'Ready'
                status['spotify_detail'] = 'Spotify client ready'
        
        # ===== GENERIC STATUS (LOWEST PRIORITY) =====
        # Only if no specific status set yet
        if status.get('spotify') in ['Unknown', None]:
            if 'connect session started' in line_lower:
                status['spotify'] = 'Connected'
            elif 'session lost' in line_lower or 'reconnect' in line_lower:
                status['spotify'] = 'Disconnected'
            elif 'authentication failed' in line_lower:
                status['spotify'] = 'Auth Failed'
            elif 'failed' in line_lower or 'error' in line_lower:
                status['spotify'] = 'Error'
            elif 'playing' in line_lower and 'paused' not in line_lower:
                status['spotify'] = 'Playing'
            elif 'paused' in line_lower:
                status['spotify'] = 'Paused'
            elif 'resuming' in line_lower or 'resume' in line_lower:
                status['spotify'] = 'Resuming'
        
        # Extract device name from various log formats
        if 'mdns_hostname_set' in line_lower or 'device name' in line_lower:
            device_match = re.search(r'(?:device name|hostname)[:\s]+["\']?([^"\'\s]+)', line_lower)
            if device_match:
                status['spotify_device_name'] = device_match.group(1)
        
        # Extract track info from cspot logs
        # Format: "Spotify track: Track Name - Artist Name"
        if 'spotify track:' in line_lower:
            track_match = re.search(r'spotify track:\s*(.+?)\s*-\s*(.+?)(?:\s|$)', line_lower)
            if track_match:
                status['spotify_track'] = track_match.group(1).strip()
                status['spotify_artist'] = track_match.group(2).strip()
        
        # Also try generic "now playing" format
        elif 'now playing' in line_lower:
            track_match = re.search(r'(?:now playing|track:)\s*[-–]\s*(.+?)(?:\s+by\s+|\s*[-–]\s*)(.+?)(?:\s|$)', line_lower)
            if track_match:
                status['spotify_track'] = track_match.group(1).strip()
                status['spotify_artist'] = track_match.group(2).strip() if len(track_match.groups()) > 1 else ''
    
    # AWS IoT status
    if 'aws' in line_lower or 'mqtt' in line_lower or 'somnus' in line_lower:
        import re
        if 'connected to aws iot as' in line_lower:
            status['aws'] = 'Connected'
            # Extract device ID if available
            device_match = re.search(r'connected to aws iot as\s+([^\s\)]+)', line_lower)
            if device_match:
                status['aws_device_id'] = device_match.group(1)
        elif 'aws iot bridge initialized' in line_lower:
            status['aws'] = 'Initialized'
        elif 'somnus mqtt service started' in line_lower:
            status['aws'] = 'Connecting'
        elif 'mqtt service started' in line_lower and 'aws' in line_lower:
            status['aws'] = 'Connecting'
        elif 'disconnected' in line_lower and ('mqtt' in line_lower or 'aws' in line_lower):
            status['aws'] = 'Disconnected'
        elif 'failed' in line_lower and ('mqtt' in line_lower or 'aws' in line_lower or 'somnus' in line_lower):
            if 'init failed' in line_lower:
                status['aws'] = 'Init Failed'
            elif 'start failed' in line_lower:
                status['aws'] = 'Start Failed'
            else:
                status['aws'] = 'Failed'
        elif 'telemetry publish' in line_lower:
            if 'failed' in line_lower:
                with status_lock:
                    status['aws'] = 'Error'
                    status['aws_device_id'] = None
            elif 'success' in line_lower or 'published' in line_lower:
                with status_lock:
                    status['aws'] = 'Connected'
        elif re.search(r'aws.*connected|mqtt.*connected|iot.*connected', line_lower):
            with status_lock:
                status['aws'] = 'Connected'
        elif re.search(r'aws.*failed|mqtt.*failed|ssl.*failed|mbedtls.*failed', line_lower):
            with status_lock:
                status['aws'] = 'Error'
        elif 'telemetry publish failed' in line_lower:
            with status_lock:
                status['aws'] = 'Publish Failed'
    
    # Wake word
    if 'wake' in line_lower and ('detected' in line_lower or 'energy' in line_lower or 'simulated' in line_lower):
        status['wake_word'] = True
        status['last_wake_time'] = datetime.now().strftime("%H:%M:%S")
        status['stats']['wake_events'] += 1
        # Reset after 2 seconds
        def reset_wake():
            time.sleep(2)
            status['wake_word'] = False
        threading.Thread(target=reset_wake, daemon=True).start()
    
    # Mute
    if 'muted' in line_lower or 'mute' in line_lower:
        status['muted'] = 'muted' in line_lower or 'true' in line_lower
    
    # Audio
    if 'audio playback' in line_lower or 'tts' in line_lower:
        if 'start' in line_lower:
            status['audio_playing'] = True
        elif 'stop' in line_lower:
            status['audio_playing'] = False
    
    # Sensor readings - parse "Sensors: T=22.5°C H=50.0% VOC=120 CO2=450ppm Lux=200 Prox=0 PM2.5=15"
    if 'sensors:' in line_lower:
        import re
        import time
        # Parse sensor log: "Sensors: T=22.5°C H=50.0% VOC=120 CO2=450ppm Lux=200 Prox=0 PM2.5=15"
        temp_match = re.search(r'T=([0-9.]+)', line_lower)
        if temp_match:
            try:
                status['sensors']['temperature_c'] = float(temp_match.group(1))
                status['sensors']['last_update_ms'] = int(time.time() * 1000)
            except:
                pass
        hum_match = re.search(r'H=([0-9.]+)', line_lower)
        if hum_match:
            try:
                status['sensors']['humidity_rh'] = float(hum_match.group(1))
            except:
                pass
        voc_match = re.search(r'VOC=(\d+)', line_lower)
        if voc_match:
            try:
                status['sensors']['voc_index'] = int(voc_match.group(1))
            except:
                pass
        co2_match = re.search(r'CO2=([0-9.]+)', line_lower)
        if co2_match:
            try:
                status['sensors']['co2_ppm'] = float(co2_match.group(1))
            except:
                pass
        lux_match = re.search(r'Lux=(\d+)', line_lower)
        if lux_match:
            try:
                status['sensors']['ambient_lux'] = int(lux_match.group(1))
            except:
                pass
        prox_match = re.search(r'Prox=(\d+)', line_lower)
        if prox_match:
            try:
                status['sensors']['proximity'] = int(prox_match.group(1))
            except:
                pass
        pm_match = re.search(r'PM2\.5=([0-9.]+)', line_lower)
        if pm_match:
            try:
                status['sensors']['pm2_5_ug_m3'] = float(pm_match.group(1))
            except:
                pass
    
    # Check sensor availability from initialization logs
    if 'sensor_integration' in line_lower or 'sensor' in line_lower:
        import re
        if 'sht45' in line_lower and ('detected' in line_lower or 'initialized' in line_lower):
            status['sensors']['sht45_available'] = True
        elif 'sht45' in line_lower and 'failed' in line_lower:
            status['sensors']['sht45_available'] = False
        if 'sgp40' in line_lower and ('detected' in line_lower or 'initialized' in line_lower):
            status['sensors']['sgp40_available'] = True
        elif 'sgp40' in line_lower and 'failed' in line_lower:
            status['sensors']['sgp40_available'] = False
        if 'scd40' in line_lower and ('detected' in line_lower or 'initialized' in line_lower):
            status['sensors']['scd40_available'] = True
        elif 'scd40' in line_lower and 'failed' in line_lower:
            status['sensors']['scd40_available'] = False
        if 'vcnl4040' in line_lower and ('detected' in line_lower or 'initialized' in line_lower):
            status['sensors']['vcnl4040_available'] = True
        elif 'vcnl4040' in line_lower and 'failed' in line_lower:
            status['sensors']['vcnl4040_available'] = False
        if 'ec10' in line_lower and ('detected' in line_lower or 'initialized' in line_lower):
            status['sensors']['ec10_available'] = True
        elif 'ec10' in line_lower and 'failed' in line_lower:
            status['sensors']['ec10_available'] = False
    
    # AI API events (Gemini only)
    if 'gemini' in line_lower or '🎙️' in line or '💬' in line or '🔊' in line:
        # Gemini Live / Batch STT monitoring
        if 'gemini live' in line_lower or '🎙️' in line:
            if 'speech started' in line_lower or 'accumulating' in line_lower:
                status['gemini']['batch_stt_active'] = True
                status['gemini']['status'] = 'Active'
                status['gemini']['speech_detected'] = True
            if 'speech ended' in line_lower:
                status['gemini']['batch_stt_active'] = True
                status['gemini']['status'] = 'Processing'
                status['gemini']['speech_detected'] = False
            if 'processing batch stt' in line_lower:
                status['gemini']['batch_stt_active'] = True
                status['gemini']['status'] = 'Processing'
        # Gemini batch STT
        if 'gemini: processing batch stt' in line_lower or 'gemini: speech ended' in line_lower:
            status['gemini']['batch_stt_active'] = True
            status['gemini']['status'] = 'Processing'
        
        # Pathway markers (=== GEMINI BATCH STT-LLM-TTS PATHWAY START ===)
        if 'pathway start' in line_lower and 'gemini' in line_lower:
            status['gemini']['pathway_active'] = True
            status['gemini']['status'] = 'Processing'
            status['gemini']['batch_stt_active'] = True
        if 'pathway complete' in line_lower and 'gemini' in line_lower:
            status['gemini']['pathway_active'] = False
            status['gemini']['status'] = 'Ready'
            status['gemini']['batch_stt_active'] = False
        
        # Step-by-step tracking (Step 1/3: STT, Step 2/3: LLM, Step 3/3: TTS)
        if 'step 1/3: stt' in line_lower or 'step 1/3: stt -' in line_lower:
            status['gemini']['stt_count'] += 1
            status['gemini']['status'] = 'Processing'
            status['gemini']['batch_stt_active'] = True
        if 'step 2/3: llm' in line_lower or 'step 2/3: llm -' in line_lower or '💬 [gemini live] step 2/3: llm' in line_lower:
            status['gemini']['llm_processing'] = True
            status['gemini']['status'] = 'Processing'
        if 'step 3/3: tts' in line_lower or 'step 3/3: tts -' in line_lower:
            status['gemini']['tts_processing'] = True
            status['gemini']['status'] = 'Processing'
        
        # Success/Failure tracking
        if '✅ step 1/3: stt success' in line_lower:
            status['gemini']['transcript_count'] += 1
            status['gemini']['batch_stt_active'] = True
            # Extract transcript from log
            transcript_match = re.search(r'success.*["\']([^"\']+)["\']', line_lower)
            if transcript_match:
                status['gemini']['last_transcript'] = transcript_match.group(1).strip()[:100]
                status['gemini']['last_transcript_time'] = datetime.now().strftime("%H:%M:%S")
                status['gemini']['speech_detected'] = True
        if '✅ step 2/3: llm success' in line_lower or '✅ [gemini live] step 2/3: llm success' in line_lower:
            status['gemini']['llm_processing'] = False
            status['gemini']['llm_count'] = status['gemini'].get('llm_count', 0) + 1
            # Extract LLM response
            response_match = re.search(r'success.*["\']([^"\']+)["\']', line_lower)
            if response_match:
                status['gemini']['last_llm_response'] = response_match.group(1).strip()[:200]
        if '✅ step 3/3: tts success' in line_lower:
            status['gemini']['tts_processing'] = False
            status['gemini']['tts_playing'] = True
            status['gemini']['tts_count'] = status['gemini'].get('tts_count', 0) + 1
            status['gemini']['status'] = 'Active'
        
        # Error tracking
        if '❌ step' in line_lower and 'failed' in line_lower:
            status['stats']['gemini_errors'] += 1
            status['gemini']['status'] = 'Error'
            if 'step 1/3' in line_lower:
                status['gemini']['batch_stt_active'] = False
            if 'step 2/3' in line_lower:
                status['gemini']['llm_processing'] = False
            if 'step 3/3' in line_lower:
                status['gemini']['tts_processing'] = False
                status['gemini']['tts_playing'] = False
        
        if 'gemini stt:' in line_lower or '🔊 [gemini stt]' in line_lower:
            status['gemini']['stt_count'] += 1
            status['gemini']['transcript_count'] += 1
            # Extract Gemini transcript
            import re
            transcript_match = re.search(r'gemini stt:\s*["\']?([^"\'\n]+)', line_lower)
            if transcript_match:
                status['gemini']['last_transcript'] = transcript_match.group(1).strip()[:100]
                status['gemini']['last_transcript_time'] = datetime.now().strftime("%H:%M:%S")
                status['gemini']['speech_detected'] = True
                def reset_speech():
                    time.sleep(5)
                    status['gemini']['speech_detected'] = False
                threading.Thread(target=reset_speech, daemon=True).start()
        
        # VAD (Voice Activity Detection)
        if 'vad' in line_lower:
            if 'detected' in line_lower or 'active' in line_lower or 'speech' in line_lower:
                status['gemini']['vad_active'] = True
                status['gemini']['status'] = 'Active'
                # Reset after 2 seconds
                def reset_vad():
                    time.sleep(2)
                    status['gemini']['vad_active'] = False
                threading.Thread(target=reset_vad, daemon=True).start()
        
        # LLM processing (Gemini) - enhanced patterns
        if 'sending to gemini' in line_lower or '💬 [gemini llm]' in line_lower or '💬 [gemini live] step 2/3: llm' in line_lower:
            status['gemini']['llm_processing'] = True
            status['gemini']['status'] = 'Processing'
        if 'gemini chat response:' in line_lower or '✅ [gemini llm] success' in line_lower or '✅ [gemini live] step 2/3: llm success' in line_lower:
            status['gemini']['llm_processing'] = False
            status['gemini']['status'] = 'Active'
            status['gemini']['llm_count'] = status['gemini'].get('llm_count', 0) + 1
            # Extract response text
            import re
            response_match = re.search(r'(?:success|response):\s*["\']?([^"\'\n]+)', line_lower)
            if response_match:
                status['gemini']['last_llm_response'] = response_match.group(1).strip()[:200]
        
        # TTS processing
        if 'tts generated' in line_lower or 'tts generation' in line_lower:
            status['gemini']['tts_playing'] = True
            status['gemini']['status'] = 'Active'
        if 'tts playback' in line_lower:
            if 'failed' in line_lower:
                status['gemini']['tts_playing'] = False
                status['gemini']['status'] = 'Ready'
            elif 'complete' in line_lower or 'finished' in line_lower or 'started' in line_lower:
                status['gemini']['tts_playing'] = True
                status['gemini']['status'] = 'Active'
        
        # Gemini TTS processing
        if '🔊 [gemini tts]' in line_lower:
            if 'generating speech' in line_lower:
                status['gemini']['tts_processing'] = True
                status['gemini']['status'] = 'Processing'
            if 'success' in line_lower:
                status['gemini']['tts_processing'] = False
                status['gemini']['tts_playing'] = True
                status['gemini']['status'] = 'Active'
                status['gemini']['tts_count'] = status['gemini'].get('tts_count', 0) + 1
        
        # Gemini Live pathway tracking (enhanced)
        if 'gemini live' in line_lower and 'pathway' in line_lower:
            if 'start' in line_lower:
                status['gemini']['pathway_active'] = True
                status['gemini']['status'] = 'Processing'
            if 'complete' in line_lower or 'continued' in line_lower:
                status['gemini']['pathway_active'] = False
                status['gemini']['status'] = 'Ready'
            if 'failed' in line_lower:
                status['gemini']['pathway_active'] = False
                status['gemini']['status'] = 'Error'
                status['stats']['gemini_errors'] += 1
        
        # Gemini function calling / tools - enhanced patterns
        if '🔧 [gemini tools]' in line_lower or 'gemini tools' in line_lower or '🔧 [gemini llm] function call detected' in line_lower:
            status['gemini']['function_calls'] = status['gemini'].get('function_calls', 0) + 1
            if 'executing function' in line_lower:
                # Extract function name
                func_match = re.search(r'executing function:\s*(\w+)', line_lower)
                if func_match:
                    status['gemini']['last_function'] = func_match.group(1)
                    status['gemini']['last_function_time'] = datetime.now().strftime("%H:%M:%S")
            # Also check for function call detection in LLM logs
            func_call_match = re.search(r'function call detected:\s*(\w+)', line_lower)
            if func_call_match:
                status['gemini']['last_function'] = func_call_match.group(1)
                status['gemini']['last_function_time'] = datetime.now().strftime("%H:%M:%S")
            if 'set_leds' in line_lower:
                if 'on' in line_lower:
                    status['leds']['SPOTIFY']['state'] = 'ON'  # Use LED status
                elif 'off' in line_lower:
                    status['leds']['SPOTIFY']['state'] = 'OFF'
            if 'set_led_color' in line_lower:
                rgb_match = re.search(r'rgb\((\d+),\s*(\d+),\s*(\d+)\)', line_lower)
                if rgb_match:
                    status['gemini']['last_led_color'] = f"RGB({rgb_match.group(1)}, {rgb_match.group(2)}, {rgb_match.group(3)})"
        
        # Gemini errors
        if 'gemini' in line_lower and ('error' in line_lower or '❌' in line):
            status['stats']['gemini_errors'] += 1
            status['gemini']['status'] = 'Error'
    
    # Critical error detection (Guru Meditation, Panic, etc.)
    error_keywords = [
        'guru meditation error',
        'panic',
        'exception was unhandled',
        'integerdividebyzero',
        'assert failed',
        'abort()',
        'crash',
        'fatal error',
        'watchdog reset',
        'brownout',
        'stack overflow',
        'heap corruption'
    ]
    
    is_critical_error = any(keyword in line_lower for keyword in error_keywords)
    if is_critical_error:
        # Check if this error is already in the log (avoid duplicates)
        error_hash = hash(line.strip())
        error_exists = any(hash(err.get('message', '').strip()) == error_hash for err in status.get('error_log', []))
        
        if not error_exists:
            error_entry = {
                'message': line.strip(),
                'time': datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                'timestamp': time.time()
            }
            status['error_log'].append(error_entry)
            # Keep only last 50 errors
            if len(status['error_log']) > 50:
                status['error_log'] = status['error_log'][-50:]
            # Save to file
            save_error_log()
            status['stats']['errors'] += 1
    
    # Reboot detection
    boot_keywords = [
        'esp32-s3',
        'chip is esp32s3',
        'cpu frequency',
        'free heap',
        'app cpu start',
        'main task',
        'nvs',
        'boot:',
        'rst:0x',
        'boot mode:'
    ]
    
    is_boot_message = any(keyword in line_lower for keyword in boot_keywords) and ('starting' in line_lower or 'initialized' in line_lower or 'rst:' in line_lower)
    if is_boot_message:
        current_boot_time = time.time()
        if status.get('last_boot_time') is None:
            # First boot detection
            status['last_boot_time'] = current_boot_time
        elif current_boot_time - status['last_boot_time'] > 5:
            # Reboot detected (more than 5 seconds since last boot message)
            status['reboot_detected'] = True
            status['last_boot_time'] = current_boot_time
    
    # Errors - exclude expected/non-critical messages
    if 'error' in line_lower or 'failed' in line_lower:
        # Exclude expected fallback messages that aren't actual errors
        exclude_patterns = [
            'stack overflow',
            'certificate discovery',  # Expected fallback to embedded certs
            'using embedded',  # Expected behavior
            'spiffs may not be mounted',  # Expected if SPIFFS not used
            'falling back to embedded',  # Expected fallback
            'certificate directory not found',  # Expected if certs not provisioned
        ]
        if not any(pattern in line_lower for pattern in exclude_patterns):
            status['stats']['errors'] += 1
    
    # I2C Bus Scan parsing
    global current_scanning_bus
    if 'scanning' in line_lower and ('sensor bus' in line_lower or 'audio bus' in line_lower):
        # Detect which bus is being scanned
        bus_name = 'sensor_bus' if 'sensor bus' in line_lower else 'audio_bus'
        current_scanning_bus = bus_name
        status['i2c_scan'][bus_name]['status'] = 'Scanning'
        status['i2c_scan'][bus_name]['devices'] = []
        # Extract GPIO pins if present
        import re
        sda_match = re.search(r'sda=gpio(\d+)', line_lower)
        scl_match = re.search(r'scl=gpio(\d+)', line_lower)
        if sda_match:
            status['i2c_scan'][bus_name]['sda'] = int(sda_match.group(1))
        if scl_match:
            status['i2c_scan'][bus_name]['scl'] = int(scl_match.group(1))
        print(f"DEBUG: Started scanning {bus_name}")
    
    # I2C device found: "✓ Found device at address 0x44"
    if 'found device at address' in line_lower or 'device at address' in line_lower:
        import re
        addr_match = re.search(r'0x([0-9a-fA-F]{2})', line)
        if addr_match:
            addr = int(addr_match.group(1), 16)
            # Use current_scanning_bus if set, otherwise try to determine from line
            bus_name = current_scanning_bus or 'sensor_bus'
            if 'audio bus' in line_lower:
                bus_name = 'audio_bus'
            elif 'sensor bus' in line_lower:
                bus_name = 'sensor_bus'
            elif status['i2c_scan']['audio_bus']['status'] == 'Scanning':
                bus_name = 'audio_bus'
            elif status['i2c_scan']['sensor_bus']['status'] == 'Scanning':
                bus_name = 'sensor_bus'
            
            if addr not in status['i2c_scan'][bus_name]['devices']:
                status['i2c_scan'][bus_name]['devices'].append(addr)
                status['i2c_scan'][bus_name]['devices'].sort()
                print(f"DEBUG: I2C device found on {bus_name}: 0x{addr:02X}")
    
    # I2C scan complete: "Total: 2 device(s) found on Sensor Bus"
    if 'total:' in line_lower and 'device' in line_lower and ('found on' in line_lower or 'sensor bus' in line_lower or 'audio bus' in line_lower):
        bus_name = 'sensor_bus' if 'sensor bus' in line_lower else 'audio_bus'
        status['i2c_scan'][bus_name]['status'] = 'Complete'
        status['i2c_scan'][bus_name]['scan_time'] = datetime.now().strftime("%H:%M:%S")
        import re
        count_match = re.search(r'(\d+)\s+device', line_lower)
        if count_match:
            expected_count = int(count_match.group(1))
            actual_count = len(status['i2c_scan'][bus_name]['devices'])
            if expected_count != actual_count:
                print(f"WARNING: I2C scan count mismatch on {bus_name}: expected {expected_count}, parsed {actual_count}")
    
    # I2C scan end: "=== End scan of Sensor Bus ==="
    if 'end scan of' in line_lower:
        bus_name = 'sensor_bus' if 'sensor bus' in line_lower else 'audio_bus'
        if status['i2c_scan'][bus_name]['status'] == 'Scanning':
            status['i2c_scan'][bus_name]['status'] = 'Complete'
            status['i2c_scan'][bus_name]['scan_time'] = datetime.now().strftime("%H:%M:%S")
        if current_scanning_bus == bus_name:
            current_scanning_bus = None
    
    # I2C scan failed: "Failed to create scan bus"
    if 'failed to create scan bus' in line_lower or 'failed to initialize i2c bus' in line_lower:
        bus_name = 'sensor_bus' if 'sensor bus' in line_lower or 'i2c_num_0' in line_lower else 'audio_bus'
        status['i2c_scan'][bus_name]['status'] = 'Failed'
        status['i2c_scan'][bus_name]['scan_time'] = datetime.now().strftime("%H:%M:%S")
    
    # Check for I2C conflicts (same address on both buses, or bus creation errors)
    if 'i2c' in line_lower and ('conflict' in line_lower or 'already' in line_lower or 'busy' in line_lower or 'in use' in line_lower):
        conflict_msg = line.strip()[:200]
        if conflict_msg not in status['i2c_scan']['conflicts']:
            status['i2c_scan']['conflicts'].append(conflict_msg)
            if len(status['i2c_scan']['conflicts']) > 10:
                status['i2c_scan']['conflicts'].pop(0)  # Keep last 10 conflicts
            print(f"DEBUG: I2C conflict detected: {conflict_msg}")
    
    # Check for duplicate addresses across buses
    sensor_addrs = set(status['i2c_scan']['sensor_bus']['devices'])
    audio_addrs = set(status['i2c_scan']['audio_bus']['devices'])
    duplicate_addrs = sensor_addrs & audio_addrs
    if duplicate_addrs and len(status['i2c_scan']['conflicts']) < 10:
        conflict_msg = f"Duplicate I2C addresses found on both buses: {[hex(a) for a in duplicate_addrs]}"
        if conflict_msg not in status['i2c_scan']['conflicts']:
            status['i2c_scan']['conflicts'].append(conflict_msg)
    
    # LED state parsing - look for "LED[NAME]: RGB(r,g,b)" or "LED[NAME]: OFF"
    if 'led[' in line_lower and ']:' in line_lower:
        import re
        # Match patterns like "LED[WIFI]: RGB(0,120,120)" or "LED[WIFI]: OFF (R:0 G:0 B:0)"
        # Also handle ANSI color codes that might be in the log
        # Remove ANSI codes first for cleaner matching
        line_clean = re.sub(r'\033\[[0-9;]+m|\x1b\[[0-9;]+m', '', line)
        line_clean_lower = line_clean.lower()
        
        # Match LED pattern - be more flexible with whitespace
        led_pattern = r'led\[(\w+)\]:\s*(?:rgb\((\d+),(\d+),(\d+)\)|off(?:\s*\([^)]*\))?)'
        match = re.search(led_pattern, line_clean_lower)
        if match:
            led_name = match.group(1).upper()
            if match.group(2):  # RGB values found
                r = int(match.group(2))
                g = int(match.group(3))
                b = int(match.group(4))
                if led_name in status['leds']:
                    status['leds'][led_name] = {
                        'r': r,
                        'g': g,
                        'b': b,
                        'state': 'ON' if (r > 0 or g > 0 or b > 0) else 'OFF'
                    }
                    print(f"DEBUG: LED[{led_name}] updated to RGB({r},{g},{b})")
            else:  # OFF
                if led_name in status['leds']:
                    status['leds'][led_name] = {'r': 0, 'g': 0, 'b': 0, 'state': 'OFF'}
                    print(f"DEBUG: LED[{led_name}] updated to OFF")

def serial_reader(initial_port, baud=115200):
    """Read from serial port in background thread with auto-reconnect."""
    global serial_conn, running, current_port
    
    # Setup log file
    log_dir = os.path.join(os.path.dirname(__file__), '..', 'logs')
    os.makedirs(log_dir, exist_ok=True)
    log_filename = os.path.join(log_dir, f'dashboard_{datetime.now().strftime("%Y%m%d_%H%M%S")}.log')
    log_file = None
    
    try:
        log_file = open(log_filename, 'a', encoding='utf-8')
        print(f"[Log] Saving logs to: {log_filename}")
    except Exception as e:
        print(f"[Log] Failed to open log file {log_filename}: {e}")
    
    with port_lock:
        if current_port is None:
            current_port = initial_port
        status['serial_port'] = current_port
        status['serial_connected'] = False
    
    # If no initial port, try auto-detection periodically
    if not initial_port:
        print("[Serial] No initial port provided, will try auto-detection...")
    
    while running:
        try:
            # Get current port (may have changed)
            with port_lock:
                port = current_port
                
            # If no port, try auto-detection
            if not port:
                detected = auto_detect_naphome_port()
                if detected:
                    print(f"[Serial] Auto-detected device: {detected}")
                    with port_lock:
                        current_port = detected
                        port = detected
                        status['serial_port'] = port
                else:
                    time.sleep(3)  # Wait longer if no device found
                    continue
            
            # Try to connect
            if serial_conn and serial_conn.is_open:
                try:
                    serial_conn.close()
                except:
                    pass
                serial_conn = None
            
            # Check if port exists
            if not os.path.exists(port):
                print(f"✗ Port {port} does not exist")
                time.sleep(3)
                continue
            
            try:
                # Try to open with exclusive access
                serial_conn = serial.Serial(port, baud, timeout=1, exclusive=True)
                status['serial_connected'] = True
                status['serial_port'] = port
                print(f"✓ Connected to {port}")
            except (serial.SerialException, OSError) as conn_err:
                err_msg = str(conn_err)
                if '22' in err_msg or 'Invalid argument' in err_msg:
                    print(f"✗ Port {port} may be in use or device not ready: {conn_err}")
                    print("  This usually means:")
                    print("  - Another program is using the port")
                    print("  - Device is not powered on")
                    print("  - USB cable disconnected")
                else:
                    print(f"✗ Failed to connect to {port}: {conn_err}")
                status['serial_connected'] = False
                time.sleep(3)
                continue
            except Exception as conn_err:
                print(f"✗ Unexpected error connecting to {port}: {conn_err}")
                status['serial_connected'] = False
                time.sleep(3)
                continue
            
            # Read loop
            while running and serial_conn and serial_conn.is_open:
                # Check if port changed
                with port_lock:
                    if current_port != port:
                        print(f"Port changed from {port} to {current_port}, reconnecting...")
                        break
                
                try:
                    if serial_conn.in_waiting:
                        line = serial_conn.readline()
                        try:
                            # Limit line length to prevent corrupted data floods
                            if len(line) > 1000:
                                # Skip extremely long lines (likely corrupted data)
                                continue
                            text = line.decode('utf-8', errors='ignore').strip()
                            # Filter out lines that are mostly repeated characters (likely corrupted)
                            if text and len(text) > 100:
                                # Check if line is mostly the same character (corruption indicator)
                                char_counts = {}
                                for char in text[:200]:  # Check first 200 chars
                                    char_counts[char] = char_counts.get(char, 0) + 1
                                if char_counts and max(char_counts.values()) > len(text) * 0.8:
                                    # More than 80% same character - likely corrupted, skip it
                                    continue
                            if text and len(text) > 0:
                                log_entry = {
                                    'time': datetime.now().strftime("%H:%M:%S"),
                                    'text': text
                                }
                                status['logs'].append(log_entry)
                                # No limit - keep all logs
                                parse_log_line(text)
                                
                                # Write to log file
                                if log_file:
                                    try:
                                        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                                        log_file.write(f"[{timestamp}] {text}\n")
                                        log_file.flush()  # Ensure immediate write
                                    except Exception as log_err:
                                        pass  # Don't fail on log write errors
                                
                                # Trigger auto-review if error detected
                                if 'error' in text.lower() or 'failed' in text.lower() or 'E (' in text:
                                    # Schedule auto-review in background (non-blocking)
                                    def trigger_review():
                                        time.sleep(2)  # Wait a bit for more logs
                                        print(f"[AI Review] Error detected, triggering review...")
                                        auto_review_logs()
                                    threading.Thread(target=trigger_review, daemon=True).start()
                                
                                # Print to console for debugging (LED, WiFi, Spotify, and AI events)
                                if any(keyword in text.lower() for keyword in ['led[', 'wifi', 'spotify', 'openai', 'gemini', 'realtime', 'transcript', 'vad', 'session']):
                                    print(f"[{log_entry['time']}] {text[:120]}")
                        except Exception as decode_err:
                            pass
                    else:
                        time.sleep(0.05)
                except serial.SerialException as read_err:
                    print(f"Serial read error: {read_err}")
                    status['serial_connected'] = False
                    break  # Break to reconnect
                except Exception as read_err:
                    if running:
                        print(f"Read error: {read_err}")
                    time.sleep(0.1)
            
            # Close connection
            if serial_conn and serial_conn.is_open:
                serial_conn.close()
            with port_lock:
                status['serial_connected'] = False
                status['serial_port'] = current_port or port  # Keep port name even when disconnected
            print(f"✗ Disconnected from {port}, reconnecting...")
                
        except serial.SerialException as e:
            with port_lock:
                status['serial_connected'] = False
                status['serial_port'] = current_port or port  # Keep port name even when disconnected
            print(f"✗ Serial connection error: {e}")
            print("  Retrying in 3 seconds...")
            time.sleep(3)  # Wait before retrying
        except Exception as e:
            with port_lock:
                status['serial_connected'] = False
                status['serial_port'] = current_port or port  # Keep port name even when disconnected
            print(f"✗ Unexpected error: {e}")
            import traceback
            traceback.print_exc()
            time.sleep(3)
    
    # Cleanup
    if serial_conn and serial_conn.is_open:
        serial_conn.close()
    status['serial_connected'] = False
    # Keep port name in status even after cleanup
    if log_file:
        try:
            log_file.close()
        except:
            pass
    print("Serial reader thread ended")

HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
    <title>Naptick Voice Assistant Dashboard</title>
    <meta charset="UTF-8">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: #1a1a1a;
            color: #e0e0e0;
            padding: 20px;
        }
        .container { max-width: 1200px; margin: 0 auto; }
        h1 { color: #4CAF50; margin-bottom: 20px; text-align: center; }
        .status-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        .status-card {
            background: #2a2a2a;
            border-radius: 8px;
            padding: 20px;
            border-left: 4px solid #666;
        }
        .status-card.wifi.connected { border-left-color: #4CAF50; }
        .status-card.wifi.connecting { border-left-color: #FF9800; }
        .status-card.wifi.disconnected { border-left-color: #f44336; }
        .status-card.spotify.ready { border-left-color: #4CAF50; }
        .status-card.spotify.error { border-left-color: #f44336; }
        .status-card.spotify.playing { border-left-color: #2196F3; }
        .status-card.aws.disabled { border-left-color: #666; }
        .status-card.openai.connected { border-left-color: #4CAF50; }
        .status-card.openai.disconnected { border-left-color: #f44336; }
        .status-card.openai.updating { border-left-color: #FF9800; }
        .status-card.openai.active { border-left-color: #2196F3; }
        .status-title { font-size: 14px; color: #aaa; margin-bottom: 10px; text-transform: uppercase; }
        .status-value { font-size: 24px; font-weight: bold; color: #fff; }
        .status-indicator {
            display: inline-block;
            width: 12px;
            height: 12px;
            border-radius: 50%;
            margin-right: 8px;
            background: #666;
        }
        .status-indicator.green { background: #4CAF50; }
        .status-indicator.orange { background: #FF9800; }
        .status-indicator.red { background: #f44336; }
        .status-indicator.blue { background: #2196F3; }
        .status-indicator.gray { background: #666; }
        .stats {
            background: #2a2a2a;
            border-radius: 8px;
            padding: 20px;
            margin-bottom: 20px;
        }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
        }
        .stat-item { text-align: center; }
        .stat-value { font-size: 32px; font-weight: bold; color: #4CAF50; }
        .stat-label { font-size: 12px; color: #aaa; text-transform: uppercase; }
        .logs {
            background: #2a2a2a;
            border-radius: 8px;
            padding: 20px;
            max-height: 400px;
            overflow-y: auto;
        }
        .log-entry {
            padding: 8px;
            margin-bottom: 4px;
            border-radius: 4px;
            font-family: 'Courier New', monospace;
            font-size: 12px;
        }
        .log-entry.error { background: #3a1a1a; color: #ff6b6b; }
        .log-entry.wake { background: #3a2a1a; color: #ffa500; }
        .log-entry.success { background: #1a3a1a; color: #6bff6b; }
        .log-entry.openai { background: #1a1a3a; color: #6b9fff; }
        .log-entry.transcript { background: #1a3a2a; color: #6bff9f; }
        .log-entry.vad { background: #3a2a1a; color: #ffd93d; }
        .log-time { color: #666; margin-right: 10px; }
        /* ANSI color support */
        .ansi-black { color: #000; }
        .ansi-red { color: #ff6b6b; }
        .ansi-green { color: #6bff6b; }
        .ansi-yellow { color: #ffd93d; }
        .ansi-blue { color: #6b9fff; }
        .ansi-magenta { color: #ff6bff; }
        .ansi-cyan { color: #6bffff; }
        .ansi-white { color: #fff; }
        .ansi-bright-black { color: #666; }
        .ansi-bright-red { color: #ff8787; }
        .ansi-bright-green { color: #87ff87; }
        .ansi-bright-yellow { color: #ffeb3b; }
        .ansi-bright-blue { color: #87b3ff; }
        .ansi-bright-magenta { color: #ff87ff; }
        .ansi-bright-cyan { color: #87ffff; }
        .ansi-bright-white { color: #fff; }
        .ansi-bold { font-weight: bold; }
        .ansi-dim { opacity: 0.7; }
        .ansi-italic { font-style: italic; }
        .ansi-underline { text-decoration: underline; }
        .wake-alert {
            background: #FF9800;
            color: #000;
            padding: 15px;
            border-radius: 8px;
            text-align: center;
            font-size: 18px;
            font-weight: bold;
            margin-bottom: 20px;
            animation: pulse 1s infinite;
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.7; }
        }
    </style>
    <script>
        let portsLoaded = false;
        
        function loadPorts() {
            console.log('Loading ports...');
            fetch('/api/ports')
                .then(r => {
                    if (!r.ok) {
                        throw new Error(`HTTP ${r.status}: ${r.statusText}`);
                    }
                    return r.json();
                })
                .then(data => {
                    console.log('Ports data received:', data);
                    const select = document.getElementById('port-select');
                    if (!select) {
                        console.error('port-select element not found!');
                        return;
                    }
                    select.innerHTML = '';
                    
                    if (data.ports && data.ports.length > 0) {
                        data.ports.forEach(port => {
                            const option = document.createElement('option');
                            option.value = port.device;
                            option.textContent = `${port.device}${port.description ? ' - ' + port.description : ''}`;
                            if (port.device === data.current) {
                                option.selected = true;
                            }
                            select.appendChild(option);
                        });
                        console.log(`Loaded ${data.ports.length} ports`);
                    } else {
                        const option = document.createElement('option');
                        option.value = '';
                        option.textContent = 'No ports found';
                        select.appendChild(option);
                        console.warn('No ports found');
                    }
                    
                    portsLoaded = true;
                })
                .catch(e => {
                    console.error('Error loading ports:', e);
                    const select = document.getElementById('port-select');
                    if (select) {
                        select.innerHTML = '<option value="">Error loading ports</option>';
                    }
                });
        }
        
        function refreshPorts() {
            portsLoaded = false;
            loadPorts();
        }
        
        function changePort() {
            const select = document.getElementById('port-select');
            const port = select.value;
            const statusEl = document.getElementById('port-status');
            
            if (!port) {
                statusEl.textContent = '';
                return;
            }
            
            statusEl.textContent = 'Switching port...';
            statusEl.style.color = '#FF9800';
            
            fetch('/api/port', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({port: port})
            })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    statusEl.textContent = `✓ ${data.message}`;
                    statusEl.style.color = '#4CAF50';
                    setTimeout(() => {
                        statusEl.textContent = '';
                    }, 3000);
                } else {
                    statusEl.textContent = `✗ Error: ${data.message || 'Unknown error'}`;
                    statusEl.style.color = '#f44336';
                }
            })
            .catch(e => {
                console.error('Error changing port:', e);
                statusEl.textContent = `✗ Error: ${e.message}`;
                statusEl.style.color = '#f44336';
            });
        }
        
        function rebootDevice() {
            const btn = document.getElementById('reboot-btn');
            const statusEl = document.getElementById('reboot-status');
            btn.disabled = true;
            btn.textContent = 'Rebooting...';
            statusEl.textContent = '';
            console.log('Reboot button clicked');
            
            fetch('/api/reboot', { method: 'POST' })
                .then(r => {
                    if (!r.ok) {
                        return r.json().then(data => {
                            throw new Error(data.message || 'Request failed: ' + r.status);
                        });
                    }
                    return r.json();
                })
                .then(data => {
                    console.log('Reboot response:', data);
                    if (data.success) {
                        statusEl.textContent = '✓ Device rebooting...';
                        statusEl.style.color = '#4CAF50';
                        setTimeout(() => {
                            statusEl.textContent = '';
                            btn.disabled = false;
                            btn.textContent = '🔄 Reboot Device';
                        }, 3000);
                    } else {
                        statusEl.textContent = '✗ Error: ' + (data.message || 'Unknown error');
                        statusEl.style.color = '#f44336';
                        btn.disabled = false;
                        btn.textContent = '🔄 Reboot Device';
                    }
                })
                .catch(e => {
                    console.error('Reboot error:', e);
                    statusEl.textContent = '✗ Error: ' + e.message;
                    statusEl.style.color = '#f44336';
                    btn.disabled = false;
                    btn.textContent = '🔄 Reboot Device';
                });
        }
        
        function spotifyControl(action) {
            const statusEl = document.getElementById('spotify-control-status');
            statusEl.textContent = 'Sending command...';
            statusEl.style.color = '#FF9800';
            
            fetch('/api/spotify/' + action, { method: 'POST' })
                .then(r => {
                    if (!r.ok) {
                        return r.json().then(data => {
                            throw new Error(data.message || 'Request failed: ' + r.status);
                        });
                    }
                    return r.json();
                })
                .then(data => {
                    if (data.success) {
                        statusEl.textContent = '✓ ' + (data.message || 'Command sent');
                        statusEl.style.color = '#4CAF50';
                        setTimeout(() => {
                            statusEl.textContent = '';
                        }, 2000);
                    } else {
                        statusEl.textContent = '✗ ' + (data.message || 'Unknown error');
                        statusEl.style.color = '#f44336';
                        setTimeout(() => {
                            statusEl.textContent = '';
                        }, 3000);
                    }
                })
                .catch(e => {
                    console.error('Spotify control error:', e);
                    statusEl.textContent = '✗ Error: ' + e.message;
                    statusEl.style.color = '#f44336';
                    setTimeout(() => {
                        statusEl.textContent = '';
                    }, 3000);
                });
        }
        
        function toggleSpotifyConfig() {
            const configSection = document.getElementById('spotify-config-section');
            if (configSection) {
                configSection.style.display = configSection.style.display === 'none' ? 'block' : 'none';
            }
        }
        
        function saveSpotifyConfig() {
            const clientId = document.getElementById('spotify-client-id').value.trim();
            const clientSecret = document.getElementById('spotify-client-secret').value.trim();
            const statusEl = document.getElementById('spotify-config-status');
            
            if (!clientId || !clientSecret) {
                if (statusEl) {
                    statusEl.textContent = '✗ Please enter both Client ID and Secret';
                    statusEl.style.color = '#f44336';
                }
                return;
            }
            
            if (statusEl) {
                statusEl.textContent = 'Saving...';
                statusEl.style.color = '#FF9800';
            }
            
            fetch('/api/spotify/config', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({
                    client_id: clientId,
                    client_secret: clientSecret
                })
            })
                .then(r => r.json())
                .then(data => {
                    if (data.success) {
                        if (statusEl) {
                            statusEl.textContent = '✓ Configuration saved! You can now authorize.';
                            statusEl.style.color = '#4CAF50';
                        }
                        // Clear the password field
                        document.getElementById('spotify-client-secret').value = '';
                        // Hide config section after a delay
                        setTimeout(() => {
                            const configSection = document.getElementById('spotify-config-section');
                            if (configSection) configSection.style.display = 'none';
                        }, 2000);
                    } else {
                        if (statusEl) {
                            statusEl.textContent = '✗ Error: ' + (data.message || 'Failed to save');
                            statusEl.style.color = '#f44336';
                        }
                    }
                })
                .catch(e => {
                    console.error('Config save error:', e);
                    if (statusEl) {
                        statusEl.textContent = '✗ Error: ' + e.message;
                        statusEl.style.color = '#f44336';
                    }
                });
        }
        
        // Direct link - no JavaScript needed, but we'll also check status after
        // The link will redirect directly to Spotify
        
        function disconnectSpotify() {
            if (!confirm('Disconnect Spotify account? You will need to re-authorize to use device scanning.')) {
                return;
            }
            
            fetch('/api/spotify/auth/disconnect', { method: 'POST' })
                .then(r => r.json())
                .then(data => {
                    if (data.success) {
                        checkSpotifyAuthStatus();
                    } else {
                        alert('Failed to disconnect: ' + (data.message || 'Unknown error'));
                    }
                })
                .catch(e => {
                    console.error('Disconnect error:', e);
                    alert('Error disconnecting: ' + e.message);
                });
        }
        
        function uploadSpotifyCredentials(event) {
            const file = event.target.files[0];
            if (!file) return;
            
            const statusEl = document.getElementById('spotify-upload-status');
            if (statusEl) {
                statusEl.textContent = 'Reading file...';
                statusEl.style.color = '#FF9800';
            }
            
            const reader = new FileReader();
            reader.onload = function(e) {
                try {
                    const content = e.target.result;
                    let token = null;
                    
                    // Try to parse as JSON first
                    try {
                        const json = JSON.parse(content);
                        // Support various JSON formats
                        token = json.access_token || json.token || json.accessToken || json.accessToken;
                        if (!token && json.credentials) {
                            token = json.credentials.access_token || json.credentials.token;
                        }
                    } catch (jsonErr) {
                        // If not JSON, treat as plain text token
                        token = content.trim();
                    }
                    
                    if (!token) {
                        throw new Error('No access token found in file. Expected JSON with "access_token" field or plain text token.');
                    }
                    
                    // Upload token
                    fetch('/api/spotify/auth/upload', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({access_token: token})
                    })
                        .then(r => r.json())
                        .then(data => {
                            if (data.success) {
                                if (statusEl) {
                                    statusEl.textContent = '✓ Credentials uploaded successfully';
                                    statusEl.style.color = '#4CAF50';
                                }
                                // Reset file input
                                event.target.value = '';
                                // Update auth status
                                setTimeout(() => {
                                    checkSpotifyAuthStatus();
                                    if (statusEl) {
                                        statusEl.textContent = '';
                                    }
                                }, 2000);
                            } else {
                                if (statusEl) {
                                    statusEl.textContent = '✗ Error: ' + (data.message || 'Upload failed');
                                    statusEl.style.color = '#f44336';
                                }
                            }
                        })
                        .catch(e => {
                            console.error('Upload error:', e);
                            if (statusEl) {
                                statusEl.textContent = '✗ Error: ' + e.message;
                                statusEl.style.color = '#f44336';
                            }
                        });
                } catch (err) {
                    if (statusEl) {
                        statusEl.textContent = '✗ Error: ' + err.message;
                        statusEl.style.color = '#f44336';
                    }
                }
            };
            reader.onerror = function() {
                if (statusEl) {
                    statusEl.textContent = '✗ Error reading file';
                    statusEl.style.color = '#f44336';
                }
            };
            reader.readAsText(file);
        }
        
        function checkSpotifyAuthStatus() {
            fetch('/api/spotify/auth/status', { method: 'GET' })
                .then(r => r.json())
                .then(data => {
                    const statusEl = document.getElementById('spotify-auth-text');
                    const authBtn = document.getElementById('spotify-auth-btn');
                    const disconnectBtn = document.getElementById('spotify-disconnect-btn');
                    
                    const linkContainer = document.getElementById('spotify-auth-link-container');
                    
                    const directLink = document.getElementById('spotify-direct-link');
                    
                    if (data.authorized) {
                        if (statusEl) {
                            statusEl.textContent = '✓ Authorized';
                            statusEl.style.color = '#4CAF50';
                        }
                        if (directLink) directLink.style.display = 'none';
                        if (disconnectBtn) disconnectBtn.style.display = 'block';
                        if (linkContainer) linkContainer.style.display = 'none';
                    } else {
                        if (statusEl) {
                            statusEl.textContent = 'Not authorized';
                            statusEl.style.color = '#888';
                        }
                        if (directLink) directLink.style.display = 'block';
                        if (disconnectBtn) disconnectBtn.style.display = 'none';
                        if (linkContainer) linkContainer.style.display = 'none';
                    }
                })
                .catch(e => {
                    console.error('Auth status check error:', e);
                });
        }
        
        function scanSpotifyDevices() {
            const btn = document.getElementById('spotify-scan-btn');
            const devicesList = document.getElementById('spotify-devices-list');
            const statusEl = document.getElementById('spotify-control-status');
            
            if (!btn || !devicesList) return;
            
            btn.disabled = true;
            btn.textContent = 'Scanning...';
            devicesList.innerHTML = '<div style="color: #888; text-align: center; padding: 8px;">Scanning for Spotify devices...</div>';
            if (statusEl) {
                statusEl.textContent = 'Scanning for devices...';
                statusEl.style.color = '#FF9800';
            }
            
            fetch('/api/spotify/devices', { method: 'GET' })
                .then(r => {
                    if (!r.ok) {
                        return r.json().then(data => {
                            throw new Error(data.message || 'Request failed: ' + r.status);
                        });
                    }
                    return r.json();
                })
                .then(data => {
                    btn.disabled = false;
                    btn.textContent = '🔍 Scan for Devices';
                    
                    if (data.success && data.devices && data.devices.length > 0) {
                        devicesList.innerHTML = '';
                        let foundNaphome = false;
                        
                        data.devices.forEach(device => {
                            const isNaphome = device.name && device.name.toLowerCase().includes('naphome');
                            if (isNaphome) foundNaphome = true;
                            
                            const deviceDiv = document.createElement('div');
                            deviceDiv.style.cssText = 'padding: 6px; margin-bottom: 4px; background: ' + 
                                (isNaphome ? '#2a4a2a' : '#222') + '; border-radius: 3px; border-left: 3px solid ' +
                                (isNaphome ? '#1DB954' : '#666') + '; cursor: pointer;';
                            deviceDiv.innerHTML = `
                                <div style="font-weight: ${isNaphome ? 'bold' : 'normal'}; color: ${isNaphome ? '#1DB954' : '#fff'};">
                                    ${isNaphome ? '🎵 ' : ''}${device.name || 'Unknown Device'}
                                </div>
                                <div style="font-size: 9px; color: #888; margin-top: 2px;">
                                    ${device.type || ''} ${device.is_active ? '• Active' : '• Inactive'} ${device.volume_percent !== null ? '• Vol: ' + device.volume_percent + '%' : ''}
                                </div>
                            `;
                            deviceDiv.onclick = () => {
                                if (device.id) {
                                    selectSpotifyDevice(device.id, device.name);
                                }
                            };
                            devicesList.appendChild(deviceDiv);
                        });
                        
                        if (foundNaphome) {
                            if (statusEl) {
                                statusEl.textContent = '✓ Found Naphome device!';
                                statusEl.style.color = '#1DB954';
                            }
                        } else {
                            if (statusEl) {
                                statusEl.textContent = 'Found ' + data.devices.length + ' device(s), but no Naphome';
                                statusEl.style.color = '#FF9800';
                            }
                        }
                    } else {
                        devicesList.innerHTML = '<div style="color: #888; text-align: center; padding: 8px;">No devices found. Make sure Spotify is open and playing.</div>';
                        if (statusEl) {
                            statusEl.textContent = 'No devices found';
                            statusEl.style.color = '#888';
                        }
                    }
                })
                .catch(e => {
                    console.error('Spotify device scan error:', e);
                    btn.disabled = false;
                    btn.textContent = '🔍 Scan for Devices';
                    devicesList.innerHTML = '<div style="color: #f44336; text-align: center; padding: 8px;">Error: ' + e.message + '</div>';
                    if (statusEl) {
                        statusEl.textContent = '✗ Scan failed: ' + e.message;
                        statusEl.style.color = '#f44336';
                    }
                });
        }
        
        function selectSpotifyDevice(deviceId, deviceName) {
            const statusEl = document.getElementById('spotify-control-status');
            if (statusEl) {
                statusEl.textContent = 'Selecting device: ' + deviceName + '...';
                statusEl.style.color = '#FF9800';
            }
            
            fetch('/api/spotify/select_device', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({device_id: deviceId, device_name: deviceName})
            })
                .then(r => r.json())
                .then(data => {
                    if (data.success) {
                        if (statusEl) {
                            statusEl.textContent = '✓ Selected: ' + deviceName;
                            statusEl.style.color = '#1DB954';
                        }
                        // Refresh device list to show selected device
                        setTimeout(() => scanSpotifyDevices(), 1000);
                    } else {
                        if (statusEl) {
                            statusEl.textContent = '✗ Failed to select device';
                            statusEl.style.color = '#f44336';
                        }
                    }
                })
                .catch(e => {
                    console.error('Device selection error:', e);
                    if (statusEl) {
                        statusEl.textContent = '✗ Error: ' + e.message;
                        statusEl.style.color = '#f44336';
                    }
                });
        }
        
        let lastUpdateTime = 0;
        function updateStatus() {
            const now = Date.now();
            fetch('/api/status')
                .then(r => {
                    if (!r.ok) throw new Error('Status request failed: ' + r.status);
                    return r.json();
                })
                .then(data => {
                    // Update serial connection status
                    const serialStatus = document.getElementById('serial-status');
                    const serialIndicator = document.getElementById('serial-indicator');
                    const serialCard = document.getElementById('serial-card');
                    const portName = data.serial_port ? data.serial_port.split('/').pop() : 'unknown';
                    
                    if (data.serial_connected) {
                        serialStatus.textContent = `Connected (${portName})`;
                        serialIndicator.className = 'status-indicator green';
                        serialCard.className = 'status-card';
                    } else {
                        serialStatus.textContent = `Disconnected (${portName}) - Reconnecting...`;
                        serialIndicator.className = 'status-indicator orange';
                        serialCard.className = 'status-card';
                    }
                    
                    // Debug: log status changes
                    if (now - lastUpdateTime > 5000) {  // Log every 5 seconds
                        console.log('Status update:', {
                            serial_connected: data.serial_connected,
                            serial_port: data.serial_port,
                            logs_count: data.logs ? data.logs.length : 0
                        });
                        lastUpdateTime = now;
                    }
                    
                    // Update status values
                    const wifiStatus = document.getElementById('wifi-status');
                    const wifiCard = document.getElementById('wifi-card');
                    const wifiIndicator = document.getElementById('wifi-indicator');
                    if (wifiStatus) {
                        wifiStatus.textContent = data.wifi;
                        // Update WiFi card styling based on status
                        wifiCard.className = 'status-card wifi';
                        if (data.wifi === 'Connected') {
                            wifiCard.classList.add('connected');
                            wifiIndicator.className = 'status-indicator green';
                        } else if (data.wifi === 'Connecting') {
                            wifiCard.classList.add('connecting');
                            wifiIndicator.className = 'status-indicator orange';
                        } else if (data.wifi === 'Failed' || data.wifi === 'Disconnected') {
                            wifiCard.classList.add('disconnected');
                            wifiIndicator.className = 'status-indicator red';
                        } else {
                            wifiIndicator.className = 'status-indicator gray';
                        }
                    }
                    // Update Device Connectivity status
                    // Serial connectivity
                    const connectivitySerialStatus = document.getElementById('connectivity-serial-status');
                    const connectivitySerialIndicator = document.getElementById('connectivity-serial-indicator');
                    const connectivitySerialPort = document.getElementById('connectivity-serial-port');
                    if (connectivitySerialStatus) {
                        if (data.serial_connected) {
                            connectivitySerialStatus.textContent = 'Connected';
                            connectivitySerialIndicator.className = 'status-indicator green';
                            if (connectivitySerialPort && data.serial_port) {
                                const portName = data.serial_port.split('/').pop();
                                connectivitySerialPort.textContent = portName;
                            }
                        } else {
                            connectivitySerialStatus.textContent = 'Disconnected';
                            connectivitySerialIndicator.className = 'status-indicator red';
                            if (connectivitySerialPort) {
                                connectivitySerialPort.textContent = 'Reconnecting...';
                            }
                        }
                    }
                    
                    // WiFi connectivity
                    const connectivityWifiStatus = document.getElementById('connectivity-wifi-status');
                    const connectivityWifiIndicator = document.getElementById('connectivity-wifi-indicator');
                    const connectivityWifiIp = document.getElementById('connectivity-wifi-ip');
                    if (connectivityWifiStatus) {
                        const wifiStatus = data.wifi || 'Unknown';
                        connectivityWifiStatus.textContent = wifiStatus;
                        if (wifiStatus === 'Connected') {
                            connectivityWifiIndicator.className = 'status-indicator green';
                            // Display IP address if available
                            if (connectivityWifiIp) {
                                connectivityWifiIp.textContent = data.wifi_ip || 'Connected';
                            }
                        } else if (wifiStatus === 'Connecting') {
                            connectivityWifiIndicator.className = 'status-indicator orange';
                            if (connectivityWifiIp) {
                                connectivityWifiIp.textContent = 'Connecting...';
                            }
                        } else {
                            connectivityWifiIndicator.className = 'status-indicator red';
                            if (connectivityWifiIp) {
                                connectivityWifiIp.textContent = 'Not connected';
                            }
                        }
                    }
                    
                    // AWS IoT connectivity
                    const connectivityAwsStatus = document.getElementById('connectivity-aws-status');
                    const connectivityAwsIndicator = document.getElementById('connectivity-aws-indicator');
                    const connectivityAwsDevice = document.getElementById('connectivity-aws-device');
                    if (connectivityAwsStatus) {
                        const awsStatus = data.aws || 'Unknown';
                        connectivityAwsStatus.textContent = awsStatus;
                        if (awsStatus === 'Connected') {
                            connectivityAwsIndicator.className = 'status-indicator green';
                            if (connectivityAwsDevice && data.aws_device_id) {
                                connectivityAwsDevice.textContent = data.aws_device_id.substring(0, 20) + '...';
                            }
                        } else if (awsStatus === 'Connecting' || awsStatus === 'Initialized') {
                            connectivityAwsIndicator.className = 'status-indicator orange';
                            if (connectivityAwsDevice) {
                                connectivityAwsDevice.textContent = 'Connecting...';
                            }
                        } else {
                            connectivityAwsIndicator.className = 'status-indicator red';
                            if (connectivityAwsDevice) {
                                connectivityAwsDevice.textContent = 'Not connected';
                            }
                        }
                    }
                    
                    // Sensors connectivity
                    const connectivitySensorsStatus = document.getElementById('connectivity-sensors-status');
                    const connectivitySensorsIndicator = document.getElementById('connectivity-sensors-indicator');
                    const connectivitySensorsCount = document.getElementById('connectivity-sensors-count');
                    if (connectivitySensorsStatus) {
                        const sensors = data.sensors || {};
                        let availableCount = 0;
                        if (sensors.sht45_available) availableCount++;
                        if (sensors.sgp40_available) availableCount++;
                        if (sensors.scd40_available) availableCount++;
                        if (sensors.vcnl4040_available) availableCount++;
                        if (sensors.ec10_available) availableCount++;
                        
                        if (availableCount > 0) {
                            connectivitySensorsStatus.textContent = `${availableCount} Active`;
                            connectivitySensorsIndicator.className = 'status-indicator green';
                            if (connectivitySensorsCount) {
                                const sensorList = [];
                                if (sensors.sht45_available) sensorList.push('SHT45');
                                if (sensors.sgp40_available) sensorList.push('SGP40');
                                if (sensors.scd40_available) sensorList.push('SCD40');
                                if (sensors.vcnl4040_available) sensorList.push('VCNL4040');
                                if (sensors.ec10_available) sensorList.push('EC10');
                                connectivitySensorsCount.textContent = sensorList.join(', ');
                            }
                        } else {
                            connectivitySensorsStatus.textContent = 'None';
                            connectivitySensorsIndicator.className = 'status-indicator red';
                            if (connectivitySensorsCount) {
                                connectivitySensorsCount.textContent = 'No sensors detected';
                            }
                        }
                    }
                    
                    // Update Spotify status
                    const spotifyStatus = document.getElementById('spotify-status');
                    const spotifyCard = document.getElementById('spotify-card');
                    const spotifyIndicator = document.getElementById('spotify-indicator');
                    const spotifyDetail = document.getElementById('spotify-detail');
                    const spotifyDeviceName = document.getElementById('spotify-device-name');
                    const spotifyNowPlaying = document.getElementById('spotify-now-playing');
                    const spotifyTrack = document.getElementById('spotify-track');
                    const spotifyArtist = document.getElementById('spotify-artist');
                    const spotifyVolume = document.getElementById('spotify-volume');
                    
                    if (spotifyStatus) {
                        const spotifyState = data.spotify || 'Unknown';
                        spotifyStatus.textContent = spotifyState;
                        
                        // Update status indicator and card styling
                        if (spotifyState === 'Connected' || spotifyState === 'Playing' || spotifyState === 'Paused' || spotifyState === 'Ready') {
                            spotifyCard.className = 'status-card spotify ready';
                            spotifyIndicator.className = 'status-indicator green';
                        } else if (spotifyState === 'Waiting for Pairing' || spotifyState === 'Pairing Complete' || spotifyState === 'Initializing' || spotifyState === 'Connecting') {
                            spotifyCard.className = 'status-card spotify';
                            spotifyIndicator.className = 'status-indicator orange';
                        } else if (spotifyState === 'Error' || spotifyState === 'Auth Failed' || spotifyState === 'Pairing Failed' || spotifyState === 'Init Failed' || spotifyState === 'cspot Disabled' || spotifyState === 'Disconnected') {
                            spotifyCard.className = 'status-card spotify error';
                            spotifyIndicator.className = 'status-indicator red';
                        } else {
                            spotifyCard.className = 'status-card spotify';
                            spotifyIndicator.className = 'status-indicator gray';
                        }
                        
                        // Update detail and device name
                        if (spotifyDetail && data.spotify_detail) {
                            spotifyDetail.textContent = data.spotify_detail;
                            spotifyDetail.style.display = 'block';
                        } else if (spotifyDetail) {
                            spotifyDetail.style.display = 'none';
                        }
                        
                        if (spotifyDeviceName && data.spotify_device_name) {
                            spotifyDeviceName.textContent = `Device: ${data.spotify_device_name}`;
                            spotifyDeviceName.style.display = 'block';
                        } else if (spotifyDeviceName) {
                            spotifyDeviceName.style.display = 'none';
                        }
                        
                        // Update now playing info
                        if (spotifyNowPlaying && spotifyTrack && spotifyArtist) {
                            if (data.spotify_track) {
                                spotifyNowPlaying.style.display = 'block';
                                spotifyTrack.textContent = data.spotify_track;
                                spotifyArtist.textContent = data.spotify_artist || 'Unknown Artist';
                                
                                if (spotifyVolume && data.spotify_volume !== null && data.spotify_volume !== undefined) {
                                    spotifyVolume.textContent = `Volume: ${data.spotify_volume}%`;
                                } else if (spotifyVolume) {
                                    spotifyVolume.textContent = '';
                                }
                            } else {
                                spotifyNowPlaying.style.display = 'none';
                            }
                        }
                    }
                    
                    // Update AWS IoT status
                    const awsStatusEl = document.getElementById('aws-status');
                    const awsCardEl = document.getElementById('aws-card');
                    const awsIndicatorEl = document.getElementById('aws-indicator');
                    const awsDetailEl = document.getElementById('aws-detail');
                    const awsDeviceIdEl = document.getElementById('aws-device-id');
                    
                    if (awsStatusEl) {
                        awsStatusEl.textContent = data.aws || 'Unknown';
                        if (awsCardEl && awsIndicatorEl) {
                            if (data.aws === 'Connected') {
                                awsCardEl.className = 'status-card aws ready';
                                awsIndicatorEl.className = 'status-indicator green';
                                if (awsDetailEl) awsDetailEl.textContent = 'MQTT connected, telemetry active';
                                if (awsDeviceIdEl && data.aws_device_id) {
                                    awsDeviceIdEl.textContent = `Device: ${data.aws_device_id}`;
                                    awsDeviceIdEl.style.display = 'block';
                                }
                            } else if (data.aws === 'Connecting' || data.aws === 'Initialized') {
                                awsCardEl.className = 'status-card aws';
                                awsIndicatorEl.className = 'status-indicator orange';
                                if (awsDetailEl) awsDetailEl.textContent = 'Connecting to AWS IoT...';
                                if (awsDeviceIdEl) awsDeviceIdEl.style.display = 'none';
                            } else if (data.aws === 'Failed' || data.aws === 'Publish Failed' || data.aws === 'Init Failed' || data.aws === 'Start Failed') {
                                awsCardEl.className = 'status-card aws error';
                                awsIndicatorEl.className = 'status-indicator red';
                                if (awsDetailEl) {
                                    if (data.aws === 'Init Failed') {
                                        awsDetailEl.textContent = 'Initialization failed - check configuration';
                                    } else if (data.aws === 'Start Failed') {
                                        awsDetailEl.textContent = 'Service start failed - check certificates';
                                    } else {
                                        awsDetailEl.textContent = 'Connection failed - check certificates';
                                    }
                                }
                                if (awsDeviceIdEl) awsDeviceIdEl.style.display = 'none';
                            } else if (data.aws === 'Disconnected') {
                                awsCardEl.className = 'status-card aws';
                                awsIndicatorEl.className = 'status-indicator orange';
                                if (awsDetailEl) awsDetailEl.textContent = 'Disconnected - reconnecting...';
                                if (awsDeviceIdEl) awsDeviceIdEl.style.display = 'none';
                            } else {
                                awsCardEl.className = 'status-card aws';
                                awsIndicatorEl.className = 'status-indicator gray';
                                if (awsDetailEl) awsDetailEl.textContent = '';
                                if (awsDeviceIdEl) awsDeviceIdEl.style.display = 'none';
                            }
                        }
                    }
                    
                    document.getElementById('wake-events').textContent = data.stats.wake_events;
                    document.getElementById('wifi-connects').textContent = data.stats.wifi_connects;
                    document.getElementById('errors').textContent = data.stats.errors;
                    
                    // Update error section
                    const errorSection = document.getElementById('errors-section');
                    const errorList = document.getElementById('error-list');
                    const errorCount = document.getElementById('error-count');
                    const errorLog = data.error_log || [];
                    
                    if (errorLog.length > 0) {
                        if (errorSection) errorSection.style.display = 'block';
                        if (errorCount) errorCount.textContent = errorLog.length;
                        if (errorList) {
                            errorList.innerHTML = errorLog.map(err => {
                                return `<div style="margin-bottom: 8px; padding: 8px; background: #1a0a0a; border-left: 3px solid #f44336; border-radius: 4px;">
                                    <div style="color: #888; font-size: 10px; margin-bottom: 4px;">${err.time || 'Unknown time'}</div>
                                    <div style="color: #ff6b6b; white-space: pre-wrap; word-break: break-all;">${escapeHtml(err.message || 'Unknown error')}</div>
                                </div>`;
                            }).join('');
                        }
                    } else {
                        if (errorSection) errorSection.style.display = 'none';
                    }
                    
                    // Handle reboot detection - flash background
                    if (data.reboot_detected) {
                        status['reboot_detected'] = false;  // Reset flag
                        flashBackground();
                    }
                    const geminiErrorsEl = document.getElementById('gemini-errors');
                    if (geminiErrorsEl) geminiErrorsEl.textContent = data.stats.gemini_errors || 0;
                    
                    // Update MQTT status
                    const mqttStatus = data.mqtt || {};
                    const mqttStatusEl = document.getElementById('mqtt-status');
                    const mqttIndicator = document.getElementById('mqtt-indicator');
                    const mqttDeviceId = document.getElementById('mqtt-device-id');
                    const mqttMessages = document.getElementById('mqtt-messages');
                    
                    if (mqttStatusEl) {
                        mqttStatusEl.textContent = mqttStatus.connected ? 'Connected' : 'Disconnected';
                    }
                    if (mqttIndicator) {
                        mqttIndicator.className = mqttStatus.connected ? 'status-indicator green' : 'status-indicator red';
                    }
                    if (mqttDeviceId && mqttStatus.device_id) {
                        mqttDeviceId.textContent = `Device: ${mqttStatus.device_id}`;
                    }
                    if (mqttMessages) {
                        const msgCount = mqttStatus.messages_received || 0;
                        const lastTelemetry = mqttStatus.last_telemetry_time || 'Never';
                        mqttMessages.innerHTML = `Messages: ${msgCount}<br><span style="font-size: 10px; color: #888;">Last telemetry: ${lastTelemetry}</span>`;
                    }
                    
                    // Show subscribed topics
                    const mqttTopics = document.getElementById('mqtt-topics');
                    if (mqttTopics && mqttStatus.subscribed_topics) {
                        if (mqttStatus.subscribed_topics.length > 0) {
                            mqttTopics.innerHTML = mqttStatus.subscribed_topics.map(t => 
                                `<div style="font-size: 10px; color: #4CAF50; margin-top: 4px;">✓ ${t}</div>`
                            ).join('');
                        } else {
                            mqttTopics.innerHTML = '<div style="font-size: 10px; color: #888;">No subscriptions</div>';
                        }
                    }
                    
                    // Update AI provider title
                    const aiProviderTitle = document.getElementById('ai-provider-title');
                    // Always use Gemini (OpenAI removed)
                    const aiProvider = data.ai_provider || 'gemini';
                    if (aiProviderTitle) {
                        aiProviderTitle.textContent = 'Gemini Status';
                    }
                    
                    // Update AI status (Gemini Batch STT-LLM-TTS)
                    const gemini = data.gemini || {};
                    const openaiCard = document.getElementById('openai-card');
                    const openaiIndicator = document.getElementById('openai-indicator');
                    const openaiStatus = document.getElementById('openai-status');
                    const openaiSession = document.getElementById('openai-session');
                    const openaiSessionStatus = document.getElementById('openai-session-status');
                    const openaiWebsocket = document.getElementById('openai-websocket');
                    const openaiWebsocketLabel = document.getElementById('openai-websocket-label');
                    
                    // Gemini uses batch STT, show status based on activity
                    if (openaiWebsocketLabel) openaiWebsocketLabel.textContent = 'Status';
                    if (openaiWebsocket) {
                        // Show Gemini status based on activity
                        if (gemini.transcript_count > 0 || gemini.speech_detected || gemini.llm_processing || gemini.batch_stt_active) {
                            openaiWebsocket.textContent = 'Active';
                        } else {
                            openaiWebsocket.textContent = gemini.status || 'Ready';
                        }
                    }
                    if (openaiStatus) {
                        const geminiStatus = gemini.status || 'Ready';
                        openaiStatus.textContent = geminiStatus;
                        openaiCard.className = 'status-card openai';
                        if (geminiStatus === 'Active' || geminiStatus === 'Processing') {
                            openaiCard.classList.add('connected');
                            openaiIndicator.className = 'status-indicator green';
                        } else if (geminiStatus === 'Ready') {
                            openaiCard.classList.add('disconnected');
                            openaiIndicator.className = 'status-indicator orange';
                        } else {
                            openaiCard.classList.add('disconnected');
                            openaiIndicator.className = 'status-indicator red';
                        }
                    }
                    
                    // Session status (not applicable for Gemini batch processing)
                    if (openaiSession) {
                        openaiSession.textContent = 'Batch STT';
                    }
                    
                    if (openaiSessionStatus) {
                        openaiSessionStatus.textContent = 'Batch STT';
                    }
                    
                    // Update Gemini detailed status
                    const openaiAudioSent = document.getElementById('openai-audio-sent');
                    const openaiAudioFailed = document.getElementById('openai-audio-failed');
                    const openaiTranscripts = document.getElementById('openai-transcripts');
                    const openaiVad = document.getElementById('openai-vad');
                    const openaiLastTranscript = document.getElementById('openai-last-transcript');
                    const openaiTranscriptTime = document.getElementById('openai-transcript-time');
                    const openaiSpeech = document.getElementById('openai-speech');
                    const openaiGpt = document.getElementById('openai-gpt');
                    
                    if (openaiWebsocket) openaiWebsocket.textContent = gemini.status || 'Ready';
                    if (openaiAudioSent) openaiAudioSent.textContent = gemini.audio_accumulated || 0;
                    if (openaiAudioFailed) openaiAudioFailed.textContent = 0; // Gemini doesn't track failed audio
                    if (openaiTranscripts) openaiTranscripts.textContent = gemini.transcript_count || 0;
                    if (openaiVad) openaiVad.textContent = gemini.vad_active ? 'Yes' : 'No';
                    if (openaiLastTranscript) {
                        if (gemini.last_transcript) {
                            openaiLastTranscript.textContent = gemini.last_transcript;
                            openaiLastTranscript.style.color = '#fff';
                        } else {
                            openaiLastTranscript.innerHTML = '<span style="color: #666;">No transcript yet...</span>';
                        }
                    }
                    if (openaiTranscriptTime) {
                        openaiTranscriptTime.textContent = gemini.last_transcript_time ? `Last: ${gemini.last_transcript_time}` : '';
                    }
                    if (openaiSpeech) openaiSpeech.textContent = gemini.speech_detected ? 'Yes' : 'No';
                    if (openaiGpt) openaiGpt.textContent = gemini.llm_processing ? 'Yes' : 'No';
                    
                    // Update AI review status
                    const aiReview = data.ai_review || {};
                    const reviewIndicator = document.getElementById('ai-review-indicator');
                    const reviewTime = document.getElementById('ai-review-time');
                    const alertDiv = document.getElementById('ai-alerts');
                    const alertContent = document.getElementById('ai-alert-content');
                    const suggestionDiv = document.getElementById('ai-suggestions');
                    const suggestionContent = document.getElementById('ai-suggestion-content');
                    
                    if (reviewTime) {
                        reviewTime.textContent = aiReview.last_review_time || 'Never';
                        if (reviewIndicator) {
                            reviewIndicator.style.color = aiReview.reviewing ? '#FF9800' : 
                                                         (aiReview.last_review_time ? '#4CAF50' : '#666');
                        }
                    }
                    
                    // Show alerts
                    if (aiReview.alerts && aiReview.alerts.length > 0) {
                        if (alertDiv && alertContent) {
                            alertDiv.style.display = 'block';
                            alertContent.textContent = aiReview.alerts[aiReview.alerts.length - 1];
                        }
                    } else {
                        if (alertDiv) alertDiv.style.display = 'none';
                    }
                    
                    // Show suggestions
                    if (aiReview.suggestions && aiReview.suggestions.length > 0) {
                        if (suggestionDiv && suggestionContent) {
                            suggestionDiv.style.display = 'block';
                            suggestionContent.textContent = aiReview.suggestions[aiReview.suggestions.length - 1];
                        }
                    } else {
                        if (suggestionDiv) suggestionDiv.style.display = 'none';
                    }
                    
                    // Update LED states
                    const ledNames = ['WIFI', 'SPOTIFY', 'AWS', 'WAKE_WORD', 'MUTE', 'AUDIO_PLAYBACK'];
                    ledNames.forEach(ledName => {
                        const ledData = data.leds && data.leds[ledName] ? data.leds[ledName] : {r: 0, g: 0, b: 0, state: 'OFF'};
                        const ledId = ledName.toLowerCase().replace(/_/g, '-');
                        const colorEl = document.getElementById(`led-${ledId}-color`);
                        const rgbEl = document.getElementById(`led-${ledId}-rgb`);
                        if (colorEl && rgbEl) {
                            const r = ledData.r || 0;
                            const g = ledData.g || 0;
                            const b = ledData.b || 0;
                            const rgbStr = `rgb(${r},${g},${b})`;
                            colorEl.style.background = rgbStr;
                            rgbEl.textContent = `RGB(${r},${g},${b})`;
                        }
                    });
                    
                    // Update Sensor Readings
                    const sensors = data.sensors || {};
                    
                    // Update sensor status indicators
                    const updateSensorStatus = (sensorId, available) => {
                        const statusEl = document.getElementById('sensor-status-' + sensorId);
                        if (statusEl) {
                            if (available === true) {
                                statusEl.textContent = '✓ Present';
                                statusEl.style.color = '#4CAF50';
                            } else if (available === false) {
                                statusEl.textContent = '⚠ Synthetic';
                                statusEl.style.color = '#FF9800';
                            } else {
                                statusEl.textContent = '--';
                                statusEl.style.color = '#666';
                            }
                        }
                    };
                    
                    updateSensorStatus('sht45', sensors.sht45_available);
                    updateSensorStatus('sgp40', sensors.sgp40_available);
                    updateSensorStatus('scd40', sensors.scd40_available);
                    updateSensorStatus('vcnl4040', sensors.vcnl4040_available);
                    updateSensorStatus('ec10', sensors.ec10_available);
                    
                    const tempEl = document.getElementById('sensor-temperature');
                    const humEl = document.getElementById('sensor-humidity');
                    const co2El = document.getElementById('sensor-co2');
                    const vocEl = document.getElementById('sensor-voc');
                    const luxEl = document.getElementById('sensor-lux');
                    const pm25El = document.getElementById('sensor-pm25');
                    
                    if (tempEl) {
                        if (sensors.temperature_c !== null && sensors.temperature_c !== undefined) {
                            tempEl.textContent = sensors.temperature_c.toFixed(1) + '°C';
                            const tempSource = document.getElementById('sensor-temp-source');
                            if (tempSource) {
                                if (sensors.sht45_available) {
                                    tempSource.textContent = '✓ SHT45 (Real)';
                                    tempSource.style.color = '#4CAF50';
                                } else {
                                    tempSource.textContent = '⚠ Synthetic Data';
                                    tempSource.style.color = '#FF9800';
                                }
                            }
                        } else {
                            tempEl.textContent = '--';
                        }
                    }
                    if (humEl) {
                        if (sensors.humidity_rh !== null && sensors.humidity_rh !== undefined) {
                            humEl.textContent = sensors.humidity_rh.toFixed(1) + '%';
                            const humSource = document.getElementById('sensor-hum-source');
                            if (humSource) {
                                if (sensors.sht45_available) {
                                    humSource.textContent = '✓ SHT45 (Real)';
                                    humSource.style.color = '#4CAF50';
                                } else {
                                    humSource.textContent = '⚠ Synthetic Data';
                                    humSource.style.color = '#FF9800';
                                }
                            }
                        } else {
                            humEl.textContent = '--';
                        }
                    }
                    if (co2El) {
                        if (sensors.co2_ppm !== null && sensors.co2_ppm !== undefined) {
                            co2El.textContent = Math.round(sensors.co2_ppm) + ' ppm';
                            const co2Source = document.getElementById('sensor-co2-source');
                            if (co2Source) {
                                if (sensors.scd40_available) {
                                    co2Source.textContent = '✓ SCD40 (Real)';
                                    co2Source.style.color = '#4CAF50';
                                } else {
                                    co2Source.textContent = '⚠ Synthetic Data';
                                    co2Source.style.color = '#FF9800';
                                }
                            }
                        } else {
                            co2El.textContent = '--';
                        }
                    }
                    if (vocEl) {
                        if (sensors.voc_index !== null && sensors.voc_index !== undefined) {
                            vocEl.textContent = sensors.voc_index;
                            const vocSource = document.getElementById('sensor-voc-source');
                            if (vocSource) {
                                if (sensors.sgp40_available) {
                                    vocSource.textContent = '✓ SGP40 (Real)';
                                    vocSource.style.color = '#4CAF50';
                                } else {
                                    vocSource.textContent = '⚠ Synthetic Data';
                                    vocSource.style.color = '#FF9800';
                                }
                            }
                        } else {
                            vocEl.textContent = '--';
                        }
                    }
                    if (luxEl) {
                        if (sensors.ambient_lux !== null && sensors.ambient_lux !== undefined) {
                            luxEl.textContent = sensors.ambient_lux + ' lux';
                            const luxSource = document.getElementById('sensor-lux-source');
                            if (luxSource) {
                                if (sensors.vcnl4040_available) {
                                    luxSource.textContent = '✓ VCNL4040 (Real)';
                                    luxSource.style.color = '#4CAF50';
                                } else {
                                    luxSource.textContent = '⚠ Synthetic Data';
                                    luxSource.style.color = '#FF9800';
                                }
                            }
                        } else {
                            luxEl.textContent = '--';
                        }
                    }
                    if (pm25El) {
                        if (sensors.pm2_5_ug_m3 !== null && sensors.pm2_5_ug_m3 !== undefined) {
                            pm25El.textContent = Math.round(sensors.pm2_5_ug_m3) + ' μg/m³';
                            const pmSource = document.getElementById('sensor-pm-source');
                            if (pmSource) {
                                if (sensors.ec10_available) {
                                    pmSource.textContent = '✓ EC10 (Real)';
                                    pmSource.style.color = '#4CAF50';
                                } else {
                                    pmSource.textContent = '⚠ Synthetic Data';
                                    pmSource.style.color = '#FF9800';
                                }
                            }
                        } else {
                            pm25El.textContent = '--';
                        }
                    }
                    
                    // Update I2C Bus Scan display
                    const i2cScan = data.i2c_scan || {};
                    const sensorBus = i2cScan.sensor_bus || {};
                    const audioBus = i2cScan.audio_bus || {};
                    
                    // Update Sensor Bus
                    const sensorSdaEl = document.getElementById('i2c-sensor-sda');
                    const sensorSclEl = document.getElementById('i2c-sensor-scl');
                    const sensorStatusEl = document.getElementById('i2c-sensor-status');
                    const sensorDevicesEl = document.getElementById('i2c-sensor-devices');
                    const sensorTimeEl = document.getElementById('i2c-sensor-time');
                    
                    if (sensorSdaEl) sensorSdaEl.textContent = sensorBus.sda || 44;
                    if (sensorSclEl) sensorSclEl.textContent = sensorBus.scl || 43;
                    if (sensorStatusEl) {
                        sensorStatusEl.textContent = sensorBus.status || 'Not scanned';
                        if (sensorBus.status === 'Complete') {
                            sensorStatusEl.style.color = '#4CAF50';
                        } else if (sensorBus.status === 'Scanning') {
                            sensorStatusEl.style.color = '#FF9800';
                        } else if (sensorBus.status === 'Failed') {
                            sensorStatusEl.style.color = '#f44336';
                        } else {
                            sensorStatusEl.style.color = '#888';
                        }
                    }
                    if (sensorDevicesEl) {
                        if (sensorBus.devices && sensorBus.devices.length > 0) {
                            sensorDevicesEl.textContent = sensorBus.devices.map(addr => '0x' + addr.toString(16).toUpperCase().padStart(2, '0')).join(', ');
                            sensorDevicesEl.style.color = '#4CAF50';
                        } else {
                            sensorDevicesEl.textContent = 'None';
                            sensorDevicesEl.style.color = '#888';
                        }
                    }
                    if (sensorTimeEl) {
                        sensorTimeEl.textContent = sensorBus.scan_time ? `Scanned: ${sensorBus.scan_time}` : '';
                    }
                    
                    // Update Audio Bus
                    const audioSdaEl = document.getElementById('i2c-audio-sda');
                    const audioSclEl = document.getElementById('i2c-audio-scl');
                    const audioStatusEl = document.getElementById('i2c-audio-status');
                    const audioDevicesEl = document.getElementById('i2c-audio-devices');
                    const audioTimeEl = document.getElementById('i2c-audio-time');
                    
                    if (audioSdaEl) audioSdaEl.textContent = audioBus.sda || 1;
                    if (audioSclEl) audioSclEl.textContent = audioBus.scl || 2;
                    if (audioStatusEl) {
                        audioStatusEl.textContent = audioBus.status || 'Not scanned';
                        if (audioBus.status === 'Complete') {
                            audioStatusEl.style.color = '#4CAF50';
                        } else if (audioBus.status === 'Scanning') {
                            audioStatusEl.style.color = '#FF9800';
                        } else if (audioBus.status === 'Failed') {
                            audioStatusEl.style.color = '#f44336';
                        } else {
                            audioStatusEl.style.color = '#888';
                        }
                    }
                    if (audioDevicesEl) {
                        if (audioBus.devices && audioBus.devices.length > 0) {
                            audioDevicesEl.textContent = audioBus.devices.map(addr => '0x' + addr.toString(16).toUpperCase().padStart(2, '0')).join(', ');
                            audioDevicesEl.style.color = '#4CAF50';
                        } else {
                            audioDevicesEl.textContent = 'None';
                            audioDevicesEl.style.color = '#888';
                        }
                    }
                    if (audioTimeEl) {
                        audioTimeEl.textContent = audioBus.scan_time ? `Scanned: ${audioBus.scan_time}` : '';
                    }
                    
                    // Update I2C Conflicts
                    const conflictsDiv = document.getElementById('i2c-conflicts');
                    const conflictsList = document.getElementById('i2c-conflicts-list');
                    if (conflictsDiv && conflictsList) {
                        if (i2cScan.conflicts && i2cScan.conflicts.length > 0) {
                            conflictsDiv.style.display = 'block';
                            conflictsList.innerHTML = i2cScan.conflicts.map(c => `<div style="margin: 4px 0;">• ${c}</div>`).join('');
                        } else {
                            conflictsDiv.style.display = 'none';
                        }
                    }
                    
                    // Update wake word alert
                    if (data.wake_word) {
                        document.getElementById('wake-alert').style.display = 'block';
                    } else {
                        document.getElementById('wake-alert').style.display = 'none';
                    }
                    
                    // Update logs
                    const logsContainer = document.getElementById('logs-container');
                    const logsCountEl = document.getElementById('logs-count');
                    if (logsContainer && data.logs && Array.isArray(data.logs)) {
                        if (logsCountEl) logsCountEl.textContent = data.logs.length;
                        
                        // Track scroll state before mutating DOM so we can avoid unexpected jumps
                        const previousScrollTop = logsContainer.scrollTop;
                        const previousMaxScrollTop = Math.max(0, logsContainer.scrollHeight - logsContainer.clientHeight);
                        const wasAtBottom = previousMaxScrollTop - previousScrollTop < 50;
                        
                        logsContainer.innerHTML = '';
                        // Show last 100 entries for performance, but keep all in memory
                        data.logs.slice(-100).forEach(log => {
                            const entry = document.createElement('div');
                            entry.className = 'log-entry';
                            
                            // Check for error/wake/success keywords (but don't override ANSI colors)
                            const logTextLower = (log.text || '').toLowerCase();
                            if (logTextLower.includes('error') || logTextLower.includes('failed')) {
                                entry.classList.add('error');
                            } else if (logTextLower.includes('wake') || logTextLower.includes('detected')) {
                                entry.classList.add('wake');
                            } else if (logTextLower.includes('connected') || logTextLower.includes('ready')) {
                                entry.classList.add('success');
                            } else if (logTextLower.includes('openai') || logTextLower.includes('realtime') || logTextLower.includes('gemini')) {
                                entry.classList.add('openai');
                            } else if (logTextLower.includes('transcript') || logTextLower.includes('response.audio_transcript')) {
                                entry.classList.add('transcript');
                            } else if (logTextLower.includes('vad') && (logTextLower.includes('detected') || logTextLower.includes('active'))) {
                                entry.classList.add('vad');
                            }
                            
                            // Convert ANSI codes to HTML (if any)
                            const logText = log.text || '';
                            const htmlText = ansiToHtml(logText);
                            entry.innerHTML = `<span class="log-time">[${log.time || '--'}]</span>${htmlText}`;
                            logsContainer.appendChild(entry);
                        });
                        
                        // Only auto-scroll to bottom if user was already at/near the bottom
                        if (wasAtBottom) {
                            logsContainer.scrollTop = logsContainer.scrollHeight;
                        } else {
                            const newMaxScrollTop = Math.max(0, logsContainer.scrollHeight - logsContainer.clientHeight);
                            logsContainer.scrollTop = Math.min(previousScrollTop, newMaxScrollTop);
                        }
                    } else if (logsCountEl) {
                        logsCountEl.textContent = '0';
                    }
                })
                .catch(e => {
                    console.error('Status update error:', e);
                    // Show error in serial status
                    const serialStatus = document.getElementById('serial-status');
                    if (serialStatus) {
                        serialStatus.textContent = 'Error fetching status';
                    }
                });
        }
        // ANSI to HTML converter (JavaScript version)
        function ansiToHtml(text) {
            if (!text) return text;
            
            const colorMap = {
                30: 'black', 31: 'red', 32: 'green', 33: 'yellow', 34: 'blue',
                35: 'magenta', 36: 'cyan', 37: 'white',
                90: 'bright-black', 91: 'bright-red', 92: 'bright-green', 93: 'bright-yellow',
                94: 'bright-blue', 95: 'bright-magenta', 96: 'bright-cyan', 97: 'bright-white',
            };
            
            // Match ANSI escape sequences: \033[ or \x1b[ followed by codes and 'm'
                const ansiPattern = /\\033\\[([0-9;]+)m|\\x1b\\[([0-9;]+)m/g;
            let result = '';
            let lastIndex = 0;
            let openSpans = [];
            
            let match;
            while ((match = ansiPattern.exec(text)) !== null) {
                // Add text before the ANSI code
                if (match.index > lastIndex) {
                    result += escapeHtml(text.substring(lastIndex, match.index));
                }
                
                // Parse the ANSI code (use match[1] or match[2] depending on which pattern matched)
                const codesStr = match[1] || match[2];
                const codes = codesStr.split(';').map(c => parseInt(c, 10));
                const classes = [];
                
                for (const code of codes) {
                    if (code === 0) {
                        // Reset - close all open spans
                        if (openSpans.length > 0) {
                            result += '</span>'.repeat(openSpans.length);
                            openSpans = [];
                        }
                    } else if (code === 1) {
                        classes.push('ansi-bold');
                    } else if (code === 2) {
                        classes.push('ansi-dim');
                    } else if (code === 3) {
                        classes.push('ansi-italic');
                    } else if (code === 4) {
                        classes.push('ansi-underline');
                    } else if (colorMap[code]) {
                        classes.push('ansi-' + colorMap[code]);
                    }
                }
                
                if (classes.length > 0) {
                    result += '<span class="' + classes.join(' ') + '">';
                    openSpans.push(classes);
                }
                
                lastIndex = match.index + match[0].length;
            }
            
            // Add remaining text
            if (lastIndex < text.length) {
                result += escapeHtml(text.substring(lastIndex));
            }
            
            // Close any unclosed spans
            if (openSpans.length > 0) {
                result += '</span>'.repeat(openSpans.length);
            }
            
            return result;
        }
        
        function escapeHtml(text) {
            const div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
        }
        
        // Load ports on page load (wait for DOM to be ready)
        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', loadPorts);
        } else {
            // DOM is already ready
            loadPorts();
        }
        // Refresh ports every 10 seconds
        setInterval(refreshPorts, 10000);
        
        // Periodic auto-review every 30 seconds (more frequent)
        setInterval(() => {
            console.log('Triggering periodic auto-review...');
            fetch('/api/ai/auto-review', {method: 'POST'})
                .then(r => r.json())
                .then(data => {
                    console.log('Auto-review response:', data);
                })
                .catch(e => console.error('Auto-review trigger failed:', e));
        }, 30000);  // Every 30 seconds
        
        setInterval(updateStatus, 500);
        updateStatus();
        
        // Spotify functions removed - UI no longer displays Spotify player
        
        // Copy alert function
        function copyAlert() {
            const alertContent = document.getElementById('ai-alert-content');
            if (!alertContent) return;
            
            const text = alertContent.textContent || alertContent.innerText;
            if (!text) return;
            
            navigator.clipboard.writeText(text).then(() => {
                const btn = document.getElementById('copy-alert-btn');
                if (btn) {
                    const originalText = btn.textContent;
                    btn.textContent = '✓ Copied!';
                    btn.style.background = '#4CAF50';
                    setTimeout(() => {
                        btn.textContent = originalText;
                        btn.style.background = '#444';
                    }, 2000);
                }
            }).catch(err => {
                console.error('Failed to copy:', err);
                // Fallback for older browsers
                const textArea = document.createElement('textarea');
                textArea.value = text;
                textArea.style.position = 'fixed';
                textArea.style.opacity = '0';
                document.body.appendChild(textArea);
                textArea.select();
                try {
                    document.execCommand('copy');
                    const btn = document.getElementById('copy-alert-btn');
                    if (btn) {
                        const originalText = btn.textContent;
                        btn.textContent = '✓ Copied!';
                        btn.style.background = '#4CAF50';
                        setTimeout(() => {
                            btn.textContent = originalText;
                            btn.style.background = '#444';
                        }, 2000);
                    }
                } catch (err) {
                    console.error('Fallback copy failed:', err);
                }
                document.body.removeChild(textArea);
            });
        }
    </script>
</head>
<body>
        <div class="container">
        <h1>🎤 Naphome Voice Assistant Dashboard</h1>
        
        <div style="text-align: center; margin-bottom: 20px;">
            <div style="display: inline-flex; align-items: center; gap: 15px; flex-wrap: wrap; justify-content: center;">
                <div style="display: flex; align-items: center; gap: 10px;">
                    <label for="port-select" style="color: #aaa; font-size: 14px;">Serial Port:</label>
                    <select id="port-select" onchange="changePort()" style="
                        background: #2a2a2a;
                        color: #fff;
                        border: 1px solid #444;
                        padding: 8px 12px;
                        border-radius: 5px;
                        font-size: 14px;
                        cursor: pointer;
                        min-width: 200px;
                    ">
                        <option value="">Loading ports...</option>
                    </select>
                    <button onclick="refreshPorts()" style="
                        background: #2196F3;
                        color: white;
                        border: none;
                        padding: 8px 15px;
                        border-radius: 5px;
                        cursor: pointer;
                        font-size: 12px;
                    ">🔄 Refresh</button>
                </div>
                <button id="reboot-btn" onclick="rebootDevice()" style="
                    background: #f44336;
                    color: white;
                    border: none;
                    padding: 10px 20px;
                    border-radius: 5px;
                    cursor: pointer;
                    font-size: 14px;
                    font-weight: bold;
                ">🔄 Reboot Device</button>
                <span id="reboot-status" style="margin-left: 10px; color: #4CAF50;"></span>
            </div>
            <div id="port-status" style="margin-top: 10px; font-size: 12px; color: #666;"></div>
        </div>
        
        <div id="wake-alert" class="wake-alert" style="display: none;">
            ⚡ WAKE WORD DETECTED! ⚡
        </div>
        
        <div class="status-grid">
            <div class="status-card" id="serial-card">
                <div class="status-title">Serial Port</div>
                <div class="status-value">
                    <span class="status-indicator" id="serial-indicator"></span>
                    <span id="serial-status">Connecting...</span>
                </div>
            </div>
            
            <div class="status-card wifi" id="wifi-card">
                <div class="status-title">Wi-Fi</div>
                <div class="status-value">
                    <span class="status-indicator" id="wifi-indicator"></span>
                    <span id="wifi-status">Unknown</span>
                </div>
            </div>
            
            <div class="status-card" id="connectivity-card" style="border-left: 4px solid #2196F3;">
                <div class="status-title">Device Connectivity</div>
                <div style="display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; margin-top: 10px;">
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">Serial</div>
                        <div style="display: flex; align-items: center; gap: 6px;">
                            <span class="status-indicator" id="connectivity-serial-indicator"></span>
                            <span style="font-size: 12px; color: #fff;" id="connectivity-serial-status">--</span>
                        </div>
                        <div style="font-size: 9px; color: #666; margin-top: 2px;" id="connectivity-serial-port">--</div>
                    </div>
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">WiFi</div>
                        <div style="display: flex; align-items: center; gap: 6px;">
                            <span class="status-indicator" id="connectivity-wifi-indicator"></span>
                            <span style="font-size: 12px; color: #fff;" id="connectivity-wifi-status">--</span>
                        </div>
                        <div style="font-size: 9px; color: #666; margin-top: 2px;" id="connectivity-wifi-ip">--</div>
                    </div>
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">AWS IoT</div>
                        <div style="display: flex; align-items: center; gap: 6px;">
                            <span class="status-indicator" id="connectivity-aws-indicator"></span>
                            <span style="font-size: 12px; color: #fff;" id="connectivity-aws-status">--</span>
                        </div>
                        <div style="font-size: 9px; color: #666; margin-top: 2px;" id="connectivity-aws-device">--</div>
                    </div>
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">Sensors</div>
                        <div style="display: flex; align-items: center; gap: 6px;">
                            <span class="status-indicator" id="connectivity-sensors-indicator"></span>
                            <span style="font-size: 12px; color: #fff;" id="connectivity-sensors-status">--</span>
                        </div>
                        <div style="font-size: 9px; color: #666; margin-top: 2px;" id="connectivity-sensors-count">--</div>
                    </div>
                </div>
            </div>
            
            <div class="status-card spotify" id="spotify-card">
                <div class="status-title">Spotify Connect</div>
                <div class="status-value">
                    <span class="status-indicator" id="spotify-indicator"></span>
                    <span id="spotify-status">Unknown</span>
                </div>
                <div id="spotify-detail" style="margin-top: 4px; font-size: 11px; color: #888; min-height: 14px;"></div>
                <div id="spotify-device-name" style="margin-top: 2px; font-size: 10px; color: #666; font-style: italic;"></div>
                
                <!-- Now Playing Info -->
                <div id="spotify-now-playing" style="margin-top: 12px; padding: 8px; background: #1a1a1a; border-radius: 4px; display: none;">
                    <div style="font-size: 11px; color: #888; margin-bottom: 4px;">Now Playing</div>
                    <div id="spotify-track" style="font-size: 13px; color: #fff; font-weight: 500;"></div>
                    <div id="spotify-artist" style="font-size: 11px; color: #aaa; margin-top: 2px;"></div>
                    <div id="spotify-volume" style="font-size: 10px; color: #666; margin-top: 4px;"></div>
                </div>
            </div>
            
            <div class="status-card aws" id="aws-card">
                <div class="status-title">AWS IoT</div>
                <div class="status-value">
                    <span class="status-indicator" id="aws-indicator"></span>
                    <span id="aws-status">Unknown</span>
                </div>
                <div id="aws-detail" style="margin-top: 4px; font-size: 11px; color: #888; min-height: 14px;"></div>
                <div id="aws-device-id" style="margin-top: 2px; font-size: 10px; color: #666; font-style: italic;"></div>
            </div>
            
            <div class="status-card">
                <div class="status-title">MQTT Subscription</div>
                <div class="status-value">
                    <span class="status-indicator gray" id="mqtt-indicator"></span>
                    <span id="mqtt-status">Disconnected</span>
                </div>
                <div style="font-size: 11px; color: #aaa; margin-top: 8px;" id="mqtt-device-id"></div>
                <div style="font-size: 11px; color: #aaa; margin-top: 4px;" id="mqtt-messages">Messages: 0</div>
                <div style="font-size: 10px; color: #666; margin-top: 8px; max-height: 60px; overflow-y: auto;" id="mqtt-topics"></div>
            </div>
            
            <div class="status-card openai" id="openai-card">
                <div class="status-title">AI Provider</div>
                <div class="status-value">
                    <span class="status-indicator" id="openai-indicator"></span>
                    <span id="openai-status">Disconnected</span>
                </div>
                <div style="font-size: 11px; color: #aaa; margin-top: 8px;">
                    Session: <span id="openai-session">None</span>
                </div>
            </div>
        </div>
        
        <div class="status-grid" style="margin-top: 20px;">
            <div class="status-card openai">
                <div class="status-title" id="ai-provider-title">AI Status (Detecting...)</div>
                <div style="display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; margin-top: 10px;">
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;" id="openai-websocket-label">Connection</div>
                        <div style="font-size: 14px; color: #fff;" id="openai-websocket">Disconnected</div>
                    </div>
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">Session</div>
                        <div style="font-size: 14px; color: #fff;" id="openai-session-status">None</div>
                    </div>
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">Audio Sent</div>
                        <div style="font-size: 14px; color: #4CAF50;" id="openai-audio-sent">0</div>
                    </div>
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">Audio Failed</div>
                        <div style="font-size: 14px; color: #f44336;" id="openai-audio-failed">0</div>
                    </div>
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">Transcripts</div>
                        <div style="font-size: 14px; color: #2196F3;" id="openai-transcripts">0</div>
                    </div>
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">VAD Active</div>
                        <div style="font-size: 14px; color: #FF9800;" id="openai-vad">No</div>
                    </div>
                </div>
            </div>
            
            <div class="status-card openai">
                <div class="status-title">Speech Recognition</div>
                <div style="padding: 15px; background: #1a1a1a; border-radius: 4px; margin-top: 10px;">
                    <div style="font-size: 11px; color: #aaa; margin-bottom: 8px;">Last Transcript</div>
                    <div style="font-size: 13px; color: #fff; min-height: 40px; word-wrap: break-word;" id="openai-last-transcript">
                        <span style="color: #666;">No transcript yet...</span>
                    </div>
                    <div style="font-size: 10px; color: #666; margin-top: 8px;" id="openai-transcript-time"></div>
                </div>
                <div style="display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; margin-top: 10px;">
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">Speech Detected</div>
                        <div style="font-size: 14px; color: #4CAF50;" id="openai-speech">No</div>
                    </div>
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">GPT Processing</div>
                        <div style="font-size: 14px; color: #2196F3;" id="openai-gpt">No</div>
                    </div>
                </div>
            </div>
        </div>
        
        <div class="status-grid" style="margin-top: 20px;">
            <div class="status-card">
                <div class="status-title">LED States</div>
                <div style="display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; margin-top: 10px;">
                    <div id="led-wifi" style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">WiFi</div>
                        <div style="display: flex; align-items: center; gap: 8px;">
                            <div id="led-wifi-color" style="width: 20px; height: 20px; border-radius: 50%; background: #000; border: 1px solid #444;"></div>
                            <div id="led-wifi-rgb" style="font-size: 10px; color: #888;">RGB(0,0,0)</div>
                        </div>
                    </div>
                    <div id="led-spotify" style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">Spotify</div>
                        <div style="display: flex; align-items: center; gap: 8px;">
                            <div id="led-spotify-color" style="width: 20px; height: 20px; border-radius: 50%; background: #000; border: 1px solid #444;"></div>
                            <div id="led-spotify-rgb" style="font-size: 10px; color: #888;">RGB(0,0,0)</div>
                        </div>
                    </div>
                    <div id="led-aws" style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">AWS</div>
                        <div style="display: flex; align-items: center; gap: 8px;">
                            <div id="led-aws-color" style="width: 20px; height: 20px; border-radius: 50%; background: #000; border: 1px solid #444;"></div>
                            <div id="led-aws-rgb" style="font-size: 10px; color: #888;">RGB(0,0,0)</div>
                        </div>
                    </div>
                    <div id="led-wake-word" style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">Wake Word</div>
                        <div style="display: flex; align-items: center; gap: 8px;">
                            <div id="led-wake-word-color" style="width: 20px; height: 20px; border-radius: 50%; background: #000; border: 1px solid #444;"></div>
                            <div id="led-wake-word-rgb" style="font-size: 10px; color: #888;">RGB(0,0,0)</div>
                        </div>
                    </div>
                    <div id="led-mute" style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">Mute</div>
                        <div style="display: flex; align-items: center; gap: 8px;">
                            <div id="led-mute-color" style="width: 20px; height: 20px; border-radius: 50%; background: #000; border: 1px solid #444;"></div>
                            <div id="led-mute-rgb" style="font-size: 10px; color: #888;">RGB(0,0,0)</div>
                        </div>
                    </div>
                    <div id="led-audio-playback" style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 11px; color: #aaa; margin-bottom: 4px;">Audio</div>
                        <div style="display: flex; align-items: center; gap: 8px;">
                            <div id="led-audio-playback-color" style="width: 20px; height: 20px; border-radius: 50%; background: #000; border: 1px solid #444;"></div>
                            <div id="led-audio-playback-rgb" style="font-size: 10px; color: #888;">RGB(0,0,0)</div>
                        </div>
                    </div>
                </div>
            </div>
        </div>
        
        <div class="stats">
            <div class="stats-grid">
                <div class="stat-item">
                    <div class="stat-value" id="wake-events">0</div>
                    <div class="stat-label">Wake Events</div>
                </div>
                <div class="stat-item">
                    <div class="stat-value" id="wifi-connects">0</div>
                    <div class="stat-label">Wi-Fi Connects</div>
                </div>
                <div class="stat-item">
                    <div class="stat-value" id="errors">0</div>
                    <div class="stat-label">Errors</div>
                </div>
                <div class="stat-item">
                    <div class="stat-value" id="gemini-errors">0</div>
                    <div class="stat-label">Gemini Errors</div>
                </div>
            </div>
        </div>
        
        <div class="status-grid" style="margin-top: 20px;">
            <div class="status-card" style="border-left: 4px solid #9C27B0;">
                <div class="status-title">I2C Bus Scan</div>
                <div style="margin-bottom: 12px;">
                    <div style="font-size: 11px; color: #888; margin-bottom: 8px;">Sensor Bus (I2C_NUM_0)</div>
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px; margin-bottom: 8px;">
                        <div style="font-size: 10px; color: #aaa; margin-bottom: 4px;">
                            GPIO: SDA=<span id="i2c-sensor-sda">44</span> (RXD), SCL=<span id="i2c-sensor-scl">43</span> (TXD)
                            <span style="margin-left: 12px; color: #888;">Status: <span id="i2c-sensor-status">Not scanned</span></span>
                        </div>
                        <div style="font-size: 12px; color: #fff;">
                            Devices: <span id="i2c-sensor-devices" style="font-family: monospace;">--</span>
                            <span style="font-size: 10px; color: #888; margin-left: 8px;" id="i2c-sensor-time"></span>
                        </div>
                    </div>
                    <div style="font-size: 11px; color: #888; margin-bottom: 8px;">Audio Bus (I2C_NUM_1)</div>
                    <div style="padding: 8px; background: #1a1a1a; border-radius: 4px;">
                        <div style="font-size: 10px; color: #aaa; margin-bottom: 4px;">
                            GPIO: SDA=<span id="i2c-audio-sda">1</span>, SCL=<span id="i2c-audio-scl">2</span>
                            <span style="margin-left: 12px; color: #888;">Status: <span id="i2c-audio-status">Not scanned</span></span>
                        </div>
                        <div style="font-size: 12px; color: #fff;">
                            Devices: <span id="i2c-audio-devices" style="font-family: monospace;">--</span>
                            <span style="font-size: 10px; color: #888; margin-left: 8px;" id="i2c-audio-time"></span>
                        </div>
                    </div>
                </div>
                <div id="i2c-conflicts" style="display: none; margin-top: 12px; padding: 8px; background: #3a1a1a; border-radius: 4px; border-left: 3px solid #f44336;">
                    <div style="font-size: 11px; color: #f44336; margin-bottom: 4px; font-weight: bold;">⚠ I2C Conflicts Detected</div>
                    <div style="font-size: 10px; color: #ffaaaa;" id="i2c-conflicts-list"></div>
                </div>
            </div>
        </div>
        
        <div class="status-grid" style="margin-top: 20px;">
            <div class="status-card" style="border-left: 4px solid #2196F3;">
                <div class="status-title">Sensor Readings (1Hz)</div>
                <div style="margin-bottom: 12px; padding: 8px; background: #1a1a1a; border-radius: 4px;">
                    <div style="font-size: 11px; color: #888; margin-bottom: 4px;">Sensor Status</div>
                    <div style="display: grid; grid-template-columns: repeat(5, 1fr); gap: 8px; font-size: 10px;">
                        <div>
                            <span style="color: #888;">SHT45:</span>
                            <span id="sensor-status-sht45" style="font-weight: bold; margin-left: 4px;">--</span>
                        </div>
                        <div>
                            <span style="color: #888;">SGP40:</span>
                            <span id="sensor-status-sgp40" style="font-weight: bold; margin-left: 4px;">--</span>
                        </div>
                        <div>
                            <span style="color: #888;">SCD40:</span>
                            <span id="sensor-status-scd40" style="font-weight: bold; margin-left: 4px;">--</span>
                        </div>
                        <div>
                            <span style="color: #888;">VCNL4040:</span>
                            <span id="sensor-status-vcnl4040" style="font-weight: bold; margin-left: 4px;">--</span>
                        </div>
                        <div>
                            <span style="color: #888;">EC10:</span>
                            <span id="sensor-status-ec10" style="font-weight: bold; margin-left: 4px;">--</span>
                        </div>
                    </div>
                </div>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 12px;">
                    <div>
                        <div style="font-size: 11px; color: #888; margin-bottom: 4px;">Temperature</div>
                        <div style="font-size: 18px; color: #fff; font-weight: bold;" id="sensor-temperature">--</div>
                        <div style="font-size: 10px;" id="sensor-temp-source">--</div>
                    </div>
                    <div>
                        <div style="font-size: 11px; color: #888; margin-bottom: 4px;">Humidity</div>
                        <div style="font-size: 18px; color: #fff; font-weight: bold;" id="sensor-humidity">--</div>
                        <div style="font-size: 10px;" id="sensor-hum-source">--</div>
                    </div>
                    <div>
                        <div style="font-size: 11px; color: #888; margin-bottom: 4px;">CO₂</div>
                        <div style="font-size: 18px; color: #4CAF50; font-weight: bold;" id="sensor-co2">--</div>
                        <div style="font-size: 10px;" id="sensor-co2-source">--</div>
                    </div>
                    <div>
                        <div style="font-size: 11px; color: #888; margin-bottom: 4px;">VOC Index</div>
                        <div style="font-size: 18px; color: #FF9800; font-weight: bold;" id="sensor-voc">--</div>
                        <div style="font-size: 10px;" id="sensor-voc-source">--</div>
                    </div>
                    <div>
                        <div style="font-size: 11px; color: #888; margin-bottom: 4px;">Ambient Light</div>
                        <div style="font-size: 18px; color: #FFD700; font-weight: bold;" id="sensor-lux">--</div>
                        <div style="font-size: 10px;" id="sensor-lux-source">--</div>
                    </div>
                    <div>
                        <div style="font-size: 11px; color: #888; margin-bottom: 4px;">PM2.5</div>
                        <div style="font-size: 18px; color: #f44336; font-weight: bold;" id="sensor-pm25">--</div>
                        <div style="font-size: 10px;" id="sensor-pm-source">--</div>
                    </div>
                </div>
            </div>
        </div>
        
        <div class="errors-section" id="errors-section" style="
            background: #2a1a1a;
            border: 1px solid #f44336;
            border-radius: 8px;
            padding: 15px;
            margin-bottom: 20px;
            display: none;
        ">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;">
                <h3 style="margin: 0; color: #f44336;">⚠️ Errors (<span id="error-count">0</span>)</h3>
                <button id="clear-errors-btn" onclick="clearErrors()" style="
                    background: #444;
                    color: white;
                    border: 1px solid #666;
                    padding: 5px 15px;
                    border-radius: 4px;
                    cursor: pointer;
                    font-size: 12px;
                ">Clear</button>
            </div>
            <div id="error-list" style="
                max-height: 200px;
                overflow-y: auto;
                font-family: 'Courier New', monospace;
                font-size: 11px;
                color: #ff6b6b;
            "></div>
        </div>
        
        <div class="logs">
            <h3 style="margin-bottom: 15px;">All Logs (<span id="logs-count">0</span> entries)</h3>
            <div style="margin-bottom: 10px; font-size: 11px; color: #666;">
                Showing last 100 entries. Scroll to see more.
            </div>
            <div id="logs-container"></div>
        </div>
        
        <div class="ai-assistant" style="
            background: #2a2a2a;
            border-radius: 8px;
            padding: 20px;
            margin-top: 30px;
            max-height: 600px;
            display: flex;
            flex-direction: column;
        ">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px;">
                <h3 style="margin: 0; color: #4CAF50;">🤖 AI Assistant</h3>
                <div id="ai-auto-review-status" style="font-size: 11px; color: #666;">
                    <span id="ai-review-indicator">●</span> Auto-review: <span id="ai-review-time">Never</span>
                </div>
            </div>
            
            <div id="ai-alerts" style="
                background: #3a1a1a;
                border-left: 4px solid #f44336;
                border-radius: 4px;
                padding: 12px;
                margin-bottom: 15px;
                display: none;
            ">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 5px;">
                    <div style="font-weight: bold; color: #ff6b6b;">⚠️ AI Alert</div>
                    <button id="copy-alert-btn" onclick="copyAlert()" style="
                        background: #444;
                        color: #fff;
                        border: 1px solid #666;
                        padding: 4px 10px;
                        border-radius: 4px;
                        cursor: pointer;
                        font-size: 11px;
                        transition: background 0.2s;
                    " onmouseover="this.style.background='#555'" onmouseout="this.style.background='#444'">📋 Copy</button>
                </div>
                <div id="ai-alert-content" style="color: #ffaaaa; font-size: 12px; white-space: pre-wrap;"></div>
            </div>
            
            <div id="ai-suggestions" style="
                background: #1a3a2a;
                border-left: 4px solid #4CAF50;
                border-radius: 4px;
                padding: 12px;
                margin-bottom: 15px;
                display: none;
            ">
                <div style="font-weight: bold; color: #6bff6b; margin-bottom: 5px;">💡 AI Suggestion</div>
                <div id="ai-suggestion-content" style="color: #aaffaa; font-size: 12px; white-space: pre-wrap;"></div>
            </div>
            <div id="ai-chat-container" style="
                flex: 1;
                overflow-y: auto;
                background: #1a1a1a;
                border-radius: 4px;
                padding: 15px;
                margin-bottom: 15px;
                min-height: 300px;
                max-height: 400px;
                font-family: 'Courier New', monospace;
                font-size: 12px;
            ">
                <div class="ai-message" style="color: #aaa; margin-bottom: 10px;">
                    AI Assistant ready. I can help you debug, read/edit files, and build/flash firmware.
                </div>
            </div>
            <div style="display: flex; gap: 10px;">
                <input type="text" id="ai-input" placeholder="Ask me anything about the logs, code, or system..." style="
                    flex: 1;
                    background: #1a1a1a;
                    color: #fff;
                    border: 1px solid #444;
                    padding: 10px;
                    border-radius: 4px;
                    font-size: 14px;
                ">
                <button id="ai-send-btn" style="
                    background: #4CAF50;
                    color: white;
                    border: none;
                    padding: 10px 20px;
                    border-radius: 4px;
                    cursor: pointer;
                    font-size: 14px;
                    font-weight: bold;
                ">Send</button>
            </div>
            <div id="ai-status" style="margin-top: 10px; font-size: 11px; color: #666;"></div>
        </div>
    </div>
    
    <script>
        // AI Assistant Functions
        async function sendAIMessage() {
            const input = document.getElementById('ai-input');
            if (!input) {
                console.error('AI input element not found');
                return;
            }
            const message = input.value.trim();
            if (!message) return;
            
            const chatContainer = document.getElementById('ai-chat-container');
            const statusEl = document.getElementById('ai-status');
            
            // Add user message
            const userMsg = document.createElement('div');
            userMsg.className = 'ai-message';
            userMsg.style.cssText = 'color: #6b9fff; margin-bottom: 10px;';
            userMsg.textContent = `You: ${message}`;
            chatContainer.appendChild(userMsg);
            chatContainer.scrollTop = chatContainer.scrollHeight;
            
            input.value = '';
            input.disabled = true;
            if (statusEl) {
                statusEl.textContent = 'Thinking...';
                statusEl.style.color = '#FF9800';
            }
            
            try {
                const response = await fetch('/api/ai/chat', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({message: message})
                });
                
                if (!response.ok) {
                    throw new Error(`HTTP ${response.status}: ${response.statusText}`);
                }
                
                const data = await response.json();
                
                if (data.error) {
                    throw new Error(data.error);
                }
                
                // Add AI response
                const aiMsg = document.createElement('div');
                aiMsg.className = 'ai-message';
                aiMsg.style.cssText = 'color: #6bff6b; margin-bottom: 10px; white-space: pre-wrap;';
                aiMsg.textContent = `AI: ${data.response || 'No response'}`;
                chatContainer.appendChild(aiMsg);
                chatContainer.scrollTop = chatContainer.scrollHeight;
                
                // Execute actions if any
                if (data.actions && data.actions.length > 0) {
                    for (const action of data.actions) {
                        await executeAIAction(action, chatContainer);
                    }
                }
                
                if (statusEl) {
                    statusEl.textContent = 'Ready';
                    statusEl.style.color = '#4CAF50';
                }
            } catch (error) {
                console.error('AI chat error:', error);
                const errorMsg = document.createElement('div');
                errorMsg.className = 'ai-message';
                errorMsg.style.cssText = 'color: #ff6b6b; margin-bottom: 10px;';
                errorMsg.textContent = `Error: ${error.message}`;
                chatContainer.appendChild(errorMsg);
                chatContainer.scrollTop = chatContainer.scrollHeight;
                
                if (statusEl) {
                    statusEl.textContent = `Error: ${error.message}`;
                    statusEl.style.color = '#f44336';
                }
            } finally {
                input.disabled = false;
                input.focus();
            }
        }
        
        // Set up event listeners when DOM is ready
        document.addEventListener('DOMContentLoaded', function() {
            const input = document.getElementById('ai-input');
            const sendBtn = document.getElementById('ai-send-btn');
            
            if (input && sendBtn) {
                // Enter key handler
                input.addEventListener('keydown', function(event) {
                    if (event.key === 'Enter' && !event.shiftKey) {
                        event.preventDefault();
                        sendAIMessage();
                    }
                });
                
                // Button click handler
                sendBtn.addEventListener('click', function(event) {
                    event.preventDefault();
                    sendAIMessage();
                });
            } else {
                console.error('AI chat elements not found:', {input: !!input, sendBtn: !!sendBtn});
            }
        });
        
        // Error handling functions
        function escapeHtml(text) {
            const div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
        }
        
        function flashBackground() {
            const body = document.body;
            let flashCount = 0;
            const maxFlashes = 3;
            const flashInterval = setInterval(() => {
                if (flashCount >= maxFlashes * 2) {
                    clearInterval(flashInterval);
                    body.style.backgroundColor = '';
                    return;
                }
                body.style.backgroundColor = flashCount % 2 === 0 ? '#ff4444' : '';
                flashCount++;
            }, 200);
        }
        
        function clearErrors() {
            fetch('/api/errors/clear', { method: 'POST' })
                .then(response => response.json())
                .then(data => {
                    if (data.success) {
                        const errorSection = document.getElementById('errors-section');
                        const errorList = document.getElementById('error-list');
                        const errorCount = document.getElementById('error-count');
                        if (errorSection) errorSection.style.display = 'none';
                        if (errorList) errorList.innerHTML = '';
                        if (errorCount) errorCount.textContent = '0';
                    }
                })
                .catch(err => console.error('Error clearing errors:', err));
        }
        
        async function executeAIAction(action, chatContainer) {
            const statusEl = document.getElementById('ai-status');
            const parts = action.split(':');
            const actionType = parts[0];
            
            try {
                if (actionType === 'read_file') {
                    const filePath = parts.slice(1).join(':');
                    statusEl.textContent = `Reading file: ${filePath}...`;
                    
                    const response = await fetch('/api/ai/read_file', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({path: filePath})
                    });
                    
                    const data = await response.json();
                    if (data.error) throw new Error(data.error);
                    
                    const fileMsg = document.createElement('div');
                    fileMsg.className = 'ai-message';
                    fileMsg.style.cssText = 'color: #ffd93d; margin-bottom: 10px; white-space: pre-wrap; font-size: 11px;';
                    fileMsg.textContent = `📄 File: ${filePath}\n${data.content.substring(0, 2000)}${data.content.length > 2000 ? '...' : ''}`;
                    chatContainer.appendChild(fileMsg);
                    chatContainer.scrollTop = chatContainer.scrollHeight;
                    
                } else if (actionType === 'write_file') {
                    const filePath = parts[1];
                    const content = parts.slice(2).join(':');
                    statusEl.textContent = `Writing file: ${filePath}...`;
                    
                    const response = await fetch('/api/ai/write_file', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({path: filePath, content: content})
                    });
                    
                    const data = await response.json();
                    if (data.error) throw new Error(data.error);
                    
                    const fileMsg = document.createElement('div');
                    fileMsg.className = 'ai-message';
                    fileMsg.style.cssText = 'color: #4CAF50; margin-bottom: 10px;';
                    fileMsg.textContent = `✓ File written: ${filePath}`;
                    chatContainer.appendChild(fileMsg);
                    chatContainer.scrollTop = chatContainer.scrollHeight;
                    
                } else if (actionType === 'build') {
                    statusEl.textContent = 'Building firmware... (this may take a few minutes)';
                    
                    const response = await fetch('/api/ai/build', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'}
                    });
                    
                    const data = await response.json();
                    if (data.error) throw new Error(data.error);
                    
                    const buildMsg = document.createElement('div');
                    buildMsg.className = 'ai-message';
                    buildMsg.style.cssText = `color: ${data.success ? '#4CAF50' : '#f44336'}; margin-bottom: 10px; white-space: pre-wrap; font-size: 11px;`;
                    const stdout = data.stdout || '';
                    const stderr = data.stderr || '';
                    buildMsg.textContent = `🔨 Build ${data.success ? 'SUCCESS' : 'FAILED'}\n${stdout.slice(-1000)}${stderr ? '\n' + stderr.slice(-1000) : ''}`;
                    chatContainer.appendChild(buildMsg);
                    chatContainer.scrollTop = chatContainer.scrollHeight;
                    
                } else if (actionType === 'flash') {
                    const port = parts[1] || '/dev/cu.usbserial-110';
                    statusEl.textContent = `Flashing to ${port}... (this may take a minute)`;
                    
                    const response = await fetch('/api/ai/flash', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({port: port})
                    });
                    
                    const data = await response.json();
                    if (data.error) throw new Error(data.error);
                    
                    const flashMsg = document.createElement('div');
                    flashMsg.className = 'ai-message';
                    flashMsg.style.cssText = `color: ${data.success ? '#4CAF50' : '#f44336'}; margin-bottom: 10px; white-space: pre-wrap; font-size: 11px;`;
                    const stdout = data.stdout || '';
                    const stderr = data.stderr || '';
                    flashMsg.textContent = `⚡ Flash ${data.success ? 'SUCCESS' : 'FAILED'}\n${stdout.slice(-1000)}${stderr ? '\n' + stderr.slice(-1000) : ''}`;
                    chatContainer.appendChild(flashMsg);
                    chatContainer.scrollTop = chatContainer.scrollHeight;
                }
            } catch (error) {
                const errorMsg = document.createElement('div');
                errorMsg.className = 'ai-message';
                errorMsg.style.cssText = 'color: #ff6b6b; margin-bottom: 10px;';
                errorMsg.textContent = `Action error: ${error.message}`;
                chatContainer.appendChild(errorMsg);
                chatContainer.scrollTop = chatContainer.scrollHeight;
            }
        }
    </script>
</body>
</html>
"""

@app.route('/')
def dashboard():
    """Render the dashboard."""
    return render_template_string(HTML_TEMPLATE)

@app.route('/favicon.ico')
def favicon():
    """Handle favicon request to prevent 403 errors."""
    return '', 204  # No content

@app.route('/api/reboot', methods=['POST'])
def api_reboot():
    """Reboot the device by toggling DTR."""
    global serial_conn
    try:
        # Check if serial connection exists and is open
        if not serial_conn:
            return jsonify({'success': False, 'message': 'Serial port not initialized'}), 500
        
        if not serial_conn.is_open:
            return jsonify({'success': False, 'message': 'Serial port not open. Status: ' + str(status.get('serial_connected', False))}), 500
        
        if not status.get('serial_connected', False):
            return jsonify({'success': False, 'message': 'Serial port marked as disconnected'}), 500
        
        # Toggle DTR to reset device
        serial_conn.setDTR(False)
        time.sleep(0.1)
        serial_conn.setDTR(True)
        time.sleep(0.1)
        
        print(f"✓ Reboot command sent via DTR toggle")
        return jsonify({'success': True, 'message': 'Device reboot command sent'})
    except serial.SerialException as e:
        print(f"✗ Serial error during reboot: {e}")
        status['serial_connected'] = False
        return jsonify({'success': False, 'message': f'Serial error: {str(e)}'}), 500
    except Exception as e:
        print(f"✗ Error during reboot: {e}")
        import traceback
        traceback.print_exc()
        return jsonify({'success': False, 'message': str(e)}), 500

# Error log persistence
ERROR_LOG_FILE = os.path.join(os.path.dirname(__file__), 'error_log.json')

def load_error_log():
    """Load error log from file"""
    global status
    try:
        if os.path.exists(ERROR_LOG_FILE):
            with open(ERROR_LOG_FILE, 'r') as f:
                error_data = json.load(f)
                status['error_log'] = error_data.get('errors', [])
                status['last_boot_time'] = error_data.get('last_boot_time')
                # Keep only last 50 errors
                if len(status['error_log']) > 50:
                    status['error_log'] = status['error_log'][-50:]
    except Exception as e:
        print(f"Error loading error log: {e}")
        status['error_log'] = []

def save_error_log():
    """Save error log to file"""
    try:
        error_data = {
            'errors': status.get('error_log', []),
            'last_boot_time': status.get('last_boot_time'),
            'last_saved': time.time()
        }
        with open(ERROR_LOG_FILE, 'w') as f:
            json.dump(error_data, f, indent=2)
    except Exception as e:
        print(f"Error saving error log: {e}")

@app.route('/api/status')
def api_status():
    """API endpoint for status (for AJAX polling)."""
    # Convert deque to list for JSON serialization
    status_copy = status.copy()
    status_copy['logs'] = list(status['logs'])
    # Ensure serial_port is always set
    if not status_copy.get('serial_port'):
        status_copy['serial_port'] = status.get('serial_port', 'unknown')
    # Ensure serial_connected is a boolean
    status_copy['serial_connected'] = bool(status.get('serial_connected', False))
    
    # Update MQTT status
    with mqtt_lock:
        status_copy['mqtt'] = {
            'connected': mqtt_connected,
            'device_id': mqtt_device_id,
            'messages_received': status.get('mqtt', {}).get('messages_received', 0),
            'last_message_time': status.get('mqtt', {}).get('last_message_time'),
        }
    
    return jsonify(status_copy)

@app.route('/api/debug')
def api_debug():
    """Debug endpoint to check serial connection state."""
    global serial_conn
    debug_info = {
        'serial_conn_exists': serial_conn is not None,
        'serial_conn_is_open': serial_conn.is_open if serial_conn else False,
        'status_serial_connected': status.get('serial_connected', False),
        'status_serial_port': status.get('serial_port', 'not set'),
        'status_logs_count': len(status.get('logs', [])),
    }
    return jsonify(debug_info)

@app.route('/api/ports', methods=['GET'])
def api_ports():
    """List available serial ports."""
    try:
        ports = []
        # Use pyserial's list_ports for cross-platform support
        available_ports = serial.tools.list_ports.comports()
        for port in available_ports:
            ports.append({
                'device': port.device,
                'description': port.description or 'Unknown',
                'manufacturer': port.manufacturer or '',
            })
        
        # Also check common macOS paths
        import glob
        import os
        mac_ports = glob.glob('/dev/cu.*')
        for port_path in mac_ports:
            if 'Bluetooth' not in port_path and port_path not in [p['device'] for p in ports]:
                try:
                    # Try to open to verify it's accessible
                    test_ser = serial.Serial(port_path, 115200, timeout=0.1)
                    test_ser.close()
                    ports.append({
                        'device': port_path,
                        'description': os.path.basename(port_path),
                        'manufacturer': '',
                    })
                except:
                    pass
        
        return jsonify({'ports': ports, 'current': status.get('serial_port', '')})
    except Exception as e:
        return jsonify({'error': str(e), 'ports': []}), 500

# Spotify OAuth configuration
# Default Client ID for basic authorization (users can override with their own)
SPOTIFY_DEFAULT_CLIENT_ID = '5f573c9620494bae87890c0ee08e6028'  # Public client ID for basic use
SPOTIFY_CLIENT_ID = os.environ.get('SPOTIFY_CLIENT_ID', SPOTIFY_DEFAULT_CLIENT_ID)
SPOTIFY_CLIENT_SECRET = os.environ.get('SPOTIFY_CLIENT_SECRET', '')
SPOTIFY_REDIRECT_URI = os.environ.get('SPOTIFY_REDIRECT_URI', 'http://localhost:5001/api/spotify/auth/callback')
SPOTIFY_SCOPES = 'user-read-playback-state user-modify-playback-state user-read-currently-playing'

# In-memory state storage (in production, use Redis or database)
spotify_oauth_states = {}

def get_spotify_token():
    """Get Spotify access token from environment or file."""
    token = os.environ.get('SPOTIFY_ACCESS_TOKEN')
    if not token:
        token_file = os.path.expanduser('~/.spotify_token')
        if os.path.exists(token_file):
            with open(token_file, 'r') as f:
                token = f.read().strip()
    return token

def save_spotify_token(token, refresh_token=None):
    """Save Spotify access token to file."""
    token_file = os.path.expanduser('~/.spotify_token')
    try:
        with open(token_file, 'w') as f:
            f.write(token)
        os.chmod(token_file, 0o600)  # Restrict permissions
        if refresh_token:
            refresh_file = os.path.expanduser('~/.spotify_refresh_token')
            with open(refresh_file, 'w') as f:
                f.write(refresh_token)
            os.chmod(refresh_file, 0o600)
        return True
    except Exception as e:
        print(f"✗ Error saving Spotify token: {e}")
        return False

@app.route('/api/spotify/config', methods=['POST'])
def api_spotify_config():
    """Save Spotify OAuth configuration."""
    try:
        data = request.get_json()
        client_id = data.get('client_id', '').strip()
        client_secret = data.get('client_secret', '').strip()
        
        if not client_id or not client_secret:
            return jsonify({
                'success': False,
                'message': 'Both Client ID and Client Secret are required'
            }), 400
        
        # Save to a config file
        config_file = os.path.expanduser('~/.spotify_config.json')
        try:
            config_data = {
                'client_id': client_id,
                'client_secret': client_secret
            }
            with open(config_file, 'w') as f:
                json.dump(config_data, f)
            os.chmod(config_file, 0o600)  # Restrict permissions
            
            # Update global variables
            global SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET
            SPOTIFY_CLIENT_ID = client_id
            SPOTIFY_CLIENT_SECRET = client_secret
            
            print("✓ Spotify configuration saved")
            return jsonify({
                'success': True,
                'message': 'Configuration saved successfully'
            })
        except Exception as e:
            print(f"✗ Error saving Spotify config: {e}")
            return jsonify({
                'success': False,
                'message': f'Failed to save configuration: {str(e)}'
            }), 500
    except Exception as e:
        print(f"✗ Error in Spotify config endpoint: {e}")
        return jsonify({
            'success': False,
            'message': str(e)
        }), 500

@app.route('/api/spotify/config', methods=['GET'])
def api_spotify_get_config():
    """Get Spotify OAuth configuration status (without secrets)."""
    has_client_id = bool(SPOTIFY_CLIENT_ID or load_spotify_config().get('client_id'))
    return jsonify({
        'configured': has_client_id,
        'has_client_id': has_client_id
    })

def load_spotify_config():
    """Load Spotify configuration from file."""
    config_file = os.path.expanduser('~/.spotify_config.json')
    if os.path.exists(config_file):
        try:
            with open(config_file, 'r') as f:
                return json.load(f)
        except Exception as e:
            print(f"✗ Error loading Spotify config: {e}")
    return {}

# Load configuration on startup
_loaded_config = load_spotify_config()
if not SPOTIFY_CLIENT_ID and _loaded_config.get('client_id'):
    SPOTIFY_CLIENT_ID = _loaded_config.get('client_id')
if not SPOTIFY_CLIENT_SECRET and _loaded_config.get('client_secret'):
    SPOTIFY_CLIENT_SECRET = _loaded_config.get('client_secret')

@app.route('/api/spotify/auth/authorize', methods=['GET'])
def api_spotify_authorize():
    """Start Spotify OAuth authorization flow - redirects directly to Spotify."""
    # Check if we have client ID from config file
    client_id_to_use = SPOTIFY_CLIENT_ID
    client_secret_to_use = SPOTIFY_CLIENT_SECRET
    
    if not client_id_to_use:
        _config = load_spotify_config()
        if _config.get('client_id'):
            client_id_to_use = _config.get('client_id')
            client_secret_to_use = _config.get('client_secret')
    
    # Use default if still not set
    if not client_id_to_use:
        client_id_to_use = SPOTIFY_DEFAULT_CLIENT_ID
    
    import secrets
    import urllib.parse
    
    # Generate state for CSRF protection
    state = secrets.token_urlsafe(32)
    spotify_oauth_states[state] = {
        'created': time.time(),
        'used': False
    }
    
    # Build authorization URL
    params = {
        'client_id': client_id_to_use,
        'response_type': 'code',
        'redirect_uri': SPOTIFY_REDIRECT_URI,
        'scope': SPOTIFY_SCOPES,
        'state': state,
        'show_dialog': 'false'
    }
    
    auth_url = 'https://accounts.spotify.com/authorize?' + urllib.parse.urlencode(params)
    
    # If this is a direct browser request (not AJAX), redirect immediately
    if request.headers.get('Accept', '').find('text/html') != -1 or not request.headers.get('X-Requested-With'):
        from flask import redirect
        return redirect(auth_url)
    
    # Otherwise return JSON for AJAX requests
    return jsonify({
        'success': True,
        'auth_url': auth_url,
        'state': state
    })

@app.route('/api/spotify/auth/callback', methods=['GET'])
def api_spotify_callback():
    """Handle Spotify OAuth callback."""
    code = request.args.get('code')
    state = request.args.get('state')
    error = request.args.get('error')
    
    if error:
        return render_template_string('''
            <!DOCTYPE html>
            <html>
            <head><title>Spotify Authorization</title></head>
            <body style="font-family: Arial, sans-serif; text-align: center; padding: 50px;">
                <h2 style="color: #f44336;">Authorization Failed</h2>
                <p>Error: {{ error }}</p>
                <p><a href="javascript:window.close()">Close this window</a></p>
            </body>
            </html>
        ''', error=error)
    
    if not code or not state:
        return render_template_string('''
            <!DOCTYPE html>
            <html>
            <head><title>Spotify Authorization</title></head>
            <body style="font-family: Arial, sans-serif; text-align: center; padding: 50px;">
                <h2 style="color: #f44336;">Authorization Failed</h2>
                <p>Missing authorization code or state.</p>
                <p><a href="javascript:window.close()">Close this window</a></p>
            </body>
            </html>
        ''')
    
    # Verify state
    if state not in spotify_oauth_states:
        return render_template_string('''
            <!DOCTYPE html>
            <html>
            <head><title>Spotify Authorization</title></head>
            <body style="font-family: Arial, sans-serif; text-align: center; padding: 50px;">
                <h2 style="color: #f44336;">Authorization Failed</h2>
                <p>Invalid state parameter. Please try again.</p>
                <p><a href="javascript:window.close()">Close this window</a></p>
            </body>
            </html>
        ''')
    
    # Check if state is expired (5 minutes)
    if time.time() - spotify_oauth_states[state]['created'] > 300:
        del spotify_oauth_states[state]
        return render_template_string('''
            <!DOCTYPE html>
            <html>
            <head><title>Spotify Authorization</title></head>
            <body style="font-family: Arial, sans-serif; text-align: center; padding: 50px;">
                <h2 style="color: #f44336;">Authorization Expired</h2>
                <p>The authorization request has expired. Please try again.</p>
                <p><a href="javascript:window.close()">Close this window</a></p>
            </body>
            </html>
        ''')
    
    spotify_oauth_states[state]['used'] = True
    
    # Exchange code for token
    if not SPOTIFY_API_AVAILABLE:
        return render_template_string('''
            <!DOCTYPE html>
            <html>
            <head><title>Spotify Authorization</title></head>
            <body style="font-family: Arial, sans-serif; text-align: center; padding: 50px;">
                <h2 style="color: #f44336;">Configuration Error</h2>
                <p>Spotify API not available. Install requests library.</p>
                <p><a href="javascript:window.close()">Close this window</a></p>
            </body>
            </html>
        ''')
    
    try:
        import base64
        import urllib.parse
        
        # Get client credentials (from config or default)
        client_id_to_use = SPOTIFY_CLIENT_ID
        client_secret_to_use = SPOTIFY_CLIENT_SECRET
        
        if not client_id_to_use:
            _config = load_spotify_config()
            if _config.get('client_id'):
                client_id_to_use = _config.get('client_id')
                client_secret_to_use = _config.get('client_secret')
        
        if not client_id_to_use:
            client_id_to_use = SPOTIFY_DEFAULT_CLIENT_ID
        
        # Exchange authorization code for access token
        token_url = 'https://accounts.spotify.com/api/token'
        
        # If we have a client secret, use it; otherwise try without (for public clients)
        if client_secret_to_use:
            auth_header = base64.b64encode(f'{client_id_to_use}:{client_secret_to_use}'.encode()).decode()
            headers = {
                'Authorization': f'Basic {auth_header}',
                'Content-Type': 'application/x-www-form-urlencoded'
            }
        else:
            # For public clients, we need to use PKCE or client credentials in body
            headers = {
                'Content-Type': 'application/x-www-form-urlencoded'
            }
        
        data = {
            'grant_type': 'authorization_code',
            'code': code,
            'redirect_uri': SPOTIFY_REDIRECT_URI,
            'client_id': client_id_to_use
        }
        
        if client_secret_to_use:
            data['client_secret'] = client_secret_to_use
        
        response = requests.post(token_url, headers=headers, data=data, timeout=10)
        
        if response.status_code != 200:
            error_msg = response.text[:200]
            return render_template_string('''
                <!DOCTYPE html>
                <html>
                <head><title>Spotify Authorization</title></head>
                <body style="font-family: Arial, sans-serif; text-align: center; padding: 50px;">
                    <h2 style="color: #f44336;">Token Exchange Failed</h2>
                    <p>Error: {{ error }}</p>
                    <p><a href="javascript:window.close()">Close this window</a></p>
                </body>
                </html>
            ''', error=error_msg)
        
        token_data = response.json()
        access_token = token_data.get('access_token')
        refresh_token = token_data.get('refresh_token')
        expires_in = token_data.get('expires_in', 3600)
        
        if not access_token:
            return render_template_string('''
                <!DOCTYPE html>
                <html>
                <head><title>Spotify Authorization</title></head>
                <body style="font-family: Arial, sans-serif; text-align: center; padding: 50px;">
                    <h2 style="color: #f44336;">Authorization Failed</h2>
                    <p>No access token received from Spotify.</p>
                    <p><a href="javascript:window.close()">Close this window</a></p>
                </body>
                </html>
            ''')
        
        # Save token
        if save_spotify_token(access_token, refresh_token):
            print(f"✓ Spotify access token saved (expires in {expires_in}s)")
            return render_template_string('''
                <!DOCTYPE html>
                <html>
                <head><title>Spotify Authorization</title>
                    <script>
                        window.opener.postMessage({type: 'spotify-auth-success'}, '*');
                        setTimeout(function() { window.close(); }, 2000);
                    </script>
                </head>
                <body style="font-family: Arial, sans-serif; text-align: center; padding: 50px;">
                    <h2 style="color: #4CAF50;">✓ Authorization Successful!</h2>
                    <p>You can now close this window.</p>
                    <p style="font-size: 12px; color: #888;">This window will close automatically...</p>
                </body>
                </html>
            ''')
        else:
            return render_template_string('''
                <!DOCTYPE html>
                <html>
                <head><title>Spotify Authorization</title></head>
                <body style="font-family: Arial, sans-serif; text-align: center; padding: 50px;">
                    <h2 style="color: #f44336;">Save Failed</h2>
                    <p>Token received but failed to save. Please check file permissions.</p>
                    <p><a href="javascript:window.close()">Close this window</a></p>
                </body>
                </html>
            ''')
    except Exception as e:
        print(f"✗ Error in Spotify callback: {e}")
        import traceback
        traceback.print_exc()
        return render_template_string('''
            <!DOCTYPE html>
            <html>
            <head><title>Spotify Authorization</title></head>
            <body style="font-family: Arial, sans-serif; text-align: center; padding: 50px;">
                <h2 style="color: #f44336;">Authorization Error</h2>
                <p>An error occurred: {{ error }}</p>
                <p><a href="javascript:window.close()">Close this window</a></p>
            </body>
            </html>
        ''', error=str(e))

@app.route('/api/spotify/auth/status', methods=['GET'])
def api_spotify_auth_status():
    """Check Spotify authorization status."""
    token = get_spotify_token()
    return jsonify({
        'authorized': bool(token),
        'has_token': bool(token)
    })

@app.route('/api/spotify/auth/upload', methods=['POST'])
def api_spotify_upload_credentials():
    """Upload Spotify credentials manually."""
    try:
        data = request.get_json()
        access_token = data.get('access_token')
        
        if not access_token:
            return jsonify({
                'success': False,
                'message': 'access_token is required'
            }), 400
        
        # Validate token format (basic check)
        if len(access_token) < 50:
            return jsonify({
                'success': False,
                'message': 'Invalid token format (too short)'
            }), 400
        
        # Save token
        if save_spotify_token(access_token):
            print("✓ Spotify credentials uploaded and saved")
            return jsonify({
                'success': True,
                'message': 'Credentials uploaded successfully'
            })
        else:
            return jsonify({
                'success': False,
                'message': 'Failed to save credentials'
            }), 500
    except Exception as e:
        print(f"✗ Error uploading Spotify credentials: {e}")
        import traceback
        traceback.print_exc()
        return jsonify({
            'success': False,
            'message': str(e)
        }), 500

@app.route('/api/spotify/auth/disconnect', methods=['POST'])
def api_spotify_disconnect():
    """Disconnect Spotify account by removing token."""
    try:
        token_file = os.path.expanduser('~/.spotify_token')
        refresh_file = os.path.expanduser('~/.spotify_refresh_token')
        
        removed = False
        if os.path.exists(token_file):
            os.remove(token_file)
            removed = True
        if os.path.exists(refresh_file):
            os.remove(refresh_file)
        
        if removed:
            print("✓ Spotify token removed")
            return jsonify({'success': True, 'message': 'Spotify account disconnected'})
        else:
            return jsonify({'success': False, 'message': 'No token found to remove'})
    except Exception as e:
        print(f"✗ Error disconnecting Spotify: {e}")
        return jsonify({'success': False, 'message': str(e)}), 500

@app.route('/api/spotify/devices', methods=['GET'])
def api_spotify_scan_devices():
    """Scan for available Spotify devices using Web API."""
    if not SPOTIFY_API_AVAILABLE:
        return jsonify({
            'success': False,
            'message': 'Spotify API not available. Install requests: pip install requests',
            'devices': []
        }), 503
    
    try:
        
        # Get Spotify access token
        spotify_token = get_spotify_token()
        
        if not spotify_token:
            return jsonify({
                'success': False,
                'message': 'Spotify access token not found. Set SPOTIFY_ACCESS_TOKEN environment variable or create ~/.spotify_token file',
                'devices': []
            }), 401
        
        # Call Spotify Web API to get available devices
        headers = {
            'Authorization': f'Bearer {spotify_token}',
            'Content-Type': 'application/json'
        }
        
        response = requests.get('https://api.spotify.com/v1/me/player/devices', headers=headers, timeout=5)
        
        if response.status_code == 401:
            return jsonify({
                'success': False,
                'message': 'Spotify token expired or invalid. Please refresh your access token.',
                'devices': []
            }), 401
        
        if response.status_code != 200:
            return jsonify({
                'success': False,
                'message': f'Spotify API error: {response.status_code} - {response.text[:100]}',
                'devices': []
            }), response.status_code
        
        data = response.json()
        devices = data.get('devices', [])
        
        # Filter and highlight Naphome devices
        naphome_devices = [d for d in devices if 'naphome' in d.get('name', '').lower()]
        
        print(f"✓ Found {len(devices)} Spotify device(s), {len(naphome_devices)} with 'Naphome' in name")
        
        return jsonify({
            'success': True,
            'devices': devices,
            'naphome_count': len(naphome_devices)
        })
    except requests.exceptions.RequestException as e:
        print(f"✗ Network error during Spotify device scan: {e}")
        return jsonify({
            'success': False,
            'message': f'Network error: {str(e)}',
            'devices': []
        }), 500
    except Exception as e:
        print(f"✗ Error during Spotify device scan: {e}")
        import traceback
        traceback.print_exc()
        return jsonify({
            'success': False,
            'message': str(e),
            'devices': []
        }), 500

@app.route('/api/spotify/select_device', methods=['POST'])
def api_spotify_select_device():
    """Select a Spotify device for playback."""
    if not SPOTIFY_API_AVAILABLE:
        return jsonify({
            'success': False,
            'message': 'Spotify API not available. Install requests: pip install requests'
        }), 503
    
    try:
        
        data = request.get_json()
        device_id = data.get('device_id')
        device_name = data.get('device_name', 'Unknown')
        
        if not device_id:
            return jsonify({'success': False, 'message': 'device_id required'}), 400
        
        # Get Spotify access token
        spotify_token = get_spotify_token()
        
        if not spotify_token:
            return jsonify({'success': False, 'message': 'Spotify access token not found'}), 401
        
        # Transfer playback to selected device
        headers = {
            'Authorization': f'Bearer {spotify_token}',
            'Content-Type': 'application/json'
        }
        
        response = requests.put(
            'https://api.spotify.com/v1/me/player',
            headers=headers,
            json={'device_ids': [device_id], 'play': False},
            timeout=5
        )
        
        if response.status_code == 204:
            print(f"✓ Transferred playback to device: {device_name} ({device_id})")
            return jsonify({'success': True, 'message': f'Selected device: {device_name}'})
        else:
            return jsonify({
                'success': False,
                'message': f'Failed to select device: {response.status_code} - {response.text[:100]}'
            }), response.status_code
    except Exception as e:
        print(f"✗ Error selecting Spotify device: {e}")
        import traceback
        traceback.print_exc()
        return jsonify({'success': False, 'message': str(e)}), 500

@app.route('/api/spotify/<action>', methods=['POST'])
def api_spotify_control(action):
    """Control Spotify playback via serial commands."""
    global serial_conn
    try:
        if not serial_conn or not serial_conn.is_open:
            return jsonify({'success': False, 'message': 'Serial port not connected'}), 500
        
        if not status.get('serial_connected', False):
            return jsonify({'success': False, 'message': 'Serial port marked as disconnected'}), 500
        
        # Map actions to commands
        commands = {
            'play': 'SPOTIFY_PLAY\n',
            'pause': 'SPOTIFY_PAUSE\n',
            'resume': 'SPOTIFY_RESUME\n',
            'volume_up': 'SPOTIFY_VOLUME_UP\n',
            'volume_down': 'SPOTIFY_VOLUME_DOWN\n',
        }
        
        if action not in commands:
            return jsonify({'success': False, 'message': f'Unknown action: {action}'}), 400
        
        command = commands[action]
        serial_conn.write(command.encode('utf-8'))
        serial_conn.flush()
        
        action_names = {
            'play': 'Play',
            'pause': 'Pause',
            'resume': 'Resume',
            'volume_up': 'Volume Up',
            'volume_down': 'Volume Down',
        }
        
        print(f"✓ Spotify command sent: {action_names.get(action, action)}")
        return jsonify({'success': True, 'message': f'{action_names.get(action, action)} command sent'})
    except serial.SerialException as e:
        print(f"✗ Serial error during Spotify control: {e}")
        status['serial_connected'] = False
        return jsonify({'success': False, 'message': f'Serial error: {str(e)}'}), 500
    except Exception as e:
        print(f"✗ Error during Spotify control: {e}")
        import traceback
        traceback.print_exc()
        return jsonify({'success': False, 'message': str(e)}), 500

@app.route('/api/port', methods=['POST'])
def api_set_port():
    """Change the serial port."""
    global serial_conn, running, current_port, serial_thread
    
    try:
        data = request.get_json()
        new_port = data.get('port')
        
        if not new_port:
            return jsonify({'success': False, 'message': 'No port specified'}), 400
        
        # Close current connection and update port
        with port_lock:
            if serial_conn and serial_conn.is_open:
                try:
                    serial_conn.close()
                except:
                    pass
            # Update port - this will cause the serial_reader thread to detect the change
            old_port = current_port
            current_port = new_port
            status['serial_connected'] = False
            status['serial_port'] = new_port
        
        # Give the thread a moment to detect the change and break out of its read loop
        time.sleep(0.3)
        
        # If thread is not alive or port didn't change in thread, start a new one
        # The serial_reader will automatically reconnect with the new port
        if not serial_thread or not serial_thread.is_alive():
            serial_thread = threading.Thread(target=serial_reader, args=(new_port,), daemon=True)
            serial_thread.start()
        
        print(f"✓ Switched to port: {new_port} (was {old_port})")
        return jsonify({'success': True, 'message': f'Switched to {new_port}'})
    except Exception as e:
        print(f"✗ Error changing port: {e}")
        import traceback
        traceback.print_exc()
        return jsonify({'success': False, 'message': str(e)}), 500

# AI Assistant Functions
def load_api_key(key_name):
    """Load API key from ~/.env"""
    env_file = Path.home() / '.env'
    if env_file.exists():
        with open(env_file, 'r') as f:
            for line in f:
                line = line.strip()
                if line.startswith(f'{key_name}='):
                    key = line.split('=', 1)[1].strip().strip('"').strip("'")
                    return key
    return None

def load_gemini_key():
    """Load Gemini API key from ~/.env"""
    return load_api_key('GEMINI_API_KEY')

def get_ai_provider():
    """Determine which AI provider to use based on available API keys"""
    gemini_key = load_gemini_key()
    
    return 'gemini' if gemini_key else None

def get_project_root():
    """Get the project root directory"""
    # Assume we're in scripts/, go up one level
    script_dir = Path(__file__).parent
    return script_dir.parent

def read_file_safe(file_path):
    """Safely read a file within the project"""
    project_root = get_project_root()
    full_path = project_root / file_path.lstrip('/')
    
    # Security: ensure path is within project
    try:
        full_path.resolve().relative_to(project_root.resolve())
    except ValueError:
        return None, "Path outside project root"
    
    if not full_path.exists():
        return None, "File not found"
    
    if not full_path.is_file():
        return None, "Not a file"
    
    try:
        with open(full_path, 'r', encoding='utf-8') as f:
            return f.read(), None
    except Exception as e:
        return None, str(e)

def write_file_safe(file_path, content):
    """Safely write a file within the project"""
    project_root = get_project_root()
    full_path = project_root / file_path.lstrip('/')
    
    # Security: ensure path is within project
    try:
        full_path.resolve().relative_to(project_root.resolve())
    except ValueError:
        return False, "Path outside project root"
    
    try:
        full_path.parent.mkdir(parents=True, exist_ok=True)
        with open(full_path, 'w', encoding='utf-8') as f:
            f.write(content)
        return True, None
    except Exception as e:
        return False, str(e)

def get_recent_logs(count=None):
    """Get recent logs for AI context. If count is None, returns all logs."""
    logs = list(status.get('logs', []))
    if count:
        logs = logs[-count:]
    return '\n'.join([f"[{log.get('time', '--')}] {log.get('text', '')}" for log in logs])

def auto_review_logs():
    """Automatically review logs using AI and detect issues"""
    global status
    
    import sys
    sys.stdout.flush()  # Ensure output is flushed
    
    print(f"[AI Review] Triggered - logs: {len(status.get('logs', []))}, reviewing: {status['ai_review']['reviewing']}")
    sys.stdout.flush()
    
    # Don't review if already reviewing or no logs
    if status['ai_review']['reviewing']:
        print("[AI Review] Already reviewing, skipping")
        return
    
    if len(status.get('logs', [])) < 5:
        print("[AI Review] Not enough logs (< 5), skipping")
        return
    
    # Check for errors first - if errors detected, bypass cooldown
    error_count = sum(1 for log in status.get('logs', []) 
                     if 'error' in log.get('text', '').lower() or 
                        'failed' in log.get('text', '').lower() or
                        'E (' in log.get('text', ''))
    
    # Only review every 15 seconds (reduced from 30 for more responsive reviews)
    # But bypass cooldown if errors are detected
    if error_count == 0:
        last_review = status['ai_review'].get('last_review_time')
        if last_review:
            try:
                last_time = datetime.strptime(last_review, "%H:%M:%S")
                now = datetime.now()
                # Create datetime objects with same date for comparison
                last_time = now.replace(hour=last_time.hour, minute=last_time.minute, second=last_time.second)
                elapsed = (now - last_time).total_seconds()
                if elapsed < 15:
                    print(f"[AI Review] Too soon since last review ({elapsed:.1f}s < 15s), skipping")
                    return
            except Exception as e:
                print(f"[AI Review] Error parsing last review time: {e}")
    else:
        print(f"[AI Review] Errors detected ({error_count}), bypassing cooldown")
    
    status['ai_review']['reviewing'] = True
    status['ai_review']['last_review_time'] = datetime.now().strftime("%H:%M:%S")
    
    try:
        # Use Gemini exclusively
        provider = 'gemini'
        api_key = load_gemini_key()
        if not api_key:
            print("[AI Review] GEMINI_API_KEY not found in ~/.env")
            status['ai_review']['reviewing'] = False
            return
        status['ai_provider'] = provider
        print(f"[AI Review] Using {provider.upper()} API, proceeding with call...")
        
        # Get all logs for comprehensive analysis
        all_logs = get_recent_logs()  # No limit - get all logs
        system_context = get_system_context()
        total_logs = len(status.get('logs', []))
        
        # Error count already calculated above, reuse it
        
        # Review if there are errors OR if we have enough logs (>= 10 for more proactive reviews)
        # This ensures we get periodic health checks even without errors
        if error_count == 0 and total_logs < 10:
            print(f"[AI Review] No errors and low activity ({total_logs} logs, {error_count} errors), skipping")
            status['ai_review']['reviewing'] = False
            return
        
        # If we have enough logs (>= 10), always review (even without errors) for health checks
        if total_logs >= 10:
            print(f"[AI Review] Sufficient logs ({total_logs}), proceeding with review for health check")
        
        print(f"[AI Review] Starting review - {total_logs} logs, {error_count} errors")
        
        # For very large log sets, use last 200 for analysis to stay within token limits
        # But mention total count in the prompt
        if total_logs > 200:
            recent_logs = get_recent_logs(200)
            log_note = f"Showing last 200 of {total_logs} total logs"
        else:
            recent_logs = all_logs
            log_note = f"All {total_logs} logs"
        
        # Build review prompt
        review_prompt = f"""Review these device logs and provide a brief analysis:

System Status:
- Serial: {system_context['serial_connected']}
- WiFi: {system_context['wifi']}
- Gemini Status: {system_context['gemini'].get('status', 'Unknown')}
- Gemini Session: {system_context['gemini'].get('session', 'None')}
- Total Errors: {system_context['errors']}
- Gemini Errors: {system_context['gemini_errors']}
- Error Count in Logs: {error_count}

Logs ({log_note}):
{recent_logs}

Provide a brief analysis (2-3 sentences max):
1. Are there any critical errors that need attention?
2. Is the system functioning normally?
3. Any suggestions for improvement?

Keep response concise - just the key issues and recommendations."""
        
        # Call AI API (Gemini exclusively)
        import urllib.request
        
        # Gemini API - use gemini-2.0-flash which is available
        url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key={api_key}"
        headers = {
            "Content-Type": "application/json"
        }
        
        payload = {
            "contents": [{
                "parts": [{
                    "text": f"You are a firmware debugging assistant. Provide concise, actionable analysis of device logs.\n\n{review_prompt}"
                }]
            }]
        }
        
        print(f"[AI Review] Calling Gemini API...")
        req = urllib.request.Request(url, data=json.dumps(payload).encode(), headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=15) as response:
                response_data = response.read()
                print(f"[AI Review] {provider.upper()} response received ({len(response_data)} bytes)")
                result = json.loads(response_data)
                
                # Parse response based on provider
                # Gemini response format: result['candidates'][0]['content']['parts'][0]['text']
                if 'candidates' not in result or len(result['candidates']) == 0:
                    print(f"[AI Review] ERROR: No candidates in Gemini response: {result}")
                    status['ai_review']['last_analysis'] = "Error: No response from Gemini"
                    status['ai_review']['last_review_time'] = datetime.now().strftime("%H:%M:%S")
                    status['ai_review']['reviewing'] = False
                    return
                
                candidate = result['candidates'][0]
                if 'content' not in candidate or 'parts' not in candidate['content'] or len(candidate['content']['parts']) == 0:
                    print(f"[AI Review] ERROR: Invalid Gemini response structure: {candidate}")
                    status['ai_review']['last_analysis'] = "Error: Invalid Gemini response"
                    status['ai_review']['last_review_time'] = datetime.now().strftime("%H:%M:%S")
                    status['ai_review']['reviewing'] = False
                    return
                
                analysis = candidate['content']['parts'][0]['text']
                
                print(f"[AI Review] Analysis received ({len(analysis)} chars): {analysis[:100]}...")
                
                status['ai_review']['last_analysis'] = analysis
                status['ai_review']['last_review_time'] = datetime.now().strftime("%H:%M:%S")
                
                # Extract alerts if there are critical issues
                alerts = []
                suggestions = []
                analysis_lower = analysis.lower()
                
                # Check for API errors first
                if 'http error' in analysis_lower or '429' in analysis or 'quota' in analysis_lower:
                    alerts.append(f"⚠️ Gemini API Error: {analysis}")
                    print(f"[AI Review] API error detected, added as alert")
                elif 'error' in analysis_lower or 'failed' in analysis_lower or 'critical' in analysis_lower or 'issue' in analysis_lower:
                    alerts.append(analysis)
                    print(f"[AI Review] Alert added: {len(alerts)} alerts")
                
                if 'suggest' in analysis_lower or 'recommend' in analysis_lower or 'improve' in analysis_lower:
                    suggestions.append(analysis)
                    print(f"[AI Review] Suggestion added: {len(suggestions)} suggestions")
                
                status['ai_review']['alerts'] = alerts[-3:]  # Keep last 3 alerts
                status['ai_review']['suggestions'] = suggestions[-3:]  # Keep last 3 suggestions
                status['ai_review']['reviewing'] = False  # Mark review as complete
                
                print(f"[AI Review] Review complete - {len(alerts)} alerts, {len(suggestions)} suggestions")
                
        except urllib.error.HTTPError as e:
            error_body = e.read().decode() if hasattr(e, 'read') else str(e)
            print(f"[AI Review] HTTP Error {e.code}: {error_body}")
            error_msg = f"HTTP Error {e.code}: {error_body[:200]}"
            status['ai_review']['last_analysis'] = error_msg
            status['ai_review']['last_review_time'] = datetime.now().strftime("%H:%M:%S")
            status['ai_review']['reviewing'] = False
            # Add as alert if it's a quota/rate limit error
            if e.code == 429 or 'quota' in error_body.lower():
                status['ai_review']['alerts'] = [f"⚠️ {provider.upper()} API Quota Exceeded: {error_msg}"]
            return
        except Exception as e:
            print(f"[AI Review] Request error: {e}")
            import traceback
            traceback.print_exc()
            status['ai_review']['last_analysis'] = f"Error: {str(e)}"
            status['ai_review']['last_review_time'] = datetime.now().strftime("%H:%M:%S")
            status['ai_review']['reviewing'] = False
            return
            
    except Exception as e:
        print(f"[AI Review] Error: {e}")
        import traceback
        traceback.print_exc()
    finally:
        status['ai_review']['reviewing'] = False
        print(f"[AI Review] Review finished, reviewing flag cleared")

def get_latest_build_time():
    """Get the most recent build timestamp from build directory or git."""
    try:
        # Check build directory for most recent build
        build_dir = os.path.join(os.path.dirname(__file__), '..', 'samples', 'korvo_voice_assistant', 'build')
        if os.path.exists(build_dir):
            # Look for build artifacts with timestamps
            bootloader_bin = os.path.join(build_dir, 'bootloader', 'bootloader.bin')
            if os.path.exists(bootloader_bin):
                mtime = os.path.getmtime(bootloader_bin)
                return datetime.fromtimestamp(mtime).strftime("%Y-%m-%d %H:%M:%S")
        
        # Fallback: get latest git commit date
        repo_root = os.path.join(os.path.dirname(__file__), '..')
        try:
            result = subprocess.run(
                ['git', 'log', '-1', '--format=%ci'],
                cwd=repo_root,
                capture_output=True,
                text=True,
                timeout=2
            )
            if result.returncode == 0 and result.stdout.strip():
                # Parse git date format: "2025-11-21 11:52:36 -0700"
                git_date = result.stdout.strip().split()[0:2]  # Get date and time parts
                if len(git_date) >= 2:
                    return f"{git_date[0]} {git_date[1]}"
        except:
            pass
    except:
        pass
    return None

def get_system_context():
    """Get current system status for AI context"""
    return {
        'serial_connected': status.get('serial_connected', False),
        'serial_port': status.get('serial_port', ''),
        'wifi': status.get('wifi', 'Unknown'),
        'gemini': status.get('gemini', {}),
        'errors': status.get('stats', {}).get('errors', 0),
        'gemini_errors': status.get('stats', {}).get('gemini_errors', 0),
        'error_log': status.get('error_log', []),
        'reboot_detected': status.get('reboot_detected', False),
        'sensors': status.get('sensors', {}),
    }

@app.route('/api/ai/chat', methods=['POST'])
def api_ai_chat():
    """AI chat endpoint with file access and build capabilities"""
    try:
        data = request.get_json()
        message = data.get('message', '')
        if not message:
            return jsonify({'error': 'No message provided'}), 400
        
        # Use Gemini exclusively
        provider = 'gemini'
        api_key = load_gemini_key()
        if not api_key:
            return jsonify({'error': 'GEMINI_API_KEY not found in ~/.env'}), 500
        
        status['ai_provider'] = 'gemini'
        
        # Get context - use all logs for comprehensive analysis
        all_logs = get_recent_logs()  # Get all logs
        # For chat, use last 200 if there are many logs to stay within token limits
        if len(status.get('logs', [])) > 200:
            recent_logs = get_recent_logs(200)
        else:
            recent_logs = all_logs
        system_context = get_system_context()
        
        # Build system prompt
        system_prompt = f"""You are an AI coding assistant (like Cursor) helping debug and develop an ESP32-S3 firmware project called "Naphome Voice Assistant".

PROJECT CONTEXT:
- This is an ESP-IDF v5.2 project for ESP32-S3
- Main sample: samples/korvo_voice_assistant/
- Uses Gemini AI for voice transcription, LLM, and TTS
- Audio pipeline: I2S -> AEC -> BSS/NS -> VAD -> Gemini Batch STT
- Also uses WakeNet for local wake word detection
- Project root: {get_project_root()}

CURRENT SYSTEM STATUS:
- Serial Connected: {system_context['serial_connected']}
- Serial Port: {system_context['serial_port']}
- WiFi: {system_context['wifi']}
- Gemini Status: {system_context['gemini'].get('status', 'Unknown')}
- Gemini Session: {system_context['gemini'].get('session', 'None')}
- Total Errors: {system_context['errors']}
- Gemini Errors: {system_context['gemini_errors']}

LOGS ({len(status.get('logs', []))} total entries, showing last 200 for context):
{recent_logs}

YOUR CAPABILITIES:
1. **Read Files**: Use ACTION:read_file:path/to/file to read any file in the project
2. **Edit Files**: Use ACTION:write_file:path/to/file:content to write files (use \\n for newlines)
3. **Build Firmware**: Use ACTION:build to compile the firmware
4. **Flash Firmware**: Use ACTION:flash:port to flash to device (default: /dev/cu.usbserial-110)
5. **Analyze & Debug**: Interpret logs, suggest fixes, explain code

ACTION FORMAT:
- Read: ACTION:read_file:samples/korvo_voice_assistant/main/file.c
- Write: ACTION:write_file:path/to/file:line1\\nline2\\nline3
- Build: ACTION:build
- Flash: ACTION:flash:/dev/cu.usbserial-110

IMPORTANT RULES:
- Always read files before editing to understand context
- For file writes, include the COMPLETE file content (not just diffs)
- Use \\n for newlines in file content
- Analyze logs to understand issues before suggesting fixes
- Be proactive - if you see errors in logs, suggest fixes
- Explain your reasoning when making changes

When the user asks you to do something:
1. If you need to see code, use read_file first
2. Analyze the situation
3. Make changes using write_file
4. If they ask to build/flash, use the appropriate action
5. Always explain what you're doing and why"""
        
        # Call AI API (Gemini exclusively)
        import urllib.request
        import urllib.parse
        
        # Force Gemini for all chat interactions
        provider = 'gemini'
        api_key = load_gemini_key()
        if not api_key:
            return jsonify({'error': 'GEMINI_API_KEY not found in ~/.env'}), 500
        
        # Use Gemini API
        url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key={api_key}"
        headers = {
            "Content-Type": "application/json"
        }
        
        # Gemini API format
        payload = {
            "contents": [{
                "parts": [{
                    "text": f"{system_prompt}\n\nUser: {message}\nAssistant:"
                }]
            }],
            "generationConfig": {
                "temperature": 0.7,
                "maxOutputTokens": 2000
            }
        }
        
        req = urllib.request.Request(url, data=json.dumps(payload).encode(), headers=headers)
        with urllib.request.urlopen(req, timeout=30) as response:
            result = json.loads(response.read())
            
            # Extract Gemini response
            if 'candidates' in result and len(result['candidates']) > 0:
                candidate = result['candidates'][0]
                if 'content' in candidate and 'parts' in candidate['content']:
                    parts = candidate['content']['parts']
                    if len(parts) > 0 and 'text' in parts[0]:
                        ai_response = parts[0]['text'].strip()
                    else:
                        ai_response = "Error: No text in Gemini response"
                else:
                    ai_response = "Error: Unexpected Gemini response format"
            else:
                ai_response = "Error: No candidates in Gemini response"
            
            # Check for actions
            actions = []
            if 'ACTION:' in ai_response:
                lines = ai_response.split('\n')
                new_response = []
                for line in lines:
                    if line.strip().startswith('ACTION:'):
                        action = line.strip()[7:]  # Remove 'ACTION:'
                        actions.append(action)
                    else:
                        new_response.append(line)
                ai_response = '\n'.join(new_response)
            
            return jsonify({
                'response': ai_response,
                'actions': actions,
                'provider': 'gemini'
            })
            
    except Exception as e:
        import traceback
        traceback.print_exc()
        return jsonify({'error': str(e)}), 500

@app.route('/api/ai/read_file', methods=['POST'])
def api_ai_read_file():
    """Read a file for the AI assistant"""
    try:
        data = request.get_json()
        file_path = data.get('path', '')
        if not file_path:
            return jsonify({'error': 'No path provided'}), 400
        
        content, error = read_file_safe(file_path)
        if error:
            return jsonify({'error': error}), 400
        
        return jsonify({'content': content, 'path': file_path})
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/ai/write_file', methods=['POST'])
def api_ai_write_file():
    """Write a file for the AI assistant"""
    try:
        data = request.get_json()
        file_path = data.get('path', '')
        content = data.get('content', '')
        
        if not file_path:
            return jsonify({'error': 'No path provided'}), 400
        
        # Replace \n with actual newlines
        content = content.replace('\\n', '\n')
        
        success, error = write_file_safe(file_path, content)
        if not success:
            return jsonify({'error': error}), 400
        
        return jsonify({'success': True, 'path': file_path})
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/ai/build', methods=['POST'])
def api_ai_build():
    """Build the firmware"""
    try:
        project_root = get_project_root()
        sample_dir = project_root / 'samples' / 'korvo_voice_assistant'
        
        if not sample_dir.exists():
            return jsonify({'error': 'Sample directory not found'}), 404
        
        # Run build
        result = subprocess.run(
            ['idf.py', 'build'],
            cwd=str(sample_dir),
            capture_output=True,
            text=True,
            timeout=300
        )
        
        return jsonify({
            'success': result.returncode == 0,
            'stdout': result.stdout,
            'stderr': result.stderr,
            'returncode': result.returncode
        })
    except subprocess.TimeoutExpired:
        return jsonify({'error': 'Build timed out'}), 500
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/api/ai/auto-review', methods=['POST'])
def api_ai_auto_review():
    """Trigger automatic log review"""
    try:
        print(f"[API] Manual review trigger requested")
        # Run in background thread to avoid blocking
        def run_review():
            auto_review_logs()
        threading.Thread(target=run_review, daemon=True).start()
        return jsonify({
            'success': True,
            'last_review': status['ai_review'].get('last_review_time'),
            'analysis': status['ai_review'].get('last_analysis'),
            'reviewing': status['ai_review'].get('reviewing', False)
        })
    except Exception as e:
        print(f"[API] Error triggering review: {e}")
        import traceback
        traceback.print_exc()
        return jsonify({'error': str(e)}), 500

@app.route('/api/ai/flash', methods=['POST'])
def api_ai_flash():
    """Flash the firmware"""
    try:
        data = request.get_json()
        port = data.get('port', status.get('serial_port', '/dev/cu.usbserial-110'))
        
        project_root = get_project_root()
        sample_dir = project_root / 'samples' / 'korvo_voice_assistant'
        
        if not sample_dir.exists():
            return jsonify({'error': 'Sample directory not found'}), 404
        
        # Run flash
        result = subprocess.run(
            ['idf.py', '-p', port, 'flash'],
            cwd=str(sample_dir),
            capture_output=True,
            text=True,
            timeout=120
        )
        
        return jsonify({
            'success': result.returncode == 0,
            'stdout': result.stdout,
            'stderr': result.stderr,
            'returncode': result.returncode
        })
    except subprocess.TimeoutExpired:
        return jsonify({'error': 'Flash timed out'}), 500
    except Exception as e:
        return jsonify({'error': str(e)}), 500

def discover_device_id_from_certs():
    """Discover device ID from certificate files in somnus-iot-cert directory"""
    cert_dir = Path(__file__).parent.parent / "somnus-iot-cert"
    if not cert_dir.exists():
        return None
    
    # Look for certificate files with device ID pattern
    cert_files = list(cert_dir.glob("*-certificate.pem.crt"))
    if cert_files:
        # Extract device ID from filename (e.g., "SOMNUS_7A356722B383-certificate.pem.crt")
        cert_name = cert_files[0].stem.replace("-certificate.pem", "")
        if cert_name.startswith("SOMNUS_"):
            return cert_name
    
    # Also check subdirectories
    for subdir in cert_dir.iterdir():
        if subdir.is_dir() and subdir.name.startswith("SOMNUS_"):
            return subdir.name
    
    return None

def on_mqtt_connect(client, userdata, flags, rc):
    """MQTT connection callback"""
    global mqtt_connected
    if rc == 0:
        mqtt_connected = True
        with mqtt_lock:
            status['mqtt']['connected'] = True
            status['mqtt']['device_id'] = mqtt_device_id
        print(f"[MQTT] Connected to AWS IoT Core")
        
        # Subscribe to topics
        if mqtt_device_id:
            topics = [
                f"device/telemetry/{mqtt_device_id}",  # Sensor data telemetry
                f"device/receive/uat/{mqtt_device_id}",  # Commands/logs
                f"device/somnus/{mqtt_device_id}",  # Somnus commands
            ]
            subscribed = []
            for topic in topics:
                result, mid = client.subscribe(topic, qos=1)
                if result == mqtt.MQTT_ERR_SUCCESS:
                    subscribed.append(topic)
                    print(f"[MQTT] ✅ Subscribed to: {topic}")
                else:
                    print(f"[MQTT] ❌ Failed to subscribe to: {topic} (code: {result})")
            
            with mqtt_lock:
                status['mqtt']['subscribed_topics'] = subscribed
                status['mqtt']['device_id'] = mqtt_device_id
    else:
        mqtt_connected = False
        with mqtt_lock:
            status['mqtt']['connected'] = False
        print(f"[MQTT] Connection failed with code {rc}")

def on_mqtt_disconnect(client, userdata, rc):
    """MQTT disconnection callback"""
    global mqtt_connected
    mqtt_connected = False
    with mqtt_lock:
        status['mqtt']['connected'] = False
    print(f"[MQTT] Disconnected from AWS IoT Core")

def on_mqtt_message(client, userdata, msg):
    """MQTT message callback"""
    global mqtt_connected
    try:
        topic = msg.topic
        payload = msg.payload.decode('utf-8')
        
        # Update MQTT status
        with mqtt_lock:
            status['mqtt']['messages_received'] += 1
            status['mqtt']['last_message_time'] = datetime.now().strftime("%H:%M:%S")
        
        # Parse JSON payload
        try:
            data = json.loads(payload)
        except json.JSONDecodeError:
            data = {"raw": payload}
        
        # Update status based on topic
        with mqtt_lock:
            if "telemetry" in topic:
                # Telemetry data - update sensor readings
                # sensor_manager publishes: {"deviceId": "...", "timestamp_ms": ..., "sensor_name": {...}, ...}
                if isinstance(data, dict):
                    # Update timestamp
                    if "timestamp_ms" in data:
                        status['sensors']['last_update_ms'] = int(time.time() * 1000)
                    
                    # Parse sensor data from top-level keys (sensor names)
                    # Each sensor is a key with its own object containing fields
                    for key, value in data.items():
                        if key in ['deviceId', 'timestamp_ms']:
                            continue
                        
                        # Value should be a dict with sensor readings
                        if isinstance(value, dict):
                            # Extract common sensor fields
                            if "temperature_c" in value or "temperature" in value:
                                temp = value.get("temperature_c") or value.get("temperature")
                                if temp is not None:
                                    status['sensors']['temperature_c'] = float(temp)
                            
                            if "humidity_rh" in value or "humidity" in value:
                                hum = value.get("humidity_rh") or value.get("humidity")
                                if hum is not None:
                                    status['sensors']['humidity_rh'] = float(hum)
                            
                            if "voc_index" in value or "voc" in value:
                                voc = value.get("voc_index") or value.get("voc")
                                if voc is not None:
                                    status['sensors']['voc_index'] = int(voc)
                            
                            if "co2_ppm" in value or "co2" in value:
                                co2 = value.get("co2_ppm") or value.get("co2")
                                if co2 is not None:
                                    status['sensors']['co2_ppm'] = float(co2)
                            
                            if "ambient_lux" in value or "lux" in value:
                                lux = value.get("ambient_lux") or value.get("lux")
                                if lux is not None:
                                    status['sensors']['ambient_lux'] = int(lux)
                            
                            if "pm2_5_ug_m3" in value or "pm25" in value or "pm2_5" in value:
                                pm = value.get("pm2_5_ug_m3") or value.get("pm25") or value.get("pm2_5")
                                if pm is not None:
                                    status['sensors']['pm2_5_ug_m3'] = float(pm)
                            
                            if "proximity" in value:
                                prox = value.get("proximity")
                                if prox is not None:
                                    status['sensors']['proximity'] = int(prox)
                    
                    # Also check for flat structure (backward compatibility)
                    if "temperature_c" in data:
                        status['sensors']['temperature_c'] = data.get('temperature_c')
                    if "humidity_rh" in data:
                        status['sensors']['humidity_rh'] = data.get('humidity_rh')
                    if "voc_index" in data:
                        status['sensors']['voc_index'] = data.get('voc_index')
                    if "co2_ppm" in data:
                        status['sensors']['co2_ppm'] = data.get('co2_ppm')
                    if "ambient_lux" in data:
                        status['sensors']['ambient_lux'] = data.get('ambient_lux')
                    if "pm2_5_ug_m3" in data:
                        status['sensors']['pm2_5_ug_m3'] = data.get('pm2_5_ug_m3')
                
                # Update sensor last update time
                status['sensors']['last_update_ms'] = int(time.time() * 1000)
                
                # Add to logs (truncate for display)
                telemetry_summary = {}
                for key, value in data.items():
                    if key not in ['deviceId', 'timestamp_ms']:
                        if isinstance(value, dict):
                            # Summarize sensor data
                            sensor_summary = {}
                            for k, v in value.items():
                                if isinstance(v, (int, float)):
                                    sensor_summary[k] = v
                            if sensor_summary:
                                telemetry_summary[key] = sensor_summary
                        else:
                            telemetry_summary[key] = value
                
                log_entry = {
                    'time': datetime.now().strftime("%H:%M:%S"),
                    'text': f"[MQTT Telemetry] {json.dumps(telemetry_summary, indent=2)[:300]}",
                    'level': 'info'
                }
                status['logs'].append(log_entry)
                if len(status['logs']) > 1000:
                    status['logs'].popleft()
                
                # Update MQTT status
                status['mqtt']['last_telemetry_time'] = datetime.now().strftime("%H:%M:%S")
                    
            elif "receive/uat" in topic or "somnus" in topic:
                # Command/control or log data
                log_entry = {
                    'time': datetime.now().strftime("%H:%M:%S"),
                    'text': f"[MQTT] {topic}: {payload[:200]}",
                    'level': 'info'
                }
                status['logs'].append(log_entry)
                if len(status['logs']) > 1000:
                    status['logs'].popleft()
        
        print(f"[MQTT] Received on {topic}: {payload[:100]}")
        
    except Exception as e:
        print(f"[MQTT] Error processing message: {e}")

def start_mqtt_client():
    """Start AWS IoT MQTT client"""
    global mqtt_client, mqtt_device_id
    
    if not MQTT_AVAILABLE:
        print("[MQTT] paho-mqtt not available, skipping AWS IoT subscription")
        return
    
    # Discover device ID
    mqtt_device_id = discover_device_id_from_certs()
    if not mqtt_device_id:
        print("[MQTT] Could not discover device ID from certificates, skipping MQTT subscription")
        return
    
    print(f"[MQTT] Discovered device ID: {mqtt_device_id}")
    
    # Find certificate files
    cert_dir = Path(__file__).parent.parent / "somnus-iot-cert"
    cert_file = None
    key_file = None
    ca_file = cert_dir / "AmazonRootCA1.pem"
    
    # Look for certificate files
    cert_files = list(cert_dir.glob(f"{mqtt_device_id}*certificate.pem.crt"))
    key_files = list(cert_dir.glob(f"{mqtt_device_id}*private.pem.key"))
    
    if cert_files:
        cert_file = cert_files[0]
    if key_files:
        key_file = key_files[0]
    
    # Also check subdirectories
    device_dir = cert_dir / mqtt_device_id
    if device_dir.exists():
        if not cert_file:
            cert_file = device_dir / "device_cert.pem"
        if not key_file:
            key_file = device_dir / "private_key.pem"
        if not ca_file.exists():
            ca_file = device_dir / "root_ca.pem"
    
    if not cert_file or not key_file or not ca_file.exists():
        print(f"[MQTT] Certificate files not found. Cert: {cert_file}, Key: {key_file}, CA: {ca_file}")
        print("[MQTT] Skipping MQTT subscription")
        return
    
    print(f"[MQTT] Using certificates: cert={cert_file}, key={key_file}, ca={ca_file}")
    
    # AWS IoT endpoint
    endpoint = "a2w3ko3hrweita-ats.iot.ap-south-1.amazonaws.com"
    port = 8883
    
    # Create MQTT client
    client_id = f"dashboard_{int(time.time())}"
    mqtt_client = mqtt.Client(client_id=client_id)
    
    # Configure TLS
    mqtt_client.tls_set(
        ca_certs=str(ca_file),
        certfile=str(cert_file),
        keyfile=str(key_file),
        cert_reqs=ssl.CERT_REQUIRED,
        tls_version=ssl.PROTOCOL_TLSv1_2,
        ciphers=None
    )
    
    # Set callbacks
    mqtt_client.on_connect = on_mqtt_connect
    mqtt_client.on_disconnect = on_mqtt_disconnect
    mqtt_client.on_message = on_mqtt_message
    
    # Connect in background thread
    def mqtt_connect_thread():
        try:
            print(f"[MQTT] Connecting to {endpoint}:{port}...")
            mqtt_client.connect(endpoint, port, keepalive=60)
            mqtt_client.loop_start()
        except Exception as e:
            print(f"[MQTT] Connection error: {e}")
            global mqtt_connected
            mqtt_connected = False
    
    mqtt_thread = threading.Thread(target=mqtt_connect_thread, daemon=True)
    mqtt_thread.start()

def auto_detect_naphome_port():
    """Auto-detect the Naphome device serial port."""
    import glob
    import os
    
    # Priority order for detection:
    # 1. ESP32-S3 specific patterns
    # 2. Common USB serial adapters (CH340, CP210, FTDI)
    # 3. Generic USB serial ports
    
    detected_ports = []
    
    # Get all available ports using pyserial
    try:
        available_ports = serial.tools.list_ports.comports()
        for port in available_ports:
            device = port.device
            desc = (port.description or '').lower()
            manufacturer = (port.manufacturer or '').lower()
            
            # Check for ESP32 indicators
            is_esp32 = any(keyword in desc or keyword in manufacturer for keyword in [
                'esp32', 'ch340', 'cp210', 'ftdi', 'silicon labs', 'ch9102'
            ])
            
            # Check for common USB serial patterns
            is_usb_serial = any(pattern in device.lower() for pattern in [
                'usbserial', 'ttyusb', 'ttyacm', 'cu.'
            ])
            
            if is_esp32 or is_usb_serial:
                # Try to verify the port is accessible
                try:
                    test_ser = serial.Serial(device, 115200, timeout=0.1)
                    test_ser.close()
                    detected_ports.append({
                        'device': device,
                        'description': port.description or device,
                        'manufacturer': port.manufacturer or '',
                        'priority': 1 if is_esp32 else 2
                    })
                except:
                    pass
    except Exception as e:
        print(f"[Auto-detect] Error scanning ports: {e}")
    
    # Also check macOS-specific paths
    try:
        mac_ports = glob.glob('/dev/cu.*')
        for port_path in mac_ports:
            if 'Bluetooth' in port_path or 'modem' in port_path.lower():
                continue
            
            # Check if already in detected_ports
            if any(p['device'] == port_path for p in detected_ports):
                continue
            
            # Check for USB serial patterns
            if any(pattern in port_path.lower() for pattern in ['usbserial', 'usbmodem']):
                try:
                    test_ser = serial.Serial(port_path, 115200, timeout=0.1)
                    test_ser.close()
                    detected_ports.append({
                        'device': port_path,
                        'description': os.path.basename(port_path),
                        'manufacturer': '',
                        'priority': 3
                    })
                except:
                    pass
    except Exception as e:
        print(f"[Auto-detect] Error checking macOS paths: {e}")
    
    # Sort by priority (lower is better)
    detected_ports.sort(key=lambda x: x['priority'])
    
    if detected_ports:
        selected = detected_ports[0]['device']
        print(f"[Auto-detect] Found {len(detected_ports)} potential device(s):")
        for p in detected_ports[:3]:  # Show top 3
            print(f"  - {p['device']}: {p['description']} ({p['manufacturer']})")
        print(f"[Auto-detect] Selected: {selected}")
        return selected
    
    return None

def main():
    import sys
    global running, serial_thread, current_port
    
    # Auto-detect port if not provided as argument
    if len(sys.argv) > 1:
        port = sys.argv[1]
        print(f"[Startup] Using port from argument: {port}")
    else:
        print("[Startup] Auto-detecting Naphome device port...")
        port = auto_detect_naphome_port()
        
        if not port:
            # Fallback to default or try common patterns
            fallback_ports = [
                "/dev/cu.usbserial-110",
                "/dev/cu.usbserial-0001",
                "/dev/cu.usbmodem*",
                "/dev/ttyUSB0",
                "/dev/ttyACM0",
            ]
            
            print("[Startup] No device auto-detected, trying fallback ports...")
            port = None
            for fallback in fallback_ports:
                if '*' in fallback:
                    # Handle glob patterns
                    import glob
                    matches = glob.glob(fallback)
                    if matches:
                        port = matches[0]
                        break
                else:
                    try:
                        test_ser = serial.Serial(fallback, 115200, timeout=0.1)
                        test_ser.close()
                        port = fallback
                        break
                    except:
                        continue
            
            if not port:
                print("[Startup] ⚠️  No device found. Dashboard will start but serial monitoring will be disabled.")
                print("[Startup]    Connect your device and use the port selector in the dashboard.")
                port = None
            else:
                print(f"[Startup] Using fallback port: {port}")
    
    current_port = port
    
    # Ensure running is True before starting thread (already set at module level)
    
    # Start serial reader in background thread (only if port detected)
    if port:
        serial_thread = threading.Thread(target=serial_reader, args=(port,), daemon=True)
        serial_thread.start()
        
        # Give thread a moment to connect
        time.sleep(0.5)
    else:
        print("[Startup] Serial monitoring disabled - no port available")
        status['serial_connected'] = False
        status['serial_port'] = ''
    
    # Initialize AI provider on startup (Gemini exclusively)
    status['ai_provider'] = 'gemini'
    
    # Load error log from file
    load_error_log()
    provider_name = status['ai_provider'].upper() if status['ai_provider'] else 'None'
    
    # Start MQTT client
    start_mqtt_client()
    
    print(f"\n{'='*60}")
    print("  Naphome Voice Assistant Web Dashboard")
    print(f"{'='*60}")
    print(f"  🌐 Open your browser to: http://localhost:5001")
    if port:
        print(f"  📡 Monitoring serial port: {port}")
    else:
        print(f"  📡 Serial port: Not connected (use dashboard to select port)")
    print(f"  🤖 AI Provider: {provider_name}")
    if mqtt_device_id:
        print(f"  ☁️  AWS IoT MQTT: Subscribing to device/{mqtt_device_id}")
    print(f"  ⏹️  Press Ctrl+C to stop")
    print(f"{'='*60}\n")
    
    try:
        app.run(host='127.0.0.1', port=5001, debug=False, use_reloader=False)
    except KeyboardInterrupt:
        print("\n\nStopping...")
        global running
        running = False
        if serial_conn:
            serial_conn.close()
        if mqtt_client:
            mqtt_client.loop_stop()
            mqtt_client.disconnect()

if __name__ == "__main__":
    main()
