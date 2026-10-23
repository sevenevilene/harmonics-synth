# harmonics-synth

An additive synthesizer for [Pure Data](https://puredata.info/) that you play from a MIDI keyboard. Each note is built from 64 sine-wave harmonics. Their levels live in one array, which you can draw on with the mouse, set from a preset, or have an algorithm write and animate.

## Features

- 8-voice polyphony, 64 harmonics per voice (1× to 64× the fundamental)
- Harmonic levels stored in the `amps` array: draw on it directly, and it is saved with the patch
- Presets: saw, square, triangle, sine, even, organ, bell, clear
- Algorithms that compute the spectrum:
  - **tilt**: `1 / n^tilt`, so one slider goes from bright (0) through saw (1) to dark (3)
  - **formant**: a resonant peak at harmonic *center* with a given *width*; when animated, the peak sweeps up and down for a "wah"/vowel effect
  - **lfo**: each harmonic slowly fades in and out at its own rate on top of the tilt slope, so the tone keeps evolving
- **animate** toggle and **speed** slider to run the formant and lfo algorithms continuously
- Attack, release and master volume controls
- Harmonics above ~18 kHz are muted automatically to avoid aliasing
- Audio switches on automatically when the patch opens

## Requirements

Pure Data vanilla 0.49 or newer (the patch uses `clone` and `expr`).

## Usage

1. Keep all the `.pd` files in the same folder.
2. Open `harmonics.pd` in Pd.
3. Choose your keyboard under *Media → MIDI Settings*.
4. Play, and click a preset or algorithm, move the sliders, or draw on the harmonic array.

Moving a slider re-runs the selected algorithm, and animation rewrites the array continuously, so either one replaces anything you drew by hand. To draw your own spectrum, click a preset first and turn animation off.

## Adding your own algorithm

The formulas are `expr` boxes in `engine.pd`. Each one gets the harmonic number *n* (1–64) as `$f1` and outputs that harmonic's level (0–1). To add one, give it a new mode number in the two `route` objects and add a message box to the main patch that sends its name to `preset`.

## Files

- `harmonics.pd`: main patch with the controls, MIDI input and audio output
- `engine.pd`: writes the harmonic levels into the `amps` array (presets and algorithms)
- `voice.pd`: a single voice, loaded 8 times by the main patch
- `partial.pd`: a single harmonic, loaded 64 times by each voice
