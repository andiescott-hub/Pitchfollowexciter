# Pitch-follow Exciter

A multi-band harmonic exciter where each band follows the strongest pitch inside it,
saturates a phase-locked copy of that note and blends it back.

- `docs/concept.md`: concept and planning overview (findings, prior art, risks, roadmap).
- `prototype/index.html`: multi-band web prototype. Open it in a browser; it loads a demo clip.
- `plugin/`: real-time plugin (JUCE, C++). See below.

## Prototype: how the interface works

The interface borrows the interaction model of display-first EQs and multiband processors:
the frequency display is the main control surface and a small floating panel holds the rest.

| Gesture | Result |
| --- | --- |
| Double-click the display | Add a band (up to four) |
| Drag a band | Move its centre frequency; the window width follows automatically |
| Drag a band's dot up or down | Amount |
| Scroll over a band | Focus (window width, hysteresis and glide together) |
| Select a band | Floating panel: Amount, Drive, Focus, Glide (far left = fixed band, far right = 3 s), saturation type (Tube, Tape, Transistor), solo, bypass, remove |
| Drag a band next to another | Edges snap together (hold Alt to place freely) |
| Arrow keys, Delete | Nudge or remove the selected band |

Global controls: Mix / Added / Dry listening, Key (off, auto-detect or chosen), Bass link.

What the display shows: the input spectrum (filled), the output spectrum (line), each band's
window (coloured region), the note it is following right now (the tall peak, labelled with the
note name) and the harmonics the saturator adds above it (the smaller peaks).

## What is implemented from the concept

- Shared multi-resolution analysis (8192 / 4096 / 2048 / 1024 samples by register).
- Automatic window, glide, hysteresis, gate and filter Q from centre frequency.
- Tracked bandpass source, per-band saturator with three types (Tube, Tape, Transistor) and a Drive amount.
- Between-band rule 1 (no shared partials) and rule 4 (harmonic link to the bass band).
- Key awareness as a preference for in-key peaks, with auto-detection.

- No band adds energy below the notes it follows: saturation curves are symmetric, even harmonics
  come from a separate octave signal, and everything generated passes a highpass under the band.

Not yet built: harmonic budget (rule 2), adaptive window refinement, real-time input.
Analysis is offline; Amount, Drive, type, solo and bypass respond live, and moving a band
re-renders only that band.

## Plugin

`plugin/core` is the DSP engine in plain C++ with no JUCE dependency, so it can be tested on its own.
`plugin/src` is the JUCE wrapper (VST3, AU on macOS, Standalone) and the editor.

```bash
cmake -S plugin -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
ctest --test-dir build
```

JUCE 8.0.4 is downloaded on first configure; pass `-DPFX_JUCE_PATH=/path/to/JUCE` to use a local copy,
or `-DPFX_BUILD_PLUGIN=OFF` to build only the core tests. Built plugins land in
`build/PitchFollowExciter_artefacts/`.

Modes:

- **Lookahead** (default): audio is delayed 4096 samples at 44.1/48 kHz (about 93 ms) and the host
  compensates. Bands land on a new note within a few milliseconds.
- **Low latency**: no delay. A band follows a note change after roughly half an analysis window
  (about 50 ms in the low mids, more in the bass, less up high).

Oversampling (Off, 4x, 8x, 16x) applies to the saturator only. The clean phase-locked copy stays at
the base rate, so oversampling adds no latency to it.

The editor matches the prototype: input and output spectrum, band windows, the note each band is
following with its harmonic peaks, edge snapping, and Amount, Drive, Focus and Glide per band.

Not yet in the plugin: key auto-detect (choose the key by hand), harmonic budget, stereo wet signal
(the added signal is mono, as in the prototype). The company name and plugin codes in
`plugin/CMakeLists.txt` are placeholders.

### Getting a build without compiling

Every push that touches `plugin/` builds macOS (VST3, AU, Standalone; Apple silicon and Intel) and
Windows (VST3, Standalone) on GitHub Actions. Open the repository's Actions tab, pick the latest
"Build plugin" run and download the zip for your system from Artifacts.

Install on macOS:

- `Pitch-follow Exciter.vst3` goes in `~/Library/Audio/Plug-Ins/VST3`
- `Pitch-follow Exciter.component` goes in `~/Library/Audio/Plug-Ins/Components`

The builds are not notarised by Apple, so macOS blocks them after download until you clear the
quarantine flag:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Pitch-follow Exciter.vst3" ~/Library/Audio/Plug-Ins/Components/"Pitch-follow Exciter.component"
```

Install on Windows: copy `Pitch-follow Exciter.vst3` to `C:\Program Files\Common Files\VST3`.

Then rescan plugins in your DAW.
