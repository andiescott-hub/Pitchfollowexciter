# Pitch-follow Exciter

A multi-band harmonic exciter where each band follows the strongest pitch inside it,
saturates a phase-locked copy of that note and blends it back.

- `docs/concept.md`: concept and planning overview (findings, prior art, risks, roadmap).
- `prototype/index.html`: multi-band web prototype. Open it in a browser; it loads a demo clip.

## Prototype: how the interface works

The interface borrows the interaction model of display-first EQs and multiband processors:
the frequency display is the main control surface and a small floating panel holds the rest.

| Gesture | Result |
| --- | --- |
| Double-click the display | Add a band (up to four) |
| Drag a band | Move its centre frequency; the window width follows automatically |
| Drag a band's dot up or down | Amount |
| Scroll over a band | Focus (window width, hysteresis and glide together) |
| Select a band | Floating panel: Amount, Character, Focus, solo, bypass, remove |
| Arrow keys, Delete | Nudge or remove the selected band |

Global controls: Mix / Added / Dry listening, Key (off, auto-detect or chosen), Bass link.

What the display shows: the input spectrum (filled), the output spectrum (line), each band's
window (coloured region), the note it is following right now (the tall peak, labelled with the
note name) and the harmonics the saturator adds above it (the smaller peaks).

## What is implemented from the concept

- Shared multi-resolution analysis (8192 / 4096 / 2048 / 1024 samples by register).
- Automatic window, glide, hysteresis, gate and filter Q from centre frequency.
- Tracked bandpass source, per-band saturator, Character as one sweep (clean, warm, gritty).
- Between-band rule 1 (no shared partials) and rule 4 (harmonic link to the bass band).
- Key awareness as a preference for in-key peaks, with auto-detection.

Not yet built: harmonic budget (rule 2), adaptive window refinement, real-time input.
Analysis is offline; Amount, Character, solo and bypass respond live, and moving a band
re-renders only that band.
