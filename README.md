# harmonics-synth

An additive synthesizer for [Pure Data](https://puredata.info/) that you play from a MIDI keyboard. Each note is built from 16 harmonics, and you set the level of each one with its own slider.

## Features

- 8-voice polyphony
- 16 harmonic sliders (1× to 16× the fundamental)
- Presets: saw, square, triangle, organ, sine, bell, even, clear
- Attack, release and master volume controls
- Harmonics above ~18 kHz are muted automatically to avoid aliasing
- Audio switches on automatically when the patch opens

## Requirements

Pure Data vanilla 0.47 or newer (the patch uses the `clone` object).

## Usage

1. Keep `harmonics.pd` and `voice.pd` in the same folder.
2. Open `harmonics.pd` in Pd.
3. Choose your keyboard under *Media → MIDI Settings*.
4. Play, and drag the harmonic sliders or click a preset to change the sound.

## Files

- `harmonics.pd`: main patch with the controls, MIDI input and audio output
- `voice.pd`: a single voice, loaded 8 times by the main patch
