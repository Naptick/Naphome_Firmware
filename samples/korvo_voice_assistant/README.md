# Korvo Voice Assistant Sample

This ESP-IDF project targets the Korvo-1 (ESP32-S3) dev kit and wires together:

- Wake-word detection for “Naphome” (simulated timer today, ESP-SR drop-in later)
- Audio capture on the on-board PDM mic
- OpenAI Speech-to-Text + Text-to-Speech
- Intent routing for Spotify playback/volume commands
- AWS IoT telemetry hooks for remote control + sensor streaming stubs
- Korvo-1’s WS2812 status ring for idle/listening/thinking/speaking cues
- Button 1 press handling that asks the LLM for a quick acknowledgement and speaks it back
- ES8388 playback pipeline so OpenAI TTS responses and future Spotify streams render through the onboard speaker
- Optional cspot-based Spotify Connect receiver so the device shows up in the Spotify app once provisioned

The sample is intentionally modular so its pieces can graduate into the production `main/` app.

## Prerequisites

- ESP-IDF v5.0 or newer (`. $IDF_PATH/export.sh`)
- Korvo-1 dev kit over USB
- Wi-Fi network credentials
- OpenAI API key with STT/TTS access stored in `~/.env`
- (Optional) Spotify and AWS IoT credentials; the sample currently logs the commands that would be sent.

## Quick Start

```bash
cd samples/korvo_voice_assistant
idf.py set-target esp32s3
idf.py menuconfig   # Fill out "Korvo Voice Assistant" options
#   - Set Wi-Fi credentials
#   - Set OpenAI key path (defaults to ~/.env)
#   - Configure codec I2S/I2C pins for the Korvo-1 ES8388 audio path
#   - (Optional) override the WS2812 GPIO (defaults to Korvo-1’s GPIO1) and configure button GPIOs if needed
#   - (Optional) enable "Spotify playback via cspot" (defaults to `../cspot`) so the Korvo advertises itself as a Spotify Connect target. When enabled, pause/resume/volume intents control the embedded cspot player.
idf.py build flash monitor
```

When `CONFIG_KVA_SIMULATED_WAKE_INTERVAL_MS` is non-zero the assistant “hears” the wake word on that interval so you can validate the end-to-end pipeline without a trained ESP-SR model.

### Provisioning AWS IoT Credentials

1. Determine the Somnus-formatted device ID (e.g., `SOMNUS_112233445566`). You can read it from the boot log once or derive it from the Korvo’s Wi-Fi MAC.
2. Run the helper script:
   ```bash
   python scripts/provision_and_stage_somnus_cert.py --thing-name SOMNUS_<DEVICE_ID>
   ```
   This wraps `provision_aws_thing.py`, creates/attaches the IoT Thing + policy, and copies the resulting PEM bundle into `samples/korvo_voice_assistant/spiffs/Cert/`.
3. Build/flash as usual. CMake now packs `samples/korvo_voice_assistant/spiffs` into the `storage` partition automatically, so the device boots with `/spiffs/Cert` populated.

Certificates never touch version control (the PEMs are ignored) and you can re-run the script with `--skip-provision` if you just need to restage existing files.

## Wake Word Detection

- `wake_word_service` now keeps the Korvo-1 mic stream open in a lightweight FreeRTOS task.
- The task grabs 32 ms (512-sample) frames, learns a noise floor, and compares the average absolute amplitude against a threshold derived from `CONFIG_KVA_WAKE_WORD_SENSITIVITY`. Higher values make it harder to trigger (less sensitive).
- Four consecutive frames above threshold raise a wake event and pause the detector for roughly `CONFIG_KVA_CAPTURE_MS + 500 ms` so the voice pipeline can record the utterance without contention.
- If you still need hands-free testing, leave `CONFIG_KVA_SIMULATED_WAKE_INTERVAL_MS` > 0 and both the simulated timer and live audio detector will feed the pipeline.

## Diagnostics & Telemetry

- The `aws_iot_bridge` keeps running counters for wake detections (real vs. simulated), button presses, STT/TTS success rates, and Spotify command outcomes.
- Every `CONFIG_KVA_AWS_TELEMETRY_PERIOD_MS` it emits a JSON snapshot over Somnus MQTT (topic `device/telemetry/{DEVICE_ID}`) and falls back to logging if the MQTT link is down.
- Wake-word, button, STT, TTS, and Spotify paths automatically update the metrics; add more counters there as new features land.
- Provision AWS IoT credentials following `docs/aws_iot_interface.md` (drop the cert/key/CA bundle into `/spiffs/Cert` or use the embedded PEMs) so the Somnus MQTT service can authenticate before metrics are published.

## What Happens on Wake Word

1. Wake-word task (ESP-SR or simulated) notifies the voice pipeline.
2. Pipeline captures `CONFIG_KVA_CAPTURE_MS` of PCM at `CONFIG_KVA_SAMPLE_RATE`.
3. Samples are sent to OpenAI `/v1/responses` for transcription.
4. Parsed utterance flows through the rule-based intent router.
5. Spotify client stub logs the command (play/pause/resume/volume delta) that would be sent to the Web API.
6. A spoken confirmation is synthesized via OpenAI `/v1/audio/speech` and (for now) logged; hook this into playback to hear the response.
7. The WAV response is fed into the ES8388 codec over I2S so you hear it through the Korvo speaker.
8. Korvo’s WS2812 ring lights up per state (idle blue, listening cyan, thinking amber, speaking magenta, error red).
9. AWS IoT bridge stub reports state/intent and would publish sensors once wired to `sensor_manager`.
10. Button presses short-circuit the pipeline: the button service asks OpenAI to produce a cheerful sentence (e.g., “You pressed button one!”) and immediately synthesizes it.
11. When `CONFIG_KVA_SPOTIFY_USE_CSPOT` is enabled and cspot is available at `../cspot`, the device also registers as a Spotify Connect endpoint. Pair from the Spotify mobile/desktop app and playback streams through the same audio path.

### Spotify Connect via cspot

- Clone the upstream cspot repository alongside `Naphome-Firmware` (e.g., `~/GitHub/cspot`).
- Enable **Spotify playback via cspot** in menuconfig, adjust the device name if desired, and point `CSPOT_SOURCE_DIR` to the clone if you placed it elsewhere (default: `../cspot`).
- Build/flash; on first boot the device exposes a zeroconf setup page. From the Spotify app choose “Log in with device code” → “Spotify Connect” to provision credentials.
- Credentials are cached in `/spiffs/spotify_blob.json`; wipe SPIFFS or delete the file to re-pair.
- Audio flows straight into the ES8388 path, so volume control works via the assistant or the Spotify app.
- When cspot is enabled the assistant’s Spotify intents pause/resume playback locally and adjust volume through the cspot `SpircHandler` rather than the stubbed Web API calls.

## Next Steps

- Replace the simulated wake trigger with ESP-SR keyword assets.
- Feed TTS WAV output into the Korvo-1 playback chain for audible responses.
- Exchange the Spotify stub for an authenticated client that hits `/v1/me/player/*`.
- Connect the AWS IoT bridge to the existing `aws_iot` + `somnus_mqtt` components and expose remote control topics.
- Attach real sensors via `sensor_manager` and publish telemetry upstream.
- Swap the Korvo-1 button stub for a multi-button map and route button actions into Spotify or other device controls.
- Integrate the upstream cspot Spotify receiver so the assistant can request tracks directly after pairing with the Spotify app.
- Bind the voice intent router to cspot’s `SpircHandler` so commands like “pause Spotify” control the Connect session directly.
