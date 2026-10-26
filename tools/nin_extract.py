#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""NINvave: cut kits and patterns out of Nine Inch Nails multitracks.

    .venv/bin/python tools/nin_extract.py [STEMS_DIR]      (default: ~/.cache/ninvave/stems)

Writes firmware/gen/nin_data.h (the sounds as 8-bit mu-law, the kits, the preset patterns) and
build/nin/ (every sound as a WAV to audition, and a report). Needs numpy, scipy, soundfile, librosa.
Sources and credits: SOURCES.md.
"""
import sys
from pathlib import Path

import librosa
import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parent.parent
STEMS = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / ".cache/ninvave/stems"
OUT_H = ROOT / "firmware/gen/nin_data.h"
OUT_DIR = ROOT / "build/nin"
SR = 22050                      # one-shots; loops at SR / 2
FS = 44100                      # the FM-1's output rate
BUDGET = 400 * 1024             # bytes of sound in flash

HLAH = "hlah-multitrack/Head Like a Hole (115 BPM)/"
PHP = "php-pack/Pretty Hate Pack/"
# Tempos: the bpm at which the song's bars repeat most (Terrible Lie reads 117.5 from the kick alone: a 4/3 misread).
# Folder numbers of the Pretty Hate Pack, matched by length and tempo (2026-10-26): 4 = Head Like a Hole
# (same as the HLAH multitrack), 1 = Sanctified, 2 = The Only Time, 3 = Terrible Lie.
SONGS = [
    dict(name="HEAD LIKE A HOLE", tag="HLAH", bpm=115.0,
         kick=HLAH + "01. Kick.wav", snare=HLAH + "02. Snare.wav", hat=HLAH + "03. Cymbals & Toms.wav",
         perc=HLAH + "08. Extras.wav", loop=PHP + "4/08 Vocal & Percussive Loops.flac",
         bass=HLAH + "04. Bass Guitar.wav", synth=HLAH + "07. Keys.wav"),
    dict(name="TERRIBLE LIE", tag="TLIE", bpm=87.6,
         kick=PHP + "3/01 Kick.flac", snare=PHP + "3/02 Snare.flac", hat=PHP + "3/03 Drums Other.flac",
         perc=PHP + "3/08 Other Synths & FX.flac", loop=PHP + "3/08 Other Synths & FX.flac",
         bass=PHP + "3/04 Bass.flac", synth=PHP + "3/07 Synths.flac"),
    dict(name="SANCTIFIED", tag="SANC", bpm=107.7,
         kick=PHP + "1/01 Kick.flac", snare=PHP + "1/02 Drums Other.flac", hat=PHP + "1/02 Drums Other.flac",
         perc=PHP + "1/07 Percussion FX & Chants.flac", loop=PHP + "1/07 Percussion FX & Chants.flac",
         bass=PHP + "1/03 Bass.flac", synth=PHP + "1/06 Synths.flac"),
    dict(name="THE ONLY TIME", tag="ONLY", bpm=94.0,
         kick=PHP + "2/01 Kick.flac", snare=PHP + "2/02 Snare.flac", hat=PHP + "2/03 Drums Other.flac",
         perc=PHP + "2/07 Other Synths & FX.flac", loop=PHP + "2/07 Other Synths & FX.flac",
         bass=PHP + "2/04 Bass.flac", synth=PHP + "2/06 Synths.flac"),
]
TRACKS = ["kick", "snare", "hat", "perc", "loop", "bass", "synth"]
SECONDS = dict(kick=0.40, snare=0.45, hat=0.22, perc=0.60, bass=0.80, synth=1.00)   # one-shot lengths (max)
# spectral centroid windows (Hz) for picking a hit out of a mixed stem
CENTROID = dict(kick=(0, 1500), snare=(900, 3500), hat=(3500, 20000), perc=(0, 20000))
THRESH = dict(kick=0.3, snare=0.3, hat=0.12, perc=0.3)   # of the stem's 90th-percentile hit
PATTERNS_PER_SONG = 3

_cache = {}


def load(rel):
    if rel not in _cache:
        y, _ = librosa.load(STEMS / rel, sr=SR, mono=True)
        _cache[rel] = y.astype(np.float32)
    return _cache[rel]


def onsets(y):
    env = librosa.onset.onset_strength(y=y, sr=SR, hop_length=256)
    on = librosa.onset.onset_detect(onset_envelope=env, sr=SR, hop_length=256, units="samples", backtrack=True)
    peak = np.array([np.abs(y[o:o + 2048]).max() if o < len(y) else 0 for o in on])
    return on, peak


def centroid(seg):
    seg = seg[:2048]
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n=2048))
    f = np.fft.rfftfreq(2048, 1 / SR)
    return float((spec * f).sum() / (spec.sum() + 1e-9))


def trim(seg, seconds):
    seg = seg[:int(seconds * SR)].copy()
    # cut the tail once it stays under -48 dB of the peak, then fade the last 8 ms
    pk = np.abs(seg).max() + 1e-9
    loud = np.where(np.abs(seg) > pk * 0.004)[0]
    if len(loud):
        seg = seg[:min(len(seg), loud[-1] + 64)]
    fade = min(len(seg), int(0.008 * SR))
    seg[-fade:] *= np.linspace(1, 0, fade)
    return seg / pk * 0.95


def mixed(rel):
    """A stem with more than one drum in it: its hits are told apart by their brightness."""
    return "Other" in rel or "Cymbals" in rel


def pick_hit(kind, rel):
    """A typical, clean hit: well separated from the next onset, loud, in the kind's frequency band."""
    y = load(rel)
    on, peak = onsets(y)
    need = int(0.12 * SR)
    lo, hi = CENTROID[kind] if mixed(rel) else (0, 1e9)
    cands = []
    for i, o in enumerate(on):
        gap = (on[i + 1] if i + 1 < len(on) else len(y)) - o
        if gap < need or peak[i] < 0.05:
            continue
        c = centroid(y[o:o + 2048])
        if lo <= c <= hi:
            cands.append((peak[i], o, gap))
    if not cands:
        raise SystemExit(f"no {kind} hit found")
    cands.sort(key=lambda c: c[0])
    p, o, gap = cands[int(len(cands) * 0.8)]          # loud but not the loudest (a fill, a flam)
    return trim(y[o:o + gap], SECONDS[kind])


