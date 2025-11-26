# SomnusDevice LED Patterns

This document describes all LED patterns supported by the SomnusDevice reference implementation.

## LED Configuration
- **Total LEDs**: 50
- **Inner Ring**: 16 LEDs
- **Outer Ring**: 24 LEDs
- **Brightness Control**: Global brightness multiplier (0.0 - 1.0)
- **Intensity Control**: Per-pattern intensity setting
- **Transition Speed**: 60 FPS with smooth cosine easing

## Standard Patterns

### 1. Solid Color (`pattern: "none"`)
- **Description**: Sets all LEDs in the specified range to a solid color
- **Transition**: 2.0 second smooth transition
- **Parameters**:
  - `Color`: [R, G, B] array (0-255 each)
  - `LEDRange`: [start, end] LED indices
  - `Intensity`: Brightness multiplier (0.0-1.0)

### 2. Breathing Effect (`pattern: "breathing"`)
- **Description**: Smooth breathing animation with inhale-hold-exhale cycle
- **Timing**: 40% inhale, 20% hold, 40% exhale
- **Easing**: Cosine-based smooth transitions
- **Frame Rate**: 60 FPS
- **Parameters**:
  - `Color`: [R, G, B] array
  - `TotalDuration`: Cycle duration in seconds (default: 5s)
  - `LEDRange`: [start, end] LED indices

### 3. Color Transition (`pattern: "colortransition"`)
- **Description**: Smoothly transitions LEDs from current color to target color
- **Parameters**:
  - `Color`: Target [R, G, B] array
  - `TotalDuration`: Transition duration in seconds
  - `LEDRange`: [start, end] LED indices

### 4. Clear Dimming (`pattern: "cleardimming"`)
- **Description**: Gradually dims all LEDs to off
- **Transition**: 2.0 second smooth fade to black

## Special Color-Triggered Effects

These effects are automatically triggered when specific RGB color values are sent (regardless of pattern setting):

### 5. Red-Blue Gradient Circles (Normal Speed)
- **Trigger Color**: `[255, 150, 150]`
- **Effect**: Continuous circular gradient animation from red to blue
- **Speed**: Normal (0.01 phase increment, 0.02s frame delay)
- **Fade In**: Yes (smooth transition from current state)

### 6. Red-Yellow Gradient Circles (Slow)
- **Trigger Color**: `[220, 38, 38]`
- **Effect**: Continuous circular gradient animation from red to yellow
- **Speed**: Slow (0.005 phase increment, 0.03s frame delay)
- **Fade In**: Yes

### 7. White-Blue Gradient Circles (Slow)
- **Trigger Color**: `[14, 165, 233]`
- **Effect**: Continuous circular gradient animation from white to blue
- **Speed**: Slow
- **Fade In**: Yes

### 8. Blue-Green Gradient Circles (Slow)
- **Trigger Color**: `[6, 182, 212]`
- **Effect**: Continuous circular gradient animation from blue to green
- **Speed**: Slow
- **Fade In**: Yes

### 9. Dark Teal-Orange Gradient Circles (Slow)
- **Trigger Color**: `[244, 114, 182]`
- **Effect**: Continuous circular gradient animation from dark teal to orange
- **Speed**: Slow
- **Fade In**: Yes

### 10. Orange Breathing Pulse
- **Trigger Color**: `[255, 135, 0]`
- **Effect**: Continuous breathing animation with 4-4-4 pattern
- **Breathing Pattern**: 
  - 4 seconds inhale
  - 4 seconds hold at peak
  - 4 seconds exhale
  - Total cycle: 12 seconds
- **Frame Rate**: 60 FPS
- **Minimum Intensity**: 5% (prevents complete darkness)

### 11. Lilac Breathing Pulse
- **Trigger Color**: `[255, 100, 255]`
- **Effect**: Continuous breathing animation with 4-4-4 pattern
- **Breathing Pattern**: Same as orange (4-4-4, 12s cycle)
- **Frame Rate**: 60 FPS
- **Minimum Intensity**: 5%

## Startup Effect

### 12. Startup Gradient
- **Description**: Dark teal-orange gradient circles shown on device startup
- **Duration**: 10 seconds
- **Speed**: Normal
- **Auto-clear**: Yes (smoothly fades to off after completion)

## Technical Details

### Smooth Transitions
- All color changes use smooth cosine-based easing
- Default transition duration: 1.0 second (configurable)
- Transition FPS: 60 frames per second
- Formula: `eased_progress = 0.5 * (1 - cos(progress * π))`

### Brightness Control
- **Global Brightness**: Applied to all LED operations (0.0 - 1.0)
- **Intensity**: Per-pattern brightness multiplier
- **Combined Effect**: `final_color = base_color * global_brightness * intensity`

### Thread Safety
- All effects run in separate daemon threads
- Effects can be stopped cleanly without LED manipulation
- State is preserved when effects are stopped

### Effect Management
- Only one special effect can run at a time
- Starting a new effect automatically stops previous effects
- Smooth transitions between effects are supported

## Example JSON Commands

### Solid Color
```json
{
  "Pattern": "none",
  "Color": [255, 100, 100],
  "LEDRange": [0, 49],
  "Intensity": 0.8
}
```

### Breathing Effect
```json
{
  "Pattern": "breathing",
  "Color": [0, 255, 255],
  "TotalDuration": 5,
  "LEDRange": [0, 49]
}
```

### Trigger Gradient Effect
```json
{
  "Pattern": "none",
  "Color": [255, 150, 150],
  "LEDRange": [0, 49]
}
```

### Trigger Breathing Pulse
```json
{
  "Pattern": "none",
  "Color": [255, 100, 255],
  "LEDRange": [0, 49]
}
```

## Notes

- All effects support smooth transitions from the current LED state
- Special color-triggered effects override the pattern setting
- Effects can be stopped by sending a different color or pattern
- The LED state is tracked and preserved when effects are stopped
- Brightness changes are applied smoothly to active effects (when not running special effects)
