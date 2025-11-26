# Face LED Simulator

## Overview

The Face LED Simulator maps LED patterns from SomnusDevice onto the non-transparent oval region of `NaphomeFace.png`, allowing visualization of LED effects on the device's display.

## Architecture

### Components

1. **Face Image**: `NaphomeFace.png` (128x128 RGBA) converted to RGB565 format
2. **LED Position Mapping**: 50 LEDs arranged in two concentric rings:
   - Inner ring: 16 LEDs at 40% radius
   - Outer ring: 24 LEDs at 85% radius
3. **Pattern Renderer**: Renders LED colors on the face image with glow effects
4. **Animation Tasks**: FreeRTOS tasks for animated patterns

### Files

- `face_led_simulator.h` / `.c`: Main simulator implementation
- `face_led_positions.h`: Auto-generated LED position mapping
- `naphome_face_image.h`: Face image data (RGB565)
- `scripts/analyze_face_oval.py`: Analyzes face image and generates LED positions
- `scripts/generate_led_positions_header.py`: Converts JSON mapping to C header

## LED Patterns Supported

All patterns from `somnus_action_handler.c` are supported:

1. **Solid Color** (`FACE_LED_PATTERN_NONE`): Static color on all LEDs
2. **Breathing** (`FACE_LED_PATTERN_BREATHING`): Smooth cosine-based pulsing
3. **Gradient Circles**:
   - Red-Blue (normal speed)
   - Red-Yellow (slow)
   - White-Blue (slow)
   - Blue-Green (slow)
   - Teal-Orange (slow)
4. **Pulse** (4-4-4 breathing):
   - Orange pulse
   - Lilac pulse

## Usage

```c
#include "face_led_simulator.h"
#include "naphome_face_image.h"

// Initialize
face_led_simulator_t simulator;
face_led_simulator_init(&simulator, display, naphome_face_data);

// Set pattern
face_led_simulator_set_pattern(&simulator,
                                FACE_LED_PATTERN_BREATHING,
                                255, 100, 100,  // RGB
                                0.8f);          // Intensity

// Stop pattern
face_led_simulator_stop_pattern(&simulator);

// Cleanup
face_led_simulator_deinit(&simulator);
```

## Integration with somnus_action_handler

To integrate with the existing LED action handler, modify `somnus_action_handler.c` to also call the face LED simulator when LED patterns are set.

## Rendering Details

- **Base Image**: Face image is copied to render buffer
- **LED Rendering**: Each LED is rendered at its mapped position with:
  - Main LED pixel at full intensity
  - Glow effect (3x3 pixel area) at 30% intensity
  - Alpha blending with base image (70% opacity)
- **Frame Rate**: 
  - Breathing: ~20 FPS (50ms delay)
  - Gradients: 20-30ms delay (normal/slow)
  - Pulse: ~60 FPS (16ms delay)

## LED Position Mapping

LEDs are positioned in concentric circles:
- Center: (84, 67) - calculated from oval centroid
- Inner ring radius: ~21 pixels (40% of max radius)
- Outer ring radius: ~45 pixels (85% of max radius)
- Angles: Evenly distributed around the circle

## Future Enhancements

- [ ] Add support for color transitions
- [ ] Add support for clear dimming pattern
- [ ] Configurable LED glow radius
- [ ] Support for per-LED intensity mapping
- [ ] Integration with actual LED strip for synchronized display
