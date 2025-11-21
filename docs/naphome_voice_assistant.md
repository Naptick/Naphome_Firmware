# Naphome Voice Assistant (Korvo-1)

## Purpose

- Build a Korvo-1 (ESP32-S3) firmware that stays in low-power listen mode, wakes on the keyword “Naphome,” streams speech to OpenAI Speech-to-Text, generates spoken confirmations through OpenAI Text-to-Speech, and issues Spotify Connect playback commands (play/pause, resume, volume).
- Reuse the repo’s `drivers/audio/korvo1`, `audio_manager`, Wi-Fi, MQTT, and task infrastructure to keep differences from the production app minimal.
- Surface local sensor telemetry (temperature, IAQ, etc.) and remote control hooks through AWS IoT so cloud or mobile clients can monitor and steer the assistant.
- Reflect assistant state over Korvo-1’s WS2812 ring and acknowledge hardware button presses with spoken LLM responses.

## Platform Assumptions

| Area | Decision |
| --- | --- |
| Target | `korvo1` board, `idf.py set-target esp32s3` |
| Microphones | On-board PDM array (GPIO19/18/17/0), 16 kHz mono PCM |
| Playback | ES8388 DAC on Korvo-1 speaker path |
| Keyword Engine | ESP-SR wake word model trained for “Naphome” (command-and-control profile, ~20 KB) |
| Cloud APIs | OpenAI `responses` (STT) + `audio/speech` (TTS) |
| Media Control | Spotify Web API + Connect / Player endpoints (device ID discovered at runtime) |
| Cloud Control Plane | AWS IoT Core (Somnus topics) for telemetry + inbound control |

## High-Level Flow

1. **Idle / Wake Word**
   - `wake_word_task` continuously reads mic buffers from `audio_manager`.
   - The current prototype uses a lightweight RMS-based detector tuned by `CONFIG_KVA_WAKE_WORD_SENSITIVITY` while we prep the ESP-SR assets.
   - When the “Naphome” model (or the interim detector) exceeds threshold, it signals `voice_pipeline`.
2. **Listening**
   - `voice_pipeline` arms a recorder (2–5 seconds, extendable).
   - Audio is serialized as little-endian PCM16, wrapped in WAV, Base64 encoded.
3. **STT via OpenAI**
   - `openai_client` submits `POST /v1/responses` with `gpt-4o-mini-transcribe` (or `whisper-1` drop-in).
   - Transcription arrives as JSON; parsed text is handed to the intent router.
4. **Intent Routing**
   - `command_router` matches normalized text to Spotify intents: play playlist, pause, resume, volume up/down (±10%), now-playing.
   - Non-Spotify commands (e.g., “what time is it?”) can fall back to local handlers or a text-based assistant later.
5. **Spotify Control**
   - `spotify_client` maintains OAuth token (authorization code flow, refresh token stored in NVS).
   - Issues HTTPS requests to `/v1/me/player/{play,pause,volume,...}` for the active Spotify Connect device (Korvo-1 registered via `player` API) when cspot is disabled.
   - When `CONFIG_KVA_SPOTIFY_USE_CSPOT` is enabled, `spotify_player` embeds the upstream cspot Connect receiver so Korvo appears directly in the Spotify app; pause/resume/volume intents short-circuit into the local `SpircHandler`.
6. **AWS IoT Sync**
   - `aws_iot_service` keeps MQTT session alive, publishes sensor snapshots + device state, and listens for remote commands (e.g., “volume down” from app) that flow into `command_router`.
   - `aws_iot_bridge` tallies diagnostics (wake detections, button presses, STT/TTS/Spotify success rates) and emits a JSON snapshot every telemetry interval for future ingestion by AWS IoT topics.
   - `somnus_mqtt_start` runs automatically in the sample, so drop the cert/key/CA bundle into `/spiffs/Cert` (see `docs/aws_iot_interface.md`) or run `python scripts/provision_and_stage_somnus_cert.py --thing-name SOMNUS_<DEVICE>` to let the build embed the credentials and publish metrics to `device/telemetry/{DEVICE_ID}`.
7. **TTS Response**
   - `openai_client` calls `POST /v1/audio/speech` (`gpt-4o-mini-tts`, voice set via menuconfig) with response text (“Playing your Spotify Daily Mix”).
   - The WAV payload is parsed by `audio_player` and pushed over I2S to the ES8388 codec so Korvo speaks the reply out loud.
8. **LED / UX Feedback**
   - `led_controller` scenes: idle (deep blue), listening (cyan), thinking (amber), speaking (magenta), error (red).
9. **Buttons**
   - `button_service` debounces Korvo’s buttons and pushes events into the voice pipeline. Button 1 currently prompts GPT to craft a short acknowledgement (“Button one, got it!”) before running through TTS.

## Component Responsibilities

