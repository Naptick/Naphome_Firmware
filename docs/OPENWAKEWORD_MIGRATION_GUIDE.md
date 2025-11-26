# Migrating from Energy-Based to OpenWakeWord

Complete guide for migrating from the existing energy-based wake word detector to OpenWakeWord.

## Overview

The existing `wake_word_service` uses a simple energy-based (RMS) detector. This guide shows how to migrate to OpenWakeWord for ML-based wake word detection.

## Before & After Comparison

### Energy-Based Detector (Current)
- **Method**: RMS energy threshold
- **Accuracy**: ~60-70% (many false positives)
- **Customization**: Sensitivity slider (0-100)
- **Pros**: Simple, no model needed, low CPU
- **Cons**: Poor accuracy, no wake word specificity

### OpenWakeWord (New)
- **Method**: ML-based melspectrogram + TFLite
- **Accuracy**: 85-95% (with good training)
- **Customization**: Threshold, model training
- **Pros**: High accuracy, wake word specific, self-trainable
- **Cons**: Requires model, more CPU/memory

## Migration Strategies

### Strategy 1: Complete Replacement (Recommended)

Replace energy-based detector entirely with OpenWakeWord.

**Steps**:
1. Apply integration patch
2. Enable OpenWakeWord in config
3. Train and flash model
4. Remove energy-based code (optional)

**Pros**: Cleaner code, better accuracy
**Cons**: Requires model training

### Strategy 2: Parallel Operation (Testing)

Run both detectors in parallel during testing.

**Steps**:
1. Add OpenWakeWord alongside energy-based
2. Compare detection results
3. Log both for analysis
4. Switch to OpenWakeWord when confident

**Pros**: Safe testing, easy rollback
**Cons**: Higher CPU usage, more complex

### Strategy 3: Fallback Mode (Production)

Use OpenWakeWord as primary, energy-based as fallback.

**Steps**:
1. Enable OpenWakeWord
2. Keep energy-based as backup
3. Fall back if model fails to load
4. Monitor and alert on fallback

**Pros**: High reliability
**Cons**: More code to maintain

## Step-by-Step Migration (Strategy 1)

### Step 1: Review Current Implementation

Understand your current setup:

```c
// Current: wake_word_service uses energy-based detection
wake_word_service_config_t cfg = {
    .audio = &audio,
    .sensitivity = 50,  // 0-100
    .cooldown_ms = 2000,
};
wake_service = wake_word_service_start(&cfg, callback, ctx);
```

### Step 2: Apply Integration Patch

Apply the integration patch:

```bash
# Review the patch
cat samples/korvo_voice_assistant/main/wake_word_service_oww_patch.diff

# Apply manually or use git apply if compatible
```

Key changes:
1. Add `use_openwakeword` flag to config
2. Add `oww_handle` to service struct
3. Initialize OpenWakeWord in `wake_word_service_start()`
4. Process with OpenWakeWord in `wake_word_task()`
5. Cleanup in `wake_word_service_stop()`

### Step 3: Update Configuration

Add OpenWakeWord config:

```c
wake_word_service_config_t cfg = {
    .audio = &audio,
    .use_openwakeword = true,  // Enable OpenWakeWord
    .sensitivity = 50,         // Keep for fallback
    .cooldown_ms = 2000,
};
```

Or via Kconfig:
```ini
CONFIG_OPENWAKEWORD_ENABLE=y
CONFIG_OPENWAKEWORD_USE_TFLITE=y
CONFIG_OPENWAKEWORD_MODEL_PATH="/spiffs/hey_naptick.tflite"
CONFIG_OPENWAKEWORD_THRESHOLD=0.5
```

### Step 4: Train Model

Train "Hey, Naptick" model:

```bash
./scripts/train_openwakeword.sh
# Or follow docs/openwakeword_training_guide.md
```

### Step 5: Flash Model

Flash model to device:

```bash
# Flash to SPIFFS or partition
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
    --chip esp32s3 --port /dev/ttyUSB0 \
    write_flash 0x140000 hey_naptick.tflite
```

### Step 6: Test with Test Mode

Test integration before deploying model:

```ini
CONFIG_OPENWAKEWORD_TEST_MODE=y
```

Build and test:
```bash
idf.py build flash monitor
```

Verify:
- Audio is processed
- Callbacks fire
- No crashes
- Performance acceptable

### Step 7: Deploy with Model

Disable test mode and deploy:

```ini
CONFIG_OPENWAKEWORD_TEST_MODE=n
```

Build, flash model, and test:
```bash
idf.py build flash
# Flash model
idf.py monitor
```

### Step 8: Tune Threshold

Adjust threshold based on testing:

