# Collecting Training Data for "Hey, Naptick"

## Positive Samples (Required)

Record "Hey, Naptick" in various conditions:

### Recommended Conditions
- **Distance**: Close (1-2 feet), Medium (3-5 feet), Far (6-10 feet)
- **Environment**: Quiet room, Noisy room, Outdoor
- **Speaker**: Different speakers if possible
- **Tone**: Normal, Excited, Whispered, Clear
- **Speed**: Normal, Fast, Slow

### File Format
- Format: WAV
- Sample Rate: 16kHz
- Channels: Mono
- Bit Depth: 16-bit

### Naming
Save as: `hey_naptick_001.wav`, `hey_naptick_002.wav`, etc.

### Quantity
- Minimum: 50 samples
- Recommended: 100-200 samples
- Optimal: 200+ samples

## Negative Samples (Recommended)

Record samples that should NOT trigger:

### Types
- Other wake words ("Hey Google", "Alexa", etc.)
- Similar phrases ("Hey, Naptick" variations that shouldn't trigger)
- Background noise
- Other speech
- Music

### Quantity
- Minimum: 100 samples
- Recommended: 200-500 samples

## Collection Methods

### Method 1: Direct Recording
```bash
# Using arecord (Linux) or similar
arecord -r 16000 -f S16_LE -c 1 hey_naptick_001.wav
# Say "Hey, Naptick"
# Press Ctrl+C to stop
```

### Method 2: TTS Generation
Use text-to-speech to generate samples:
- Google TTS
- Amazon Polly
- Azure TTS
- Local TTS (espeak, festival, etc.)

### Method 3: Online Tools
- Use online audio recording tools
- Export as WAV (16kHz, mono, 16-bit)

## Validation

After collecting, verify:
- [ ] All files are WAV format
- [ ] Sample rate is 16kHz
- [ ] Mono channel
- [ ] 16-bit depth
- [ ] Files are 1-3 seconds long
- [ ] Clear audio (not too quiet/loud)
