# NINvave 0.1: how to play it

## The screen

```
 NINVAVE  PLAY            <- PLAY while running, REC (red) while recording
    HLAH A                <- the pattern: a NIN preset, or USER 1..8
 HEAD LIKE A HOLE 114.8 BPM   <- its song and the tempo (EXT: following a MIDI clock)
 ▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪         <- KICK      the grid: 7 tracks x 16 steps
 ▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪         <- SNARE     red row = the selected track
 ▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪         <- HAT       white column = the step playing now
 ▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪         <- PERC      dim row = muted
 ▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪         <- LOOP
 ▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪         <- BASS
 ▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪▪         <- SYNTH
      KICK                <- the selected track
 HLAH KICK  P+0 D15 L14   <- its sound, Pitch, Decay, Level
 STEP KEYS                <- what the keys do; the FX
```

## Start a jam (30 seconds)

1. Turn **PRESETS** to a pattern (`HLAH A` .. `ONLY C`).
2. Press **PLAY**.
3. Press **FX** for grit: CRUSH, then DRIVE, then off.
4. Turn **PRESETS** while it plays: the next pattern takes over on the spot, in step.

## The two key modes: SEQ

**SEQ** switches what the keys do. The SEQ light is **on in STEP mode**.

### STEP mode: build the pattern

The **16 white keys are the 16 steps** of the selected track, F3 (step 1) to G5 (step 16). Press one to turn the
step on or off. A lit step glows dimly; the playing step is bright.

The **black keys pick the track**, left to right, and play it once so you hear what it is:

| Black key | F#3 | G#3 | A#3 | C#4 | D#4 | F#4 | G#4 | **A#4** | **C#5** |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| | KICK | SNARE | HAT | PERC | LOOP | BASS | SYNTH | mute / unmute the track | clear the track |

(D#5 and F#5 do nothing yet.) The **SELECT** knob picks the track as well.

A BASS or SYNTH step you switch on plays the note it had before (from the preset), or else the sound's own note.
To put other notes in, record them (below).

### PLAY mode: play it live

Every key plays the **selected track**, chromatically. On BASS and SYNTH the keys are in tune: C4 is middle C.
On the drums, C4 is the sound as recorded and the other keys pitch it up and down, like an old sampler.
**OCT-** and **OCT+** shift the keys by octaves (two each way).

Notes from a MIDI keyboard (USB or the TRS jack) play the selected track the same way.

## Record

1. In PLAY mode, select the track (SELECT knob).
2. Press **PLAY** to start the pattern, then **REC** (the REC light and the red REC on screen).
3. Play: every note lands on the nearest step, with its pitch on BASS and SYNTH.
4. Press **REC** again to stop recording. The pattern keeps playing.

To start over on a track, go to STEP mode and use **C#5** (clear).

## Shape the sounds: the four knobs

They work on the **selected track**:

| Knob | | |
| --- | --- | --- |
| KNOB1 | SOUND | any of the 28 sounds: put the Sanctified kick under the Head Like a Hole groove, or a snare on the BASS track |
| KNOB2 | PITCH | -24..+24 semitones, the whole track |
| KNOB3 | DECAY | 15 = the full sound; lower = shorter, tighter |
| KNOB4 | LEVEL | 0..15 |

## Tempo and sync

- **ALGORITHM**: the tempo, 1 BPM a click (40..240). Choosing a preset sets its song's tempo again.
- The LOOP track follows the tempo, tape style: faster tempo, faster and higher loop.
- **MIDI clock in** (USB or TRS): send START and NINvave follows the clock (EXT on screen); STOP stops it. Without
  a clock for 2 seconds it goes back to its own tempo.

## Save your patterns

There are 8 user slots, after the presets on the **PRESETS** knob: `USER 1` .. `USER 8` (`NEW` = empty).

- **SAVE** on a NIN preset you changed: it goes into the first empty user slot (when all 8 are full: USER 1).
- **SAVE** on a user slot: it is saved there.
- To put a pattern in a particular slot: turn PRESETS to that slot (an empty one keeps the pattern you have), then SAVE.

The sound stops for a moment while it saves (the flash is being written).

**Careful:** turning PRESETS loads that pattern at once, and unsaved changes are gone. Save first.

NINvave remembers the last pattern, the FX and the octave when you switch off.

## Every control

| Control | |
| --- | --- |
| PRESETS | pattern: 12 NIN presets, then USER 1..8 |
| SELECT | track |
| ALGORITHM | tempo |
| KNOB1..4 | sound, pitch, decay, level of the track |
| MASTER | volume |
| SEQ | STEP mode (light on) / PLAY mode |
| PLAY | start / stop (always from step 1) |
| REC | record on / off |
| FX | off, CRUSH, DRIVE |
| OCT- OCT+ | octave of the keys in PLAY mode |
| SAVE | save the pattern |
| white keys | STEP: the 16 steps; PLAY: notes |
| black keys | STEP: tracks, mute, clear; PLAY: notes |
| HOME EDIT ENV LFO SEL GLO ARP | nothing yet |

## The presets

| | A | B | C |
| --- | --- | --- | --- |
| Head Like a Hole (114.8) | HLAH A | HLAH B | HLAH C |
| Terrible Lie (87.6) | TLIE A | TLIE B | TLIE C |
| Sanctified (107.7) | SANC A | SANC B | SANC C |
| The Only Time (94.0) | ONLY A | ONLY B | ONLY C |

A is the bar that repeats most in the song, then B, then C. Each song's kit is loaded with its patterns.

## If something goes wrong

- **It will not start:** hold **OCT-** while switching on (the USB rescue), then install again over USB.
- **Back to the official firmware:** M-VAVE's updater, or the [FM-1 Firmware Hub](https://thefirmwarehub.com).
- **A red screen with numbers:** a crash. Write down the numbers (or take a photo); the FM-1 restarts by itself.
