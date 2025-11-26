# OpenAI Realtime API with AFE Integration

## Summary

Implemented OpenAI Realtime API streaming with AFE (Audio Front-End) processing, skipping wake word detection.

## Key Changes

### 1. OpenAI Realtime WebSocket Client (`samples/korvo_voice_assistant/main/openai_client.c`)
- Added `openai_realtime_start()` - Initializes WebSocket connection to OpenAI Realtime API
- Added `openai_realtime_send_audio()` - Streams audio chunks via WebSocket
- Added `openai_realtime_stop()` - Closes WebSocket connection
- WebSocket event handler parses SSE events:
  - `session.created` - Session establishment
  - `response.audio_transcript.delta` - Partial transcriptions
  - `response.audio_transcript.done` - Final transcriptions
  - `error` - Error handling

### 2. Voice Pipeline Integration (`samples/korvo_voice_assistant/main/voice_pipeline.c`)
- Added `use_realtime_streaming` and `skip_wake_word` config flags
- New `voice_pipeline_realtime_stream_task()` - Continuous audio streaming
- AFE integration when `CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE=y`:
  - Initializes AFE with beamforming, noise suppression, AGC
  - Processes audio through AFE before streaming to OpenAI
  - Skips wake word detection (`wakenet_init = false`)
- Streams audio at 24kHz (OpenAI Realtime requirement)
- Real-time transcription callbacks trigger intent routing

### 3. Main Application (`samples/korvo_voice_assistant/main/naphome_voice_assistant_main.c`)
- Enabled `use_realtime_streaming = true`
- Enabled `skip_wake_word = true`
- Wake word service is skipped when realtime streaming is enabled

## How It Works

```
Raw I2S Audio → AFE Processing (Beamforming/NS/AGC)
              ↓
        AFE-Processed Audio (mono, enhanced)
              ↓
    OpenAI Realtime WebSocket (24kHz PCM16)
              ↓
    Real-time Transcription Events
              ↓
        Intent Routing & Actions
```

## Build Requirements

- `esp_websocket_client` component (ESP-IDF)
- `CONFIG_KORVO_FARFIELD_WAKE_WORD_ENABLE=y` for AFE support
- OpenAI API key in `openai_secrets.h`

## Usage

The assistant now continuously streams AFE-processed audio to OpenAI Realtime API without wake word detection. Transcriptions are processed in real-time and trigger intent actions.