| Component | Status | Notes |
| --- | --- | --- |
| `components/audio_manager` | existing | Provide capture/playback APIs; ensure double-buffering for wake word task + TTS playback. |
| `components/wake_word` | new | Thin wrapper around ESP-SR, configurable keyword assets. Emits FreeRTOS event on detection. |
| `components/openai_client` | reuse + extend | Lift from `samples/korvo_openai_voice`, abstract STT + TTS requests, handle secrets via generated header. |
| `components/spotify_client` | new | HTTPS client using ESP-IDF’s `esp_http_client`, handles OAuth, device discovery, and commands (play, pause, volume delta). |
| `components/aws_iot_service` + `somnus_mqtt` | existing | Maintain AWS IoT Core connection, publish state/sensor JSON, dispatch inbound control topics into `intent_router`. |
| `components/voice_pipeline` | new | Orchestrates wake word → capture → STT → intent routing → TTS + button-triggered replies. Owns state machine + timeouts. |
| `components/intent_router` | new | Maps utterances (or AWS IoT commands) to actions. Start with rule-based parsing, future hook for LLM tool-calling. |
| `components/led_controller` | new | Drives WS2812 state colors for idle/listening/thinking/speaking/error. |
| `components/button_service` | new | Configurable GPIO debounce + ISR fan-out for Korvo buttons feeding the pipeline. |
| `components/audio_player` | new | Configures the ES8388 codec + I2S TX path and exposes helpers to play WAV/PCM buffers (TTS + Spotify). |
| `components/spotify_player` | new | Optional cspot-based Spotify Connect receiver that feeds PCM frames into the shared audio path. |
| `main/korvo_voice_assistant_main.c` | new | Bootstraps Wi-Fi, audio stack, wake word, pipeline tasks, AWS IoT session, and registers CLI hooks for debugging. |

## Configuration Surface (menuconfig)

- `Naphome Voice Assistant`
  - Wi-Fi credentials (reuse existing fields)
  - OpenAI API key path (`~/.env` default)
  - Wake word sensitivity (0–100)
  - Max capture window (ms)
  - Spotify client ID/secret (read from secure JSON in `somnus-iot-cert` or menuconfig for dev)
  - Default Spotify device name (fallback if Connect device list empty)
  - TTS voice preset
  - AWS IoT endpoint + Somnus certificate slot (reuse existing config items)
  - Sensor publication interval + topic root
  - ES8388 I2S/I2C pin mapping + playback sample-rate defaults
  - WS2812 GPIO / LED count / brightness
  - Button GPIO mapping + debounce interval (set GPIO to -1 to disable)
  - Optional cspot enable toggle + device name (requires the upstream cspot repo next to Naphome-Firmware)

## Tasks & Scheduling

| Task | Priority | Notes |
| --- | --- | --- |
| `wake_word_task` | 4 | High priority, pinned to core 1, 5120 bytes stack. |
| `voice_pipeline_task` | 3 | Handles network I/O, can block while waiting for OpenAI responses. |
| `spotify_refresh_task` | 2 | Refresh access tokens every 30 minutes. |
| `aws_iot_task` | 2 | (Existing) runs Somnus MQTT loop, pushes telemetry + handles control actions. |
| `led_task` | 1 | Periodic animation updates. |

## Networking & Security

- STT/TTS calls use TLS with `esp_crt_bundle_attach`.
- Spotify API requires TLS + OAuth; store refresh token encrypted in NVS (flash encryption recommended for production).
- API keys never committed: generate `openai_secrets.h` during CMake configure (same pattern as `korvo_openai_voice`).
- AWS IoT credentials pulled from provisioned cert/key bundle; all control commands validated against device shadow/state topics.

## Development Plan

1. A scaffolded project lives at `samples/korvo_voice_assistant` (forked from the OpenAI demo) and wires wake word → OpenAI STT/TTS → Spotify/AWS stubs.
2. Add wake word component (ESP-SR) and unit tests for the detector callback logic.
3. Generalize `openai_client` into a reusable component shared by sample + future main app.
4. Hook in existing `aws_iot`/`somnus_mqtt` components, publish baseline sensor payloads from `sensor_manager`, and plumb inbound MQTT actions into the command router.
5. Implement minimal Spotify client (auth + play/pause + volume delta) with mocked unit tests that replay recorded HTTP responses.
6. Build `voice_pipeline` state machine and CLI harness to simulate intents.
7. Integrate LED feedback + audio playback path.
8. Wire button handling into the pipeline (generate GPT acknowledgement + TTS playback).
9. Add automated tests for intent routing (text fixtures + MQTT command fixtures).

## Open Questions

- Do we ship with always-on Wi-Fi + Spotify OAuth tokens in flash, or require mobile-app provisioning per boot?
- Should the assistant support fallback responses via text LLM (e.g., GPT-4o) when utterances are not Spotify commands?
- Is offline wake-word inference sufficient with ESP-SR’s footprint alongside TLS stacks, or do we need PSRAM-only builds?
