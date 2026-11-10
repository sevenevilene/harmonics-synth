# harmonics-synth

An additive synthesizer for [Pure Data](https://puredata.info/) that you play from a MIDI keyboard. Each note is built from 129 sine waves (subharmonics below the root, the root itself, and harmonics above it), and the split moves with the pitch, so the partials stay in the audible range: high notes bring in more subharmonics, low notes more harmonics. Their levels live in one array and their stereo position in two more (left and right), all of which you can draw on with the mouse, set from a preset, or have an algorithm write and animate.

## Features

- 8-voice polyphony, 129 partials per voice
- The arrays hold 257 slots: 128 subharmonics (root ÷ 129 … root ÷ 2) on the left, the root in the middle, and 128 harmonics (2× … 129×) on the right
- **Moving root**: each note plays a 129-slot window of the arrays that slides with pitch. A note at frequency *f* has about *f*/20 − 1 subharmonics above 20 Hz and 18000/*f* − 1 harmonics below 18 kHz; the 128 non-root partials are shared between the two sides in that proportion, never giving a side more than can be heard. A low C (65 Hz) gets 1 subharmonic and 127 harmonics; a high E (2.6 kHz) gets 123 subharmonics and 5 harmonics. In the middle of the keyboard (around 440 Hz) only about 60 whole-number partials fit between 20 Hz and 18 kHz at all, so the window takes all the audible subharmonics and the rest of its harmonics fall above 18 kHz and are muted
- A thin bar under the pan graphs shows which part of the arrays the last note you played uses, with a tall mark at the root
- Levels stored in the `amps` array, and stereo panning in two more arrays, `panL` and `panR`: how much of each partial goes to the left and to the right speaker (0–1). What you hear on each side is level × pan. Both pan graphs have 1 at the top. Panning uses an equal-power law: a partial in the centre is at 0.71 in both graphs, a partial fully to one side is at 1 in that speaker and 0 in the other, and it keeps about the same loudness wherever it is placed. Draw on any of them; all three are saved with the patch
- Presets: saw, square, triangle, sine, even, organ, bell, clear. Presets and algorithms are mirrored, so subharmonic ÷n gets the same level as harmonic ×n
- **subs** slider: scales the whole subharmonic side, from 0 (plain harmonics) to 1 (full mirror image) to fatten the sound
- Algorithms that compute the spectrum:
  - **tilt**: `1 / n^tilt`, so one slider goes from bright (0) through saw (1) to dark (3)
  - **formant**: a resonant peak at harmonic *center* with a given *width*; when animated, the peak sweeps up and down for a "wah"/vowel effect
  - **lfo**: each harmonic slowly fades in and out at its own rate on top of the tilt slope, so the tone keeps evolving
- **animate** toggle and **speed** slider to run the formant and lfo algorithms continuously
- **Pan curves** that write both pan arrays from a formula, starting from the root in the centre and moving smoothly outwards:
  - **spread**: subharmonics glide to the left and harmonics to the right along a smooth S-curve (`tanh`)
  - **alternate**: neighbouring partials take turns going left and right, going further out the further they are from the root
  - **log** (the default, applied when the patch opens): keeps the bass in the middle. Every subharmonic, the root and ×2 stay centred; from ×3 up the harmonics widen evenly per octave, taking turns between the speakers (odd harmonics left, even ones right), until they are fully wide at harmonic number *reach* (4 at the least)
  - **log-side**: the same as log, but every harmonic moves towards one speaker (right, or left with a negative spread) instead of taking turns
  - **spread** slider (−1 to 1): how far out the curve goes; negative values swap the sides
  - **reach** slider (1 to 64): how many partials from the root it takes to get most of the way out (small = wide almost at once, large = a slow fan-out); for log, the harmonic number where it is fully wide
  - **centre**: sets both pan arrays to 0.71 (every partial in the centre) and turns the pan curve off
  - The sliders only redraw the pans while spread or alternate is selected, so pans you drew by hand are kept
- Attack, release and master volume controls
- **glide** toggle (off by default): the graphs take over the attack and release. Each new note starts every partial from 0 and ramps it up to its graph value over the attack time, and on release every partial glides down to 0 over the release time; the regular volume envelope is switched off (it opens in 1 ms and holds until the voice goes quiet). While a note is held, any change to the graphs glides there instead of jumping: rising values take the attack time and falling ones the release time. Try it with a long attack and release while clicking presets, switching pan curves or with animate on. With glide off, the regular volume envelope does the attack and release and partials follow the graphs with a quick 30 ms smoothing
- Partials above ~18 kHz (aliasing) and below 20 Hz (inaudible rumble) are muted automatically
- Audio switches on automatically when the patch opens

## Test tones (no keyboard needed)

The **TEST TONES** section in the main patch plays notes with the mouse, through the same voices as the keyboard:

- **24 … 108**: click to hold that note (C1 … C8) until you press release all, so you can move sliders while it sounds
- **60 64 67**: holds a C major chord
- **note** number box: drag or type any MIDI note number to hold it
- **stop** (release all): releases every held note
- **sweep C1-C8**: plays C1, C2 … C8 and back, one note every 1.5 seconds, so you can hear the root move

## CPU

8 voices × 129 partials is about 1,000 oscillators (the arrays are larger, but each voice only plays its 129-slot window). To keep that manageable, each partial reads its level and pan from the arrays about every 26 ms (and smooths between readings) instead of on every sample. The refresh is staggered, a few partials per millisecond, so the work never piles up in one audio block, and each voice switches its audio processing off when it is silent, so only the notes you are playing use CPU. If you still hear crackles, raise the audio buffer under *Media → Audio Settings*, or lower the voice count in `poly 8 1` and `clone -s 1 voice 8` in `harmonics.pd`.

## Requirements

Pure Data vanilla 0.49 or newer (the patch uses `clone` and `expr`).

## Usage

1. Keep all the `.pd` files in the same folder.
2. Open `harmonics.pd` in Pd.
3. Choose your keyboard under *Media → MIDI Settings*.
4. Play, and click a preset or algorithm, move the sliders, or draw on the level and pan arrays.

Presets, algorithms and their sliders (including subs) write only the levels array; moving one, or running animation, replaces levels you drew by hand. To draw your own spectrum, click a preset first and turn animation off. The pan arrays are only changed by you, by the pan curve buttons, and by the spread and reach sliders while a pan curve is selected. To draw your own pans, click centre first (or just draw over a curve, then leave the sliders alone). Because the log curve is applied when the patch opens, pans you drew are replaced the next time you open it.

## Using it in Logic or another DAW (plugdata)

Load the patch in the [plugdata](https://plugdata.org/) instrument plugin. The `pd daw-automation` subpatch registers these controls as DAW automation parameters:

| Parameter | Control | Range |
|---|---|---|
| Attack | attack (ms) | 1–2000, logarithmic |
| Release | release (ms) | 5–5000, logarithmic |
| Volume | master volume | 0–1 |
| Subs | subharmonic level | 0–1 |
| Tilt | tilt (tilt and lfo algorithms) | 0–3 |
| Center | formant center | 1–32 |
| Width | formant width | 0.5–8 |
| Speed | animation speed | 0–4 |
| Animate | animation on/off | 0 or 1 |
| Spread | pan curve spread | −1 to 1 |
| Reach | pan curve reach | 1–64 |
| Glide | glide on/off | 0 or 1 |

Automation in the DAW moves the matching slider in the patch, and moving a slider updates the DAW. In Logic you can also map them to hardware knobs with *Learn Controller Assignment*. If you hear crackles, raise Logic's I/O buffer size and close the plugdata window while playing.

In plain Pd the `param` objects don't exist, so Pd prints twelve "couldn't create" errors when the patch opens. They are harmless.

## Adding your own algorithm

The formulas are `expr` boxes in `engine.pd`. Each one gets the harmonic number *n* (1 = root, 2 = 2× or ÷2, up to 129) as `$f1` and outputs that harmonic's level (0–1). To add one, give it a new mode number in the two `route` objects and add a message box to the main patch that sends its name to `preset`.

## Files

- `harmonics.pd`: main patch with the controls, MIDI input and audio output
- `engine.pd`: writes the `amps` array (presets and algorithms)
- `pans.pd`: writes the `panL` and `panR` arrays (pan curves)
- `voice.pd`: a single voice, loaded 8 times by the main patch; works out how many subharmonics each note gets
- `partial.pd`: a single partial, loaded 129 times by each voice; picks its ratio and array slot from the note's window