```ini
# Too many false positives
CONFIG_OPENWAKEWORD_THRESHOLD=0.6

# Misses detections
CONFIG_OPENWAKEWORD_THRESHOLD=0.4

# Start here
CONFIG_OPENWAKEWORD_THRESHOLD=0.5
```

### Step 9: Monitor Performance

Check statistics:

```c
uint32_t frames, detections;
bool loaded;
openwakeword_get_statistics(oww_handle, &frames, &detections, &loaded);

ESP_LOGI(TAG, "OpenWakeWord: %lu frames, %lu detections, model: %s",
         frames, detections, loaded ? "loaded" : "not loaded");
```

### Step 10: Remove Energy-Based (Optional)

Once confident, remove energy-based code:

1. Remove `compute_frame_level()` usage
2. Remove calibration logic
3. Remove energy threshold checks
4. Keep as fallback if desired

## Configuration Mapping

Map old energy-based settings to OpenWakeWord:

| Energy-Based | OpenWakeWord | Notes |
|--------------|--------------|-------|
| `sensitivity` (0-100) | `threshold` (0.0-1.0) | Different scale |
| `activation_frames` | Built into model | Model handles timing |
| `cooldown_ms` | `cooldown_ms` | Same concept |
| `frame_samples` | Auto-calculated | Based on frame_size_ms |

### Sensitivity → Threshold Mapping

Approximate mapping:

```c
// Energy-based sensitivity 0-100 → OpenWakeWord threshold
float threshold = 1.0f - (sensitivity / 100.0f);
// sensitivity=0 (most sensitive) → threshold=1.0 (least sensitive)
// sensitivity=50 (medium) → threshold=0.5 (medium)
// sensitivity=100 (least sensitive) → threshold=0.0 (most sensitive)
```

**Better approach**: Start with threshold=0.5 and tune based on testing.

## Testing Checklist

Before deploying to production:

- [ ] Test mode works (audio processing)
- [ ] Model loads successfully
- [ ] Detections occur on "Hey, Naptick"
- [ ] False positive rate acceptable
- [ ] No crashes or memory leaks
- [ ] Performance acceptable (CPU, latency)
- [ ] Callbacks fire correctly
- [ ] Cooldown works (no re-triggering)
- [ ] Statistics API works
- [ ] Error handling tested
- [ ] Fallback works (if using Strategy 3)

## Rollback Plan

If issues occur:

### Quick Rollback
```c
// Disable OpenWakeWord
.use_openwakeword = false,
```

### Full Rollback
1. Revert integration patch
2. Remove OpenWakeWord config
3. Rebuild and flash

## Performance Comparison

Expected changes:

| Metric | Energy-Based | OpenWakeWord | Change |
|--------|-------------|--------------|--------|
| CPU Usage | ~2-3% | ~5-8% | +3-5% |
| RAM Usage | ~2KB | ~25KB | +23KB |
| Flash Usage | 0KB | ~50-200KB | +50-200KB |
| Accuracy | 60-70% | 85-95% | +15-25% |
| False Positives | High | Low | Much better |

## Common Issues & Solutions

### Issue: Model Not Loading
**Solution**: Check SPIFFS mounted, model path correct, partition table

### Issue: Too Many False Positives
**Solution**: Increase threshold, check test mode disabled, retrain model

### Issue: Misses Detections
**Solution**: Decrease threshold, check audio input, verify model trained correctly

### Issue: High CPU Usage
**Solution**: Increase frame size, enable ESP-DSP, check other tasks

### Issue: Memory Issues
**Solution**: Increase tensor arena, check for leaks, optimize buffers

## Gradual Migration Path

For production systems:

### Week 1: Testing
- Deploy to test devices
- Enable test mode
- Validate integration
- Monitor performance

### Week 2: Parallel Operation
- Enable OpenWakeWord on subset
- Run both detectors
- Compare results
- Collect metrics

### Week 3: Full Migration
- Enable OpenWakeWord on all devices
- Keep energy-based as fallback
- Monitor closely
- Tune threshold

### Week 4: Optimization
- Remove energy-based if confident
- Optimize configuration
- Final tuning
- Document results

## Success Metrics

Track these metrics:

- **Detection Rate**: % of "Hey, Naptick" detected
- **False Positive Rate**: Detections without wake word
- **Latency**: Time from wake word to callback
- **CPU Usage**: Average CPU during detection
- **Memory Usage**: Peak RAM usage
- **Uptime**: System stability

## Conclusion

Migration from energy-based to OpenWakeWord provides:
- ✅ 15-25% accuracy improvement
- ✅ Wake word specificity
- ✅ Self-training capability
- ✅ Better false positive handling

Trade-offs:
- ⚠️ Requires model training
- ⚠️ Higher CPU/memory usage
- ⚠️ More complex setup

**Recommendation**: Migrate gradually with testing at each stage.