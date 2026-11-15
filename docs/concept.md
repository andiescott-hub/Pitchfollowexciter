# Pitch-follow exciter: concept and planning overview

Nov 15, 2026 · @Andy

## Summary

The recommended direction is a multi-band harmonic exciter where each band follows the strongest pitch inside it, saturates a phase-locked copy of that note, and blends it back. Windows widen automatically at low frequencies, and the user sees three or four controls.

The idea started from a simple question: can distortion be made to respect a musical key? Ordinary distortion already follows each note, but it falls apart on chords and dense mixes because of intermodulation. Tracking one pitch per region and distorting only that pitch avoids most of the mess.

Decisions so far:

- **Source:** a tracked bandpass filter (a phase-locked copy of the real note) is the default. A generated sine is kept as an option, but it can partly cancel the original.
- **Smoothing:** short glide (20 to 60 ms) plus hysteresis. Glide alone lags and slides through wrong notes.
- **Prototype:** a working offline web prototype exists, single band, with key snap.

The closest existing products are pitch-tracking harmonic boosters and pitch-tracking EQs (see Existing systems). None of them combine region selection, saturation of the tracked note, multiple tracked bands and key awareness in one tool. That combination is the gap.

## How it works

One shared analysis feeds up to four bands. Each band finds its loudest note, smooths it, pulls a phase-locked copy of that note out of the mix, saturates it and blends it back.

&#91;embedded content: signal chain for one band\]

The dry signal passes straight through, so the effect only ever adds: Amount at zero returns the original mix. The prototype runs this chain for one band, offline.

## Findings so far

The core finding is that distortion stays musical when it acts on one tracked note at a time, and turns to mud when it acts on many notes at once.

| Finding | What it means for the design |
| --- | --- |
| Harmonic distortion only adds whole-number multiples of a note (octave, fifth, third, flat seventh) | Generated harmonics are always "in tune" with their own note, so key awareness only matters for which note is chosen |
| Harmonics 4, 5 and 6 form a major triad; a minor triad is not in the low harmonic series | Distortion alone can build a major chord over a note, never a minor one. Chords beyond that need pitch shifting |
| The 5th harmonic is about 14 cents flat of an equal-tempered major third | Generated thirds sit slightly flat against keyboards and fretted instruments |
| Two or more notes through one distortion stage create intermodulation (sum and difference tones) | This is the main reason distorted chords sound muddy. Isolating one note per band avoids it |
| An added sine at a fixed frequency is a drone, not a boost | The generated tone must follow both pitch and level, or it clashes when chords change |
| A sine that is not phase-aligned can partly cancel the real note | A tracked bandpass filter gives a phase-locked copy for free, so it acts as a true boost |
| Glide smooths jitter but lags behind fast notes and slides through wrong pitches | Keep glide short (20 to 60 ms) and let hysteresis stop peak-hopping |
| Low notes need long analysis windows to resolve pitch | Bass tracking is slower and smeared in time. Offline processing hides this; real time does not |
| Inside a window, the loudest peak may be a harmonic of a lower note | That is fine: it still belongs to a real note, so its distortion products stay consonant |

## Window size by frequency

Yes: the window should be wide at low frequencies and narrow at high ones, and it can be automated from the centre frequency alone, with an optional adaptive refinement.

The prototype already measures the window in semitones, so it scales in hertz (±4 semitones is about 25 Hz wide at 55 Hz and about 800 Hz wide at 1.76 kHz). Three separate effects push the semitone width in the same direction:

- **Crowding of partials.** Neighbouring harmonics of one note get closer in pitch as you go up: harmonics 1 and 2 are 12 semitones apart, 4 and 5 are about 3.9, 8 and 9 about 2. Mid and high windows must be narrow or they hold several competing peaks, and the tracker hops between them.
- **How bass lines move.** Bass parts jump by fifths and octaves, and usually only one instrument plays down there. A wide low window keeps the note inside it without picking up rivals.
- **Analysis resolution.** With an 8192-point FFT at 44.1 kHz, one semitone at 55 Hz is about 0.6 of a bin, while at 1.76 kHz it is about 20 bins. Low bands need long analysis windows (about 190 ms at that size), and high bands can use short ones for faster tracking.

Proposed automatic rule, as a starting point to tune by ear:

| Centre frequency | Window | Analysis window | Glide |
| --- | --- | --- | --- |
| Below 120 Hz | ±7 semitones | 8192 samples (\~190 ms) | 60 ms |
| 120 to 500 Hz | ±4 semitones | 4096 samples (\~90 ms) | 40 ms |
| 500 Hz to 2 kHz | ±2.5 semitones | 2048 samples (\~45 ms) | 25 ms |
| Above 2 kHz | ±1.5 semitones | 1024 samples (\~23 ms) | 15 ms |

