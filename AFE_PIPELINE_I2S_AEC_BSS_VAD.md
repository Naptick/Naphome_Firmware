# AFE Pipeline: I2S -> AEC -> BSS/NS -> VAD -> OpenAI Realtime

## Pipeline Overview

```
I2S (Raw Audio)
    ↓
AEC (Acoustic Echo Cancellation)
    ↓ Removes speaker echo
BSS/NS (Blind Source Separation / Noise Suppression)
    ↓ Separates speech, removes noise
VAD (Voice Activity Detection)
    ↓ Gates audio - only speech passes
OpenAI Realtime API
    ↓ Real-time transcription
```

## Implementation

### AFE Configuration
- **AEC**: `aec_init = true` - First stage, removes echo
- **SE**: `se_init = true` - Beamforming/BSS for source separation
- **NS**: `ns_init = true` - Noise suppression after AEC
- **VAD**: `vad_init = true`, `vad_mode = VAD_MODE_3` - Gates OpenAI
- **AGC**: `agc_init = true` - Gain control throughout
- **WakeNet**: `wakenet_init = false` - Skipped, using VAD instead

### Processing Flow
1. **I2S Capture** - Raw audio from ES7210 microphones
2. **AFE Feed** - Audio fed to AFE for processing
3. **AEC Processing** - Echo cancellation (may need playback reference)
4. **BSS/NS Processing** - Source separation and noise removal
5. **VAD Check** - Only proceed if speech detected
6. **OpenAI Stream** - Send processed audio only when VAD active

### VAD Gating
- Energy-based VAD on AFE-processed audio
- Threshold: 800.0 (higher for processed audio with better SNR)
- Only sends to OpenAI when VAD detects speech
- Reduces unnecessary API calls

## Key Files
- `samples/korvo_voice_assistant/main/voice_pipeline.c` - Pipeline implementation
- `samples/korvo_voice_assistant/main/openai_client.c` - OpenAI Realtime WebSocket
