# harmonics-synth

An additive synthesizer for [Pure Data](https://puredata.info/) that you play from a MIDI keyboard. Each note is built from 129 sine waves (subharmonics below the root, the root itself, and harmonics above it), and the split moves with the pitch, so the partials stay in the audible range: high notes bring in more subharmonics, low notes more harmonics. Their levels live in one array and their stereo position in two more (left and right), all of which you can draw on with the mouse, set from a preset, or have an algorithm write and animate.

## Features

- 8-voice polyphony, 129 partials per voice
- The arrays hold 257 slots: 128 subharmonics (root ÷ 129 … root ÷ 2) on the left, the root in the middle, and 128 harmonics (2× … 129×) on the right
- **Moving root**: each note plays a 129-slot window of the arrays that slides with pitch. A note at frequency *f* has about *f*/20 − 1 subharmonics above 20 Hz and 18000/*f* − 1 harmonics below 18 kHz; the 128 non-root partials are shared between the two sides in that proportion, never giving a side more than can be heard. A low C (65 Hz) gets 1 subharmonic and 127 harmonics; a high E (2.6 kHz) gets 123 subharmonics and 5 harmonics. In the middle of the keyboard (around 440 Hz) only about 60 whole-number partials fit between 20 Hz and 18 kHz at all, so the window takes all the audible subharmonics and the rest of its harmonics fall above 18 kHz and are muted
- A thin bar under the pan graphs shows which part of the arrays the last note you played uses, with a tall mark at the root
- Levels stored in the `amps` array, and stereo panning in two more arrays, `panL` and `panR`: how much of each partial goes to the left and to the right speaker (0–1). What you hear on each side is level × pan. The right pan graph is drawn upside down (1 at the bottom), so the two pan graphs mirror each other. Draw on any of them; all three are saved with the patch
- Presets: saw, square, triangle, sine, even, organ, bell, clear. Presets and algorithms are mirrored, so subharmonic ÷n gets the same level as harmonic ×n
- **subs** slider: scales the whole subharmonic side, from 0 (plain harmonics) to 1 (full mirror image) to fatten the sound
- Algorithms that compute the spectrum:
  - **tilt**: `1 / n^tilt`, so one slider goes from bright (0) through saw (1) to dark (3)
  - **formant**: a resonant peak at harmonic *center* with a given *width*; when animated, the peak sweeps up and down for a "wah"/vowel effect
  - **lfo**: each harmonic slowly fades in and out at its own rate on top of the tilt slope, so the tone keeps evolving
- **animate** toggle and **speed** slider to run the formant and lfo algorithms continuously
- **centre all pans** button: sets both pan arrays back to 1 (every partial in both speakers)
- Attack, release and master volume controls
- Partials above ~18 kHz (aliasing) and below 20 Hz (inaudible rumble) are muted automatically
- Audio switches on automatically when the patch opens

## CPU

8 voices × 129 partials is about 1,000 oscillators (the arrays are larger, but each voice only plays its 129-slot window). To keep that manageable, each partial reads its level and pan from the arrays about every 26 ms (and smooths between readings) instead of on every sample. The refresh is staggered, a few partials per millisecond, so the work never piles up in one audio block, and each voice switches its audio processing off when it is silent, so only the notes you are playing use CPU. If you still hear crackles, raise the audio buffer under *Media → Audio Settings*, or lower the voice count in `poly 8 1` and `clone -s 1 voice 8` in `harmonics.pd`.

## Requirements

Pure Data vanilla 0.49 or newer (the patch uses `clone` and `expr`).

## Usage

1. Keep all the `.pd` files in the same folder.
2. Open `harmonics.pd` in Pd.
3. Choose your keyboard under *Media → MIDI Settings*.
4. Play, and click a preset or algorithm, move the sliders, or draw on the level and pan arrays.

Presets, algorithms and their sliders (including subs) write only the levels array; moving one, or running animation, replaces levels you drew by hand. To draw your own spectrum, click a preset first and turn animation off. The pan arrays are only ever changed by you (or the centre all pans button).

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

Automation in the DAW moves the matching slider in the patch, and moving a slider updates the DAW. In Logic you can also map them to hardware knobs with *Learn Controller Assignment*. If you hear crackles, raise Logic's I/O buffer size and close the plugdata window while playing.

In plain Pd the `param` objects don't exist, so Pd prints nine "couldn't create" errors when the patch opens. They are harmless.

## Adding your own algorithm

The formulas are `expr` boxes in `engine.pd`. Each one gets the harmonic number *n* (1 = root, 2 = 2× or ÷2, up to 129) as `$f1` and outputs that harmonic's level (0–1). To add one, give it a new mode number in the two `route` objects and add a message box to the main patch that sends its name to `preset`.

## Files

- `harmonics.pd`: main patch with the controls, MIDI input and audio output
- `engine.pd`: writes the `amps` array (presets and algorithms)
- `voice.pd`: a single voice, loaded 8 times by the main patch; works out how many subharmonics each note gets
- `partial.pd`: a single partial, loaded 129 times by each voice; picks its ratio and array slot from the note's window