A smooth formula can replace the steps later. The analysis column is a multi-resolution approach; the cleaner long-term version is a constant-Q or variable-Q transform, which gives long windows to low bins and short windows to high bins by design.

**Adaptive refinement (optional).** Start from the rule, then in each frame narrow the window around the current track while the strongest peak is less than about 6 dB above the next one, and widen it when nothing is found. This handles busy passages without a user control.

It is worth testing whether glide should scale too: low notes need more smoothing and high notes need faster response, which matches the analysis window sizes above.

## Multi-band design

Run up to four tracked bands in parallel from one shared analysis, each with its own tracker, tracked filter and saturator, and add rules so the bands do not fight each other.

**Placing bands.** Two options worth testing:

- **Tap to place:** the user taps up to four spots on the spectrogram. Each tap becomes a band with an automatic window from the rule above. Closest to the prototype and the most intuitive.
- **Fixed coverage:** four preset regions (roughly sub and bass, low mids, mids, presence) that the user can nudge. Faster to use on a full mix, less precise.

**Per band:** tracker, tracked bandpass (phase-locked copy), saturator and a level. Everything else is shared or automatic.

**Rules between bands:**

1. **No shared partials.** Windows may not overlap. If two tracks drift within about a semitone of each other, the weaker band yields for that moment, so one note is never boosted twice.
2. **Harmonic budget.** A low band's generated harmonics land in the higher bands' regions. Cap the total added energy in each region (similar to the suppression stage in Techivation's M-Exciter) so stacking bands does not over-brighten.
3. **Character by register.** Defaults could lean softer and more even-order in the low band (warmth, the missing-fundamental effect that MaxxBass relies on) and gentler odd-order higher up.
4. **Optional harmonic link.** Upper bands can be told to prefer peaks that are harmonics of the low band's tracked note. The bass line then acts as the chord root, which is a simple form of key awareness that needs no key setting.

**Cost.** The expensive part is the analysis, which is shared. Each extra band adds only a peak search, a filter and a saturator, so four bands should cost little more than one.

## Minimising controls

The prototype's nine controls can shrink to three knobs plus the tap gesture, because most of them depend on frequency and can be set from it.

Proposed user-facing set:

- **Tap on the display** places or moves a band. Drag up or down to retune it, swipe away to remove it.
- **Amount** sets how much of the generated signal comes back in, per band or globally.
- **Character** is one sweep from clean (a pure tracked boost) through warm (soft, even-order) to gritty (harder, odd-order).
- **Focus** runs from tight to loose and scales window width, hysteresis and glide together around their automatic values.
- **Key** (optional, tucked away): Off, Auto-detect or a chosen key.

What happens to each prototype control:

| Prototype control | Fate |
| --- | --- |
| Window | Automatic from centre frequency; Focus nudges it |
| Glide | Automatic from centre frequency; Focus nudges it |
| Hysteresis | Automatic; Focus nudges it |
| Gate | Automatic, relative to each band's own level over time |
| Key snap and scale | Folded into Key; Auto-detect uses a key-detection pass |
| Source (filter or sine) | Fixed to tracked filter; sine kept only for sound design |
| Drive | Folded into Character |
| Even harmonics | Folded into Character |
| Wet level | Becomes Amount |

This mirrors how the more polished products in this space present themselves: Techivation markets M-Exciter as minimal, though it still exposes around ten controls plus an auto-set Mix Assist.

## Existing systems and prior art

Several tools cover parts of the idea, and two come close, but none combines tracked bands, saturation of the tracked note and key awareness.

| Tool | What it does | Overlap | What it lacks |
| --- | --- | --- | --- |
| [ChromaDSP Harmonic Boost](https://bedroomproducersblog.com/?p=39846) (free) | Polyphonic pitch tracking adds pitched layers at set intervals, within a high and low pass range | Closest: tracks pitch and adds matching content, with a tracking speed control and latency compensation | Synthesises intervals rather than saturating the real note; one range, not several bands |
| [Aeons Light Harmonic EQ](https://aeonslight-web.pages.dev/plugins/harmonic-eq) | Eight EQ bands that lock to the fundamental and its harmonics as pitch moves; can follow audio, MIDI or a sidechain | A moving boost that follows pitch, as in the tracked filter mode | No saturation or harmonic generation; follows one fundamental rather than per-band peaks |
| [Waves MaxxBass](https://manualmachine.com/wavesaudio/maxxbass/6496600-user-guide) | Splits off the bass, generates harmonics that carry its dynamics, and mixes them back so the ear hears the missing fundamental | The low-band idea, and proof that generated harmonics of bass translate well | Fixed crossover, no pitch selection, one band |
| [Techivation M-Exciter](https://techivation.com/m-exciter/) | Generates upper harmonics from tones in the input, phase-preserving, with a suppression stage and an auto-set assistant | Phase-safe generation and a harmonic budget, plus a minimal-interface pitch | Works on a frequency range, not on a tracked note |
| [Parish Audio X1 Exciter](https://mixdownmag.com.au/news/parish-audios-x1-exciter-a-frequency-focused-harmonic-enhancer/) (July 2026) | Excites a chosen centre frequency and width, dragged directly on a display; Drive, Amount and Character controls | Closest interface: drag a target on the display, one Character control | The target is fixed in frequency; it does not follow the note |
| [Plugin Boutique Scaler EQ](https://www.soundonsound.com/reviews/plugin-boutique-scaler-eq) | Peak bands made of narrow filters on the notes of a chosen key; boosts in-key notes, cuts out-of-key ones | Key awareness on a spectrum | Key is a project setting, not live pitch tracking; EQ only |
| [Haden Cain Harmonic Tracking Filter Sequencer](https://hadencain.gumroad.com/l/bekjp) (free) | Bandpass filter that follows the harmonics of incoming audio, stepping or jumping between harmonics 1 to 21 | A tracked bandpass, as in this design | A creative effect; no saturation or blend-back |

**Research foundations.** The tracking itself rests on sinusoidal modelling, the McAulay-Quatieri approach used by tools such as [SPEAR](https://mashav.com/sha/praat/scripts/partial-editing-resynthesis.html), and on [constant-Q analysis](https://arxiv.org/pdf/1210.0084), which gives finer frequency resolution at low frequencies and finer time resolution at high frequencies. Invertible, real-time constant-Q transforms exist, which matters for a live version.

**The gap:** choosing regions on a spectrum, following the strongest note in each, saturating that note in phase and blending it back, with optional key awareness. Harmonic Boost and Harmonic EQ prove the tracking is workable; X1 Exciter proves the drag-a-region interface sells.

## Risks and open questions

The biggest risk is real-time latency in the low band; the biggest open question is whether this is a mixing tool, a creative effect or both.

| Risk | Why it matters | Mitigation |
| --- | --- | --- |
| Low-band latency in real time | Resolving bass pitch needs roughly 100 to 190 ms of audio | Ship offline or lookahead first; report latency to the host; use variable-Q analysis for faster lows |
| Tracker hopping on dense mixes | Audible warbles and wrong-note distortion | Automatic narrow windows up high, hysteresis, adaptive window refinement |
| Over-brightening when bands stack | Harmonics from low bands pile into higher ones | Per-region harmonic budget |
| Tracked filter rings on very low notes | A narrow filter at 40 Hz takes time to settle and smears transients | Lower filter Q in the low band, or blend in a sine there |
| Crowded market | Several exciters and two tracking tools already exist | Lead with the gap: per-band note tracking plus saturation plus key awareness |

Open questions:

- [ ] Tap-to-place bands, fixed coverage, or both?
- [ ] Target use: mixing and mastering (transparent, safe defaults) or sound design (wilder Character range, sine mode, interval shifts)?
- [ ] Platform: web tool, desktop plugin (VST3, AU, CLAP), or web prototype first then plugin?
- [ ] Is key awareness worth its place in version one, or does harmonic link to the bass band cover it?
- [ ] How much does the automatic window rule need tuning across genres? Needs listening tests on real mixes.

## Roadmap

Build the multi-band web prototype next, then let blind listening tests decide whether the costly real-time engine is worth building.

&#91;embedded content: roadmap · 4 phases, 1 gate\]

Phases 1 and 2 are cheap and reuse the current prototype. The gate sits before phase 3 because the real-time engine, especially low-band latency, is where most of the effort and risk lies. No dates are set yet.

## Sources

- [Prototype: Pitch-follow exciter](https://claude.ai/artifact/NsexbnnktFqEKnaVgLhXpG)
- [Bedroom Producers Blog: ChromaDSP Harmonic Boost](https://bedroomproducersblog.com/?p=39846)
- [Aeons Light: Harmonic EQ](https://aeonslight-web.pages.dev/plugins/harmonic-eq)
- [Waves MaxxBass user guide](https://manualmachine.com/wavesaudio/maxxbass/6496600-user-guide)
- [Techivation: M-Exciter](https://techivation.com/m-exciter/)
- [Mixdown: Parish Audio X1 Exciter](https://mixdownmag.com.au/news/parish-audios-x1-exciter-a-frequency-focused-harmonic-enhancer/)
- [Sound On Sound: Plugin Boutique Scaler EQ review](https://www.soundonsound.com/reviews/plugin-boutique-scaler-eq)
- [Haden Cain: Harmonic Tracking Filter Sequencer](https://hadencain.gumroad.com/l/bekjp)
- [Partial editing and resynthesis guide (McAulay-Quatieri, SPEAR)](https://mashav.com/sha/praat/scripts/partial-editing-resynthesis.html)
- [Holighaus et al.: A framework for invertible, real-time constant-Q transforms](https://arxiv.org/pdf/1210.0084)
