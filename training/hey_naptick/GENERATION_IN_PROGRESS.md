# Training Data Generation - In Progress ✅

## Status: Generation Running

TTS generation for "Hey, Naptick" training data has been started!

## What's Happening

The script `generate_with_gtts_ffmpeg.py` is:
- Generating 200 "Hey, Naptick" samples using Google TTS
- Generating 300 negative samples (other phrases)
- Converting MP3 to WAV using ffmpeg
- Saving to `data/positive/` and `data/negative/`

## Check Progress

```bash
cd training/hey_naptick

# Quick check
./check_generation_progress.sh

# Count real TTS files (not placeholders)
find data/positive -name "*.wav" -size +40k -size -100k | wc -l
find data/negative -name "*.wav" -size +40k -size -100k | wc -l

# Watch recent files
ls -lt data/positive/*.wav | head -5
```

## File Sizes

- **Placeholder files**: ~63KB (silent, same size)
- **Real TTS files**: 30-50KB (vary, have audio content)

## Expected Timeline

- **Total time**: 30-60 minutes
- **Progress**: Check every 5-10 minutes
- **Completion**: When you have 200 positive + 300 negative real TTS files

## When Complete

Once generation finishes:

```bash
cd training/hey_naptick
python3 train_hey_naptick.py
```

This will train your "Hey, Naptick" model!

## Troubleshooting

**Generation seems stuck?**
- Check internet connection (gTTS needs internet)
- Check for rate limiting (Google may throttle)
- Check log: `tail -f generation.log`

**Want to stop and resume?**
- Script skips existing files
- Just run again: `python3 generate_with_gtts_ffmpeg.py`

**Generation fails?**
- Use placeholder files for structure
- Record real samples manually
- See `COLLECT_DATA.md` for instructions

## Next Steps

1. **Wait for generation** (30-60 min)
2. **Verify files** (check sizes, play samples)
3. **Train model** (`python3 train_hey_naptick.py`)
4. **Flash to ESP32** (see main docs)

Generation is running! 🚀