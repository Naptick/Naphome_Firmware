# Korvo-1 ↔ OpenAI Voice Demo

This standalone ESP-IDF project records a short clip from the Korvo-1’s on-board PDM microphone, sends it to OpenAI for transcription, and then uses the returned text to synthesize speech with OpenAI TTS.

## Prerequisites

- ESP-IDF v5.0+
- Korvo-1 (ESP32-S3) dev kit connected over USB
- Working Wi-Fi network credentials
- OpenAI API key with access to the `gpt-4o-mini-transcribe` and `gpt-4o-mini-tts` models

## One-Time Setup

1. **Store your key securely**

   ```bash
   cat >> ~/.env <<'EOF'
   OPENAI_API_KEY=sk-your-key
   EOF
   ```

   Never commit this file. The sample’s `CMakeLists.txt` reads the key directly from `~/.env` during configuration.

2. **Export ESP-IDF and enter the sample folder**

   ```bash
   . $IDF_PATH/export.sh
   cd samples/korvo_openai_voice
   ```

3. **Configure Wi-Fi + demo knobs**

   ```bash
   idf.py menuconfig
   # Navigate to "Korvo OpenAI Voice Demo" and fill in:
   #   - Wi-Fi SSID / Password
   #   - Capture duration (ms), sample rate, preferred TTS voice
   ```

4. (Optional) Create a local `sdkconfig.defaults` so you don’t have to re-enter Wi-Fi credentials every time.

## Build, Flash, Monitor

```bash
idf.py set-target esp32s3
idf.py build flash monitor
```

Output example:

```
I (1234) openai_demo: Connecting to Wi-Fi SSID=StudioLab
I (4567) openai_demo: Captured 32000 samples
I (8910) openai_client: Transcript: "hello korvo sample"
I (9012) openai_demo: Generated 32768 bytes of WAV audio from OpenAI
```

### What the demo does

1. Boots Wi-Fi STA and waits for an IP.
2. Uses `drivers/audio/korvo1` to stream 16 kHz PCM for the configured capture window.
3. Wraps the raw PCM in a minimal WAV header, Base64-encodes it, and calls `POST /v1/responses`.
4. Parses the transcribed text, echoes it to the monitor, and sends it to `POST /v1/audio/speech`.
5. Logs how many TTS bytes came back (you can extend this to play the WAV through your DAC).

### Extending the sample

- Pipe the TTS WAV into your PCM5102 / ES8388 playback chain for full round-trip audio.
- Stream longer captures by chunking them before encoding.
- Swap the transcription or TTS models via `openai_client.c`.

> ⚠️ The demo blocks while it waits for OpenAI responses. For production, move the cloud calls into a dedicated task and add retries/backoff. Also consider encrypting the API key in NVS rather than embedding it at build time.
