# Audio decoder test fixtures

These short stereo tones are generated from FFmpeg's `sine` source filter and contain no third-party music:

- `tone.opus`: Ogg Opus, 48 kHz, approximately 0.26 seconds.
- `tone.m4a`: AAC-LC in an M4A/ISO-BMFF container, 44.1 kHz, 0.25 seconds.
- `tone.aac`: AAC-LC in ADTS form, 44.1 kHz, approximately 0.28 seconds.

They are used by automated decoder tests only.
