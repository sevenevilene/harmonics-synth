# harmonics-synth

An additive synthesizer for [Pure Data](https://puredata.info/) that you play from a MIDI keyboard. Each note is built from 129 sine waves: 64 subharmonics below the root, the root itself, and 64 harmonics above it. Their levels live in one array and their stereo position in two more (left and right), all of which you can draw on with the mouse, set from a preset, or have an algorithm write and animate.

## Features

- 8-voice polyphony, 129 partials per voice: subharmonics (root ÷ 65 … root ÷ 2), the root, and harmonics (2× … 65× the root)
- The root sits in the middle of every array: subharmonics on the left, harmonics on the right
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

8 voices × 129 partials is about 1,000 oscillators. To keep that manageable, each partial reads its level and pan from the arrays about every 26 ms (and smooths between readings) instead of on every sample. The refresh is staggered, a few partials per millisecond, so the work never piles up in one audio block, and each voice switches its audio processing off when it is silent, so only the notes you are playing use CPU. If you still hear crackles, raise the audio buffer under *Media → Audio Settings*, or lower the voice count in `poly 8 1` and `clone -s 1 voice 8` in `harmonics.pd`.

## Requirements

Pure Data vanilla 0.49 or newer (the patch uses `clone` and `expr`).

## Usage

1. Keep all the `.pd` files in the same folder.
2. Open `harmonics.pd` in Pd.
3. Choose your keyboard under *Media → MIDI Settings*.
4. Play, and click a preset or algorithm, move the sliders, or draw on the level and pan arrays.

Presets, algorithms and their sliders (including subs) write only the levels array; moving one, or running animation, replaces levels you drew by hand. To draw your own spectrum, click a preset first and turn animation off. The pan arrays are only ever changed by you (or the centre all pans button).

## Adding your own algorithm

The formulas are `expr` boxes in `engine.pd`. Each one gets the harmonic number *n* (1 = root, 2 = 2× or ÷2, up to 65) as `$f1` and outputs that harmonic's level (0–1). To add one, give it a new mode number in the two `route` objects and add a message box to the main patch that sends its name to `preset`.

## Files

- `harmonics.pd`: main patch with the controls, MIDI input and audio output
- `engine.pd`: writes the `amps` array (presets and algorithms)
- `voice.pd`: a single voice, loaded 8 times by the main patch
- `partial.pd`: a single partial, loaded 129 times by each voice
