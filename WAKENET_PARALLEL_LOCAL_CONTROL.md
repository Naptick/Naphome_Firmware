# WakeNet9l in Parallel with OpenAI Realtime

## Dual Pipeline Architecture

```
I2S (Raw Audio)
    ↓
AEC (Acoustic Echo Cancellation)
    ↓
BSS/NS (Blind Source Separation / Noise Suppression)
    ↓
    ├─→ VAD → OpenAI Realtime (continuous transcription)
    └─→ WakeNet9l → Local Control (triggered on "hi esp")
```

## Implementation

### AFE Configuration
- **AEC**: `aec_init = true` - Echo cancellation
- **SE**: `se_init = true` - Beamforming/BSS
- **NS**: `ns_init = true` - Noise suppression
- **VAD**: `vad_init = true` - Gates OpenAI streaming
- **WakeNet**: `wakenet_init = true` - Parallel local control
- **Model**: `wn9_hiesp` (WakeNet9l "hi esp")

### Dual Processing Paths

**Path 1: OpenAI Realtime (Continuous)**
- I2S → AEC → BSS/NS → VAD → OpenAI Realtime
- Streams processed audio when VAD detects speech
- Real-time transcription for general queries

**Path 2: WakeNet Local Control (Triggered)**
- I2S → AEC → BSS/NS → WakeNet → Local Control
- Triggers on "hi esp" wake word
- Executes local control actions (lights, volume, etc.)
- Runs in parallel with OpenAI streaming

### Key Features
- WakeNet9l detects "hi esp" for local control
- OpenAI Realtime continues streaming for transcription
- Both paths share same AFE processing (AEC/BSS/NS)
- VAD gates OpenAI to reduce API calls
- WakeNet triggers local actions without interrupting OpenAI

## Configuration
- `enable_wakenet_local = true`
- `wakenet_model = "wn9_hiesp"`
- `wakenet_threshold = 60`
