# Voice Interaction Test Script

Interactive Python script to test and monitor the voice assistant's STT, GPT, TTS, and WakeNet functionality.

## Features

- **Real-time log monitoring** from ESP32-S3 serial port
- **Color-coded event highlighting**:
  - 🟢 **Green**: STT final transcriptions, WebSocket connections
  - 🔵 **Blue**: GPT requests
  - 🟣 **Magenta**: GPT responses
  - 🟡 **Yellow**: TTS audio generation
  - 🟠 **Orange**: Wake word detections
  - 🔴 **Red**: Errors
- **Event tracking**: Captures and summarizes STT, GPT, TTS, and WakeNet events
- **Two modes**:
  - **Interactive mode**: Prompts you to speak, then waits for response
  - **Continuous mode**: Continuously monitors logs without prompts
- **Auto-detection** of serial port
- **Event summaries** with transcription and response history

## Installation

Requires `pyserial`:

```bash
pip install pyserial
```

## Usage

### Interactive Mode (Default)

Prompts you to speak and shows summaries after each interaction:

```bash
python3 scripts/test_voice_interaction.py
```

Or with explicit port:

```bash
python3 scripts/test_voice_interaction.py --port /dev/cu.usbserial-110
```

**Controls:**
- Press `Enter` to start listening for voice interaction
- Type `s` + Enter to show summary of all events
- Type `q` + Enter to quit
- `Ctrl+C` to exit

### Continuous Monitoring Mode

Continuously monitors logs without prompts:

```bash
python3 scripts/test_voice_interaction.py --continuous
```

**Features:**
- Shows all logs in real-time with color coding
- Automatically prints summaries when important events occur
- Auto-summary every 60 seconds
- Press `Ctrl+C` to exit

## What to Look For

The script highlights these key events:

1. **STT (Speech-to-Text)**:
   - `OpenAI STT (partial): "text"` - Partial transcription
   - `OpenAI STT (FINAL): "text"` - Final transcription

2. **GPT Processing**:
   - `Sending to GPT-4o chat: "text"` - Request sent to GPT
   - `GPT Chat Response: "text"` - GPT's response

3. **TTS (Text-to-Speech)**:
   - `TTS generated X bytes` - Audio generated

4. **Wake Word Detection**:
   - `*** WAKE WORD DETECTED (local control): hi esp ***` - Local wake word triggered

5. **Connection Status**:
   - `WebSocket connected to OpenAI Realtime API` - Connected
   - `Session created: <id>` - Session established
   - `OpenAI error:` - Connection or API errors

## Example Output

```
================================================================================
Voice Assistant Interactive Test
================================================================================

Connected to /dev/cu.usbserial-110 at 115200 baud
Monitoring logs from /dev/cu.usbserial-110...
Press Ctrl+C to exit

>>> WebSocket connected
>>> Session created: sess_abc123

────────────────────────────────────────────────────────────────────────────────
Ready for voice interaction
Say something or trigger wake word 'hi esp' for local control
Press Enter to start listening, 'q' to quit, 's' for summary

Listening... (waiting up to 30 seconds for response)
────────────────────────────────────────────────────────────────────────────────

>>> STT partial: "hello"
>>> STT FINAL: "hello, how are you?"
>>> Sending to GPT: "hello, how are you?"
>>> GPT Response: "Hello! I'm doing well, thank you for asking. How can I help you today?"
>>> TTS generated: 45678 bytes

Captured 5 new events

================================================================================
Event Summary (5 events)
================================================================================

STT Transcriptions (1):
  • "hello, how are you?"

GPT Responses (1):
  • "Hello! I'm doing well, thank you for asking. How can I help you today?"

TTS Audio Generated (1):
  • 45678 bytes

================================================================================
```

## Troubleshooting

### No serial port found
- Check USB connection
- List available ports: `ls /dev/cu.*` (macOS) or `ls /dev/tty*` (Linux)
- Specify port explicitly with `--port`

### No events captured
- Verify device is running and connected to Wi-Fi
- Check that OpenAI API key is configured
- Look for WebSocket connection errors in logs
- Ensure microphone is working (check for audio capture logs)

### Permission denied
On Linux, you may need to add your user to the `dialout` group:
```bash
sudo usermod -a -G dialout $USER
# Then log out and back in
```

## Command Line Options

```
--port, -p PORT     Serial port (default: auto-detect)
--baud, -b BAUD     Baud rate (default: 115200)
--continuous, -c    Run in continuous monitoring mode
--help, -h          Show help message
```