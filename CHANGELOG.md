# Changelog

All notable changes to this project are documented here.

## [0.2.0] - 2026-09-25

- Hardware-independent core with 77 host checks: resynchronising framer, contract-exact JSON, dew point, PTC controller, static-reflector map, node-id validation.
- Firmware refuses invalid identity and duplicate pins, uses SNTP time and an MQTT last will.
- The LD2450/LD2461 frame layout is intentionally not decoded until the vendor document is available.
