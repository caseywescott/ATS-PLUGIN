# Farinelli voice synthesis

Historical reference for **dual-source vocal morphing** — the 1994 film *Farinelli* (*Farinelli il Castrato*, dir. Gérard Corbiau) recreated the lost castrato voice by merging two live singers, then refining the blend with studio analysis and editing.

## Source

- **Video:** [Making of Castrato — Farinelli il Castrato](https://www.youtube.com/watch?v=9BlZ4HagwRI) (TV documentary, ~1995–1996; ~50 min; French with Japanese/English subtitles)
- **Film:** *Farinelli* (1994) — lip-sync performance by Stefano Dionisi to a composite “third voice”

## Summary

The castrato voice that fascinated 18th-century Europe is gone: no recordings exist, and modern countertenors and sopranos each cover only part of the range. For the film, director Corbiau and music director Christophe Rousset chose a **reckless but deliberate** path: **record a male countertenor and a female coloratura soprano separately, then fuse them into one voice** spanning roughly **three and a half octaves**.

### The two source voices

| Role | Singer | Contribution |
| --- | --- | --- |
| Lower / mid register | **Derek Lee Ragin** (countertenor) | Sweet, flexible falsetto; “similar to the harpsichord”; supports bass |
| Upper register | **Eva Mala-Godlevska** (soprano coloratura) | Extreme high notes; wide dynamic range, lowered where possible to match Ragin |

Recording engineer and musicologist **Jean-Claude Gabriel** (Neuchâtel) led the experiment. Early tests (Amsterdam, April 1993) confirmed the concept; full orchestral sessions followed (e.g. Metz, August 1993, with Christophe Rousset’s Les Talens Lyriques on period instruments).

### Why one singer was not enough

- Baroque castrati kept boyhood vocal-fold size with adult lung capacity and thorax — an unmatched **range and power** combination.
- Countertenor fashion (post–baroque revival) and conventional soprano casting were each **insufficient** for the film’s historical ambition.
- The team accepted that **live staged fusion was impossible**; the composite had to be built in the studio.

### Synthesis pipeline (1993–1994)

1. **Dual recording** — Ragin and Mala-Godlevska tracked separately against the same repertoire (Handel, Porpora, Cavalli, Broschi, etc.).
2. **Register split** — Woman carries treble; man carries bass and lower material; overlap zones are where merging is hardest.
3. **Manual editing** — Gabriel made on the order of **~3,000 edits**, note-by-note, comparing bass through treble until the middle register (e.g. concert A) was indistinguishable as to which singer was sounding.
4. **Dynamic and timbral matching** — Her dynamics were reshaped toward his; vibrato, tempo, and expression were aligned in joint sessions.
5. **Voice analysis / morphing (IRCAM, Paris)** — **Philippe Depalle** (voice analysis/synthesis) used **kymography** (vocal-tract imaging during vowels) to analyze throat morphology, then software to **move the female singer’s tract shape toward the male singer’s** and **extract a new timbre per note**. Vowel onsets/offsets, strength, and pitch were classified and converted across sources so male↔female handoffs stayed inconspicuous.
6. **Seven months** of lab work produced a **third voice** that was neither singer alone — still built from **real recorded performances**, not a fully synthetic generator.

### Performance on screen

Actor **Stefano Dionisi** trained under Daniel Lipnik (**~3 months, ~10 hours/day**) in vocal technique, French, and gesture so he could **mime convincingly** to the composite. He describes the first time he heard “his” voice as overwhelming — sickness and exit from the room. The film intercuts performance with the brothers’ story (Riccardo Broschi’s compositions tailored to Carlo’s range).

### Relevance to ATS plugin work

| Farinelli (1994) | ATS / plugin morph track |
| --- | --- |
| Two fixed source performances | Source A / Source B analyses |
| Register-dependent who sings what | Band-wise or correspondence-based partial assignment |
| Manual note-level joins + tract morph | Morph lanes, envelope transfer, log-freq / Bark interpolation |
| Vowel / formant tract matching | Spectral envelope / formant-aware morph (partially planned) |
| Months of offline refinement | Immutable morph snapshots, offline recipe bake |
| Real voices only, no pure synth | Sinusoidal + residual resynthesis from analyzed audio |

The documentary’s central tension — **two voices resist becoming one until analysis and painstaking editing force a believable third identity** — is the same perceptual problem as spectral morphing: correspondence, envelope continuity, and register handoffs matter more than raw crossfade.

## Tags

`#reference` `#vocal-morph` `#historical` `#ats-plugin`