def pick_note(kind, y):
    """A sustained, steady note at an onset: (audio, MIDI root)."""
    fmin, fmax = (30, 400) if kind == "bass" else (70, 1200)
    on, peak = onsets(y)
    best = None
    loud = sorted((i for i, o in enumerate(on) if o + int(0.5 * SR) <= len(y)), key=lambda i: -peak[i])[:40]
    for i in loud:
        o = on[i]
        seg = y[o:o + int(SECONDS[kind] * SR)]
        f0, voiced, _ = librosa.pyin(seg, fmin=fmin, fmax=fmax, sr=SR, frame_length=2048, hop_length=512)
        v = f0[voiced] if voiced is not None else np.array([])
        if len(v) < 8:
            continue
        cents = 1200 * np.log2(v / np.median(v))
        score = float(np.mean(np.abs(cents) < 40)) * len(v) * peak[i]
        if best is None or score > best[0]:
            best = (score, seg, float(np.median(v)))
    if best is None:
        raise SystemExit(f"no {kind} note found")
    return trim(best[1], SECONDS[kind]), int(round(librosa.hz_to_midi(best[2])))


def beat_grid(song):
    """Beats (samples) from the first downbeat, and the tempo: kick on 1 and 3, snare on 2 and 4."""
    k, s = load(song["kick"]), load(song["snare"])
    n = min(len(k), len(s))
    # the tempo is given (measured once per song, see SONGS): the tracker only finds where the beats fall
    _, beats = librosa.beat.beat_track(y=k[:n] + s[:n], sr=SR, hop_length=256, units="samples", bpm=song["bpm"])
    bpm = 60 * SR / np.median(np.diff(beats))
    ke, se = np.abs(k), np.abs(s)
    def energy(e, b): return sum(e[x:x + 1024].sum() for x in b if x + 1024 < n)
    best = max(range(4), key=lambda ph: energy(ke, beats[ph::4]) + energy(ke, beats[ph + 2::4])
               - energy(se, beats[ph::4]) - energy(se, beats[ph + 2::4]))
    return beats[best:], bpm


