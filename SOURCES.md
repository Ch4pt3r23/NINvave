# Sources

Every sound and preset pattern in NINvave (`firmware/gen/nin_data.h`) is cut by `tools/nin_extract.py` from Nine
Inch Nails multitracks. The recordings are Nine Inch Nails'. NINvave's GPL covers its code, not these excerpts.
Nine Inch Nails is not affiliated with NINvave.

## Where the multitracks came from

- **[nindestruct.com](https://nindestruct.com)** (remix page: <https://nindestruct.com/remix.html>): a fan archive
  (est. 2014, Pyract), not an official site, with no licence stated. NINvave's Pretty Hate Machine stems come from
  its Pretty Hate Machine section. That section credits the *Pretty Hate Pack* to "Fine Finch Tails", and marks the
  Head Like a Hole, Terrible Lie, Sanctified and The Only Time multitracks as **"Ripped from Rock Band 3"**. They
  were not released through remix.nin.com.
- **remix.nin.com** (2007-2016): the official Nine Inch Nails remix site. It offered multitracks "for download and
  remixing" (all of *The Slip*, five tracks of *Ghosts I-IV*, songs from *With Teeth* and *Year Zero*, and others),
  and prohibited remixes using other artists' material. It closed in November 2016. Its multitracks are still on
  nindestruct.com, marked "Sourced from remix.nin.com". History:
  <https://www.nin.wiki/Remix.nin.com>.
- **[archive.org: REMIX.NIN.COM - Nine Inch Nails Remixes](https://archive.org/details/remix-nin-com-collection)**
  (uploaded 2018-06-17): the remixes fans submitted to remix.nin.com. No licence stated. Not used in NINvave 0.1.

No licence text was found on any of these pages (checked 2026-10-26).

## Pretty Hate Machine (NINvave 0.1)

| Song | Multitrack | Kit and patterns from |
| --- | --- | --- |
| Head Like a Hole | `Multitrack.HLAH.zip` (8 WAV stems, 115 BPM) | Kick, Snare, Cymbals & Toms, Extras, Bass Guitar, Keys; the LOOP from the Pretty Hate Pack's *Vocal & Percussive Loops* |
| Terrible Lie | `pretty.hate.pack.zip`, folder 3 | Kick, Snare, Drums Other, Other Synths & FX, Bass, Synths |
| Sanctified | `pretty.hate.pack.zip`, folder 1 | Kick, Drums Other (snare and hat), Percussion FX & Chants, Bass, Synths |
| The Only Time | `pretty.hate.pack.zip`, folder 2 | Kick, Snare, Drums Other, Other Synths & FX, Bass, Synths |

The Pretty Hate Pack's folders carry no song names. They were matched by length and tempo: folder 4 is Head Like a
Hole, with the same length and tempo as its own multitrack. Tempos are the bpm at which each song's bars repeat
most: Head Like a Hole 114.8, Terrible Lie 87.6, Sanctified 107.7, The Only Time 94.0.

The originals are kept on Hera: `/mnt/media2tb/Media/Music/NIN-Multitracks/`.
