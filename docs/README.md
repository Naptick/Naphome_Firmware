# Naphome Firmware Documentation

This directory contains the documentation for Naphome Phase 0.9 Firmware.

The documentation is automatically published to GitHub Pages when changes are pushed to the `main` branch.

## Viewing the Documentation

- **GitHub Pages**: https://naphome.github.io/Naphome_Firmware/
- **Local Development**: Run `bundle exec jekyll serve` (requires Jekyll)

## Documentation Structure

- `index.md` - Main landing page
- `aws_iot_interface.md` - AWS IoT Core and Somnus MQTT integration
- `matter_interface.md` - Matter bridge for sensor telemetry
- `specifications.md` - Firmware requirements and specifications
- `naphome_voice_assistant.md` - Korvo-1 voice assistant implementation
- `i2s_farfield_analysis.md` - Microphone array analysis

## Contributing

When updating documentation:
1. Edit the relevant `.md` files in this directory
2. Commit and push to `main` branch
3. GitHub Actions will automatically deploy to GitHub Pages
