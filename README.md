# Find The Pocket

Find The Pocket is a browser-based metronome training app that helps you improve timing, consistency, and groove using your microphone input.

It combines:
- real-time click tracking,
- calibrated beat-offset scoring,
- waveform and offset visualizations,
- pocket modes (`Strict`, `Laid-back`, `Push`),
- timed practice sessions with live countdown.

## Features

- **Metronome controls**
  - BPM range: `40-240`
  - Session duration: `Indefinite`, `1`, `3`, `5`, `10` minutes
- **Input handling**
  - Audio input device selection
  - Live input level meter
  - Raw waveform scope with click markers
- **Timing feedback**
  - Live offset graph in milliseconds (`+/- ms`)
  - Visual beat flash
  - Per-hit scoring with accuracy/consistency/stability components
- **Calibration mode**
  - Dedicated non-scoring calibration run
  - Learns timing offset and jitter tolerance
  - Applies calibration to scoring and waveform click alignment
- **Session results**
  - Total score
  - Hit count
  - Component totals (accuracy, consistency, stability, pocket)

## Run Locally

Use a local server (recommended for Safari microphone permissions):

```bash
cd /path/to/findthepocket
python3 -m http.server 8000
```

Open:

- `http://localhost:8000`

## Safari Notes

- On Safari, microphone access generally requires `localhost`/secure context.
- `file://` access is intentionally blocked for Safari capture reliability.
- If no prompt appears, check:
  - Safari website microphone permissions for `localhost`
  - macOS Privacy & Security > Microphone > Safari

## How To Practice

1. Click **Detect Inputs** and choose your microphone.
2. Run **Calibrate** once for your setup.
3. Pick:
   - BPM
   - Pocket mode
   - Session duration
4. Click **Start** and play/tap in time.
5. Review score and component totals at the end.

## Tech

- Plain HTML/CSS/JavaScript
- Web Audio API + `getUserMedia`
- Works locally on modern desktop browsers (optimized for macOS Safari use case)
