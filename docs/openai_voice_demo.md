# Korvo-1 + OpenAI Voice Sample

This guide covers the `samples/korvo_openai_voice` project that records audio on the Korvo-1 board, calls OpenAI Speech-to-Text (STT), and feeds the transcription into OpenAI Text-to-Speech (TTS).

## Why a separate sample?

- Keeps the production `main/` app lean while we experiment.
- Demonstrates how to fetch secrets from `~/.env` instead of committing them.
- Provides a reference for `esp_http_client` + JSON payloads against OpenAI’s `responses` and `audio/speech` endpoints.

## Setup Checklist

1. **Install ESP-IDF v5.0+** and export the environment: `. $IDF_PATH/export.sh`
2. **Create `~/.env` with your OpenAI key**
   ```
   OPENAI_API_KEY=sk-your-key
   ```
   The sample’s CMake script refuses to configure unless it finds this line.
3. **Configure Wi-Fi + capture parameters**
   ```
   cd samples/korvo_openai_voice
   idf.py menuconfig
   ```
   Fill in the options under **Korvo OpenAI Voice Demo**.
4. **Build/flash/monitor**
   ```
   idf.py set-target esp32s3
   idf.py build flash monitor
   ```

## Data Flow

1. `korvo_audio.c` configures the on-board PDM microphone (GPIO19/18/17/0) and captures `CONFIG_KORVO_OPENAI_CAPTURE_MS` worth of 16 kHz PCM.
2. `openai_client.c` wraps the PCM in a minimal WAV header, Base64-encodes it, and posts JSON to `https://api.openai.com/v1/responses` with the `gpt-4o-mini-transcribe` model.
3. The parsed transcript is logged and sent to `https://api.openai.com/v1/audio/speech` (`gpt-4o-mini-tts`, voice configurable). The demo only logs the WAV size/bytes, but you can feed it into your playback chain.

## Security Notes

- Secrets never touch the repo. The key is read from `~/.env` during configuration and compiled straight into the binary.
- For a production design, move the key into secure storage (NVS key partition or remote provisioning) and add network retries, exponential backoff, and quota checks.
- The demo uses the ESP-IDF certificate bundle (`esp_crt_bundle_attach`). Ensure `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE` stays enabled.

## Next Steps

- Replace the simple blocking loop with a FreeRTOS task that streams audio in chunks and pushes transcripts into MQTT.
- Integrate the PCM5102/ES8388 playback driver to render TTS audio over the Korvo speaker path.
- Swap out the OpenAI request helpers for your preferred provider by editing `openai_client.c`.