def steps_of(beats):
    """The 16th grid: each beat cut in four."""
    out = []
    for a, b in zip(beats[:-1], beats[1:]):
        out += [a + (b - a) * q // 4 for q in range(4)]
    return np.array(out)


def hit_steps(rel, grid, kind):
    y = load(rel)
    on, peak = onsets(y)
    thr = np.percentile(peak, 90) * THRESH[kind] if len(peak) else 1
    lo, hi = CENTROID.get(kind, (0, 20000))
    hits = np.zeros(len(grid), bool)
    half = np.median(np.diff(grid)) / 2
    for o, p in zip(on, peak):
        if p < thr:
            continue
        if mixed(rel) and kind in ("snare", "hat") and not lo <= centroid(y[o:o + 2048]) <= hi:
            continue
        j = int(np.argmin(np.abs(grid - o)))
        if abs(grid[j] - o) <= half:
            hits[j] = True
    return hits


def note_steps(y, grid, bars, kind):
    """For the chosen bars: the MIDI note starting on each 16th (0 = none). A step sounds when it is loud enough; a
    note starts there when the one before was quiet or the pitch moved by a semitone or more (legato lines too)."""
    fmin, fmax = (30, 400) if kind == "bass" else (70, 1200)
    out = {}
    for bar in bars:
        idx = [grid[bar * 16 + i] for i in range(16)] + [grid[bar * 16 + 16] if bar * 16 + 16 < len(grid) else len(y)]
        seg = y[idx[0]:idx[16]]
        f0 = librosa.yin(seg, fmin=fmin, fmax=fmax, sr=SR, frame_length=2048, hop_length=256)
        rms = np.array([np.sqrt(np.mean(y[idx[i]:idx[i + 1]] ** 2)) for i in range(16)])
        loud = rms > max(rms.max() * 0.3, 0.01)
        notes, prev = [0] * 16, 0
        for i in range(16):
            if not loud[i]:
                prev = 0
                continue
            fa, fb = (idx[i] - idx[0]) // 256 + 1, (idx[i + 1] - idx[0]) // 256
            n = int(round(librosa.hz_to_midi(np.median(f0[fa:max(fb, fa + 1)]))))
            if not prev or abs(n - prev) >= 1:
                notes[i] = n
            prev = n
        out[bar] = notes
    return out


def mulaw(x):
    x = np.clip(x, -1, 1)
    y = np.sign(x) * np.log1p(255 * np.abs(x)) / np.log1p(255)
    return np.clip(np.round((y + 1) * 127.5), 0, 255).astype(np.uint8)


def mulaw_table():
    y = np.arange(256) / 127.5 - 1
    x = np.sign(y) * (np.power(256, np.abs(y)) - 1) / 255
    return np.round(x * 32767).astype(int)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    report = []
    sounds = []        # (song, track, audio, rate, root)
    presets = []
    for si, song in enumerate(SONGS):
        print(f"== {song['name']}", flush=True)
        for t in ("kick", "snare", "hat", "perc"):
            sounds.append((si, t, pick_hit(t, song[t]), SR, 60))
        beats, bpm = beat_grid(song)
        grid = steps_of(beats)
        nbar = len(grid) // 16
        tr = {t: hit_steps(song[t], grid, t) for t in ("kick", "snare", "hat", "perc")}
        sig = {}
        for b in range(nbar):
            key = tuple(int(sum(1 << i for i in range(16) if tr[t][b * 16 + i])) for t in ("kick", "snare", "hat", "perc"))
            if key[0] or key[1]:
                sig.setdefault(key, []).append(b)
        common = sorted(sig.items(), key=lambda kv: -len(kv[1]))[:PATTERNS_PER_SONG]
        bars = [v[0] for _, v in common]
        # the loop: the loudest bar of its stem, at half rate
        ly = load(song["loop"])
        lb = max(range(nbar - 1), key=lambda b: float(np.abs(ly[grid[b * 16]:grid[b * 16 + 16]]).mean()))
        loop = librosa.resample(ly[grid[lb * 16]:grid[lb * 16 + 16]], orig_sr=SR, target_sr=SR // 2)
        loop = loop / (np.abs(loop).max() + 1e-9) * 0.95
        sounds.append((si, "loop", loop, SR // 2, 60))
        for t in ("bass", "synth"):
            a, root = pick_note(t, load(song[t]))
            sounds.append((si, t, a, SR, root))
        bn = note_steps(load(song["bass"]), grid, bars, "bass")
        sn = note_steps(load(song["synth"]), grid, bars, "synth")
        for n, (key, where) in enumerate(common):
            presets.append(dict(song=si, name=f"{song['tag']} {'ABC'[n]}", bpm=bpm, masks=list(key),
                                bass=bn[where[0]], synth=sn[where[0]], bars=len(where)))
        report.append(f"{song['name']}: {bpm:.1f} BPM, {nbar} bars, patterns "
                      + ", ".join(f"{'ABC'[n]} x{len(w)} (bar {w[0]})" for n, (_, w) in enumerate(common)))

    total = sum(len(s[2]) for s in sounds)
    if total > BUDGET:
        raise SystemExit(f"sounds: {total} B over the budget of {BUDGET} B")
    for si, t, a, rate, root in sounds:
        sf.write(OUT_DIR / f"{SONGS[si]['tag']}-{t}.wav", a, rate)

    # ---- the header
    pcm = bytearray()
    rows = []
    for si, t, a, rate, root in sounds:
        rows.append((len(pcm), len(a), rate * 65536 // FS, root, TRACKS.index(t), si))
        pcm += mulaw(a).tobytes()
    L = ["/* SPDX-License-Identifier: GPL-3.0-only */",
         "/* Generated by tools/nin_extract.py from the Nine Inch Nails multitracks (SOURCES.md). Do not edit. */",
         "#pragma once", "#include <stdint.h>", "",
         f"#define NIN_NSONG {len(SONGS)}u", f"#define NIN_NTRACK {len(TRACKS)}u",
         f"#define NIN_NSOUND {len(sounds)}u", f"#define NIN_NPRESET {len(presets)}u", f"#define NIN_PCM_BYTES {len(pcm)}u", "",
         "static const char *const NIN_SONG[NIN_NSONG] = {" + ", ".join(f'"{s["name"]}"' for s in SONGS) + "};",
         "static const char *const NIN_SONG_TAG[NIN_NSONG] = {" + ", ".join(f'"{s["tag"]}"' for s in SONGS) + "};",
         "static const int16_t NIN_ULAW[256] = {" + ", ".join(map(str, mulaw_table())) + "};", "",
         "typedef struct { uint32_t off, len, step; uint8_t root, track, song, pad; } nin_sound_t;   /* step: Q16 per 44.1 kHz frame */",
         "static const nin_sound_t NIN_SOUND[NIN_NSOUND] = {"]
    L += [f"    {{{o}u, {n}u, {st}u, {r}, {t}, {s}, 0}},   /* {SONGS[s]['tag']} {TRACKS[t]} */" for o, n, st, r, t, s in rows]
    L += ["};", "",
          "typedef struct { char name[8]; uint8_t song; uint16_t bpm10; uint16_t mask[5]; uint8_t bass[16], synth[16]; } nin_preset_t;",
          "static const nin_preset_t NIN_PRESET[NIN_NPRESET] = {"]
    for p in presets:
        masks = p["masks"] + [1]                       # the loop: on the one
        L.append(f'    {{"{p["name"]}", {p["song"]}, {int(round(p["bpm"] * 10))}u, {{{", ".join(f"0x{m:04X}u" for m in masks)}}},')
        L.append(f'     {{{", ".join(map(str, p["bass"]))}}},')
        L.append(f'     {{{", ".join(map(str, p["synth"]))}}}}},')
    L += ["};", "", "static const uint8_t NIN_PCM[NIN_PCM_BYTES] = {"]
    for i in range(0, len(pcm), 24):
        L.append("    " + ",".join(str(b) for b in pcm[i:i + 24]) + ",")
    L += ["};", ""]
    OUT_H.write_text("\n".join(L))

    report.append(f"sounds: {len(sounds)}, {len(pcm)} B of {BUDGET} B")
    for p in presets:
        report.append(f"  {p['name']:8s} {p['bpm']:6.1f}  " + "  ".join(
            "".join("x" if m >> i & 1 else "." for i in range(16)) for m in p["masks"])
            + "  bass " + " ".join(str(n) if n else "." for n in p["bass"]))
    for si, t, a, rate, root in sounds:
        report.append(f"  {SONGS[si]['tag']} {t:5s} {len(a) / rate:5.2f}s  root {root}")
    (OUT_DIR / "report.txt").write_text("\n".join(report) + "\n")
    print("\n".join(report))
    print(f"-> {OUT_H}")


if __name__ == "__main__":
    main()
