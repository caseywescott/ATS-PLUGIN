# SPEC: Algebraic Canon / Musical Tiling Generator for Cairo Generative Music Library

---

## Goal

Implement a constraint-first generative canon system in Cairo that produces rhythmically valid
musical tilings and maps them into existing modal transposition, quantization, motif, melody,
and media-rendering systems.

The system generates canons where rhythmic correctness is guaranteed by algebraic tiling:

```
X = R ⊕ S
```

Where:

* `X` is the full cyclic time space `Z_n`
* `R` is the rhythm tile — a set of onset positions within the tile
* `S` is the translation set — voice entry offsets
* `⊕` means direct sum: every time position in `Z_n` is covered exactly once

Pitch correctness is guaranteed by a separate constraint layer: the melodic intervals available
to the leading voice (proposta) are pre-constrained by the interval of imitation and the
harmonic field, so that all voice pairings are consonant by construction.

The output feeds into the existing Cairo music/media engine to render MIDI-like events, SVG,
tokenURI, or other onchain music representations.

---

## Architecture: Three Layers

```
Layer 1 — Construction:  seed → AP(k,d,n) selection → find_complement → (R, S)
                         (R, S) is guaranteed valid before any validator runs

Layer 2 — Assertion:     is_direct_sum(n, R, S) must pass
                         this is a correctness check, not a search predicate
                         failure here means a construction bug, not bad luck

Layer 3 — Harmony:       legato leading line h; vertical interval = lag-difference of h, minus I
                         consonance reduces to a precomputed constraint graph on motif notes
                         solved by propagation, with a consonant-clique closed-form fallback
```

The fundamental design principle: **the seed selects parameters within a structured construction
that guarantees validity**. The LCG never generates set membership directly. It selects indices
and offsets within a finite enumerable space of proven valid tilings.

```
seed → template index + rotation offset → AP construction → find_complement
     → assert → pitch constraint → emit events → render
```

---

## Why Not Unconstrained LCG Generation

A uniformly random subset R of size k from Z_n tiles Z_n with probability roughly
1/C(n−1, k−1) — vanishingly small for practical n and k. Brute-force search is infeasible
in Cairo.

Additionally, LCGs have known spectral structure: consecutive outputs tend to lie on
hyperplanes in Z_n. Raw LCG-generated sets cluster, producing rhythmically biased R candidates
that are both unlikely to tile and musically poor.

The role of the LCG in this system:

| Parameter               | LCG selects                                    |
|-------------------------|------------------------------------------------|
| `n`                     | index into the supported cycle lengths         |
| tiling template         | index into the precomputed table for chosen n  |
| `r_offset`              | rotation shift for R (value in Z_n)            |
| `s_offset`              | rotation shift for S (value in Z_n)            |
| `interval_of_imitation` | index into the supported pitch intervals       |
| harmonic field          | index into the harmonic field templates        |
| motif                   | index into the stored motif table              |

The LCG selects indices and offsets. It never generates set membership.

---

## Core Structures

### CyclicSpace

```rust
struct CyclicSpace {
    n: u32,
}
```

Supported values:

```
8, 12, 16, 24, 36, 48, 60, 72, 96, 120
```

### RhythmTile

```rust
struct RhythmTile {
    n: u32,
    onsets: Array<u32>,
}
```

### TranslationSet

Each offset in `S` is one voice entry point — the time at which that voice begins its
imitation of the proposta.

```rust
struct TranslationSet {
    n: u32,
    offsets: Array<u32>,
}
```

Cardinality bounds:

```rust
const MAX_VOICES: u32 = 8;      // upper bound on |S|
const MIN_TILE_ONSETS: u32 = 2; // lower bound on |R|; size 1 is trivial
```

Valid cardinality splits `(|R|, |S|)` for each supported n:

```
n=8:   (2,4), (4,2)
n=12:  (2,6), (3,4), (4,3), (6,2)
n=16:  (2,8), (4,4), (8,2)
n=24:  (2,12), (3,8), (4,6), (6,4), (8,3), (12,2)
n=36:  (2,18), (3,12), (4,9), (6,6), (9,4), (12,3), (18,2)
```

The seed selects a cardinality split before selecting a tiling template.

---

## Tiling Construction: AP Enumeration and Complement

### Key Principle

Valid tilings `R ⊕ S = Z_n` are produced by two steps:

1. **Generate R** as an arithmetic progression `AP(k, d, n)` for a chosen divisor pair `(k, d)`
2. **Derive S analytically** via the polynomial complement: `S = find_complement(n, R)`

Step 2 is always `find_complement`. S is never computed as a direct sum of other sets — it is
derived analytically from R.

### Arithmetic Progressions

```
AP(k, d, n) = {0, d, 2d, ..., (k-1)·d}  mod n
```

R is valid (tiles Z_n) iff `find_complement(n, R)` returns `Some`. Not all `(k, d)` pairs
produce valid tiles; validity must be confirmed by the complement function. For all supported
n values, valid `(k, d)` pairs are precomputed.

```rust
fn ap_set(k: u32, d: u32, n: u32) -> Option<Array<u32>> {
    let s = (0..k).map(|i| (i * d) % n).collect::<Set>();
    if s.len() == k { Some(sorted(s)) } else { None }  // None if AP self-intersects
}
```

### Precomputed Valid Tilings

For all supported n, valid `(k, d)` pairs are enumerated offline, their complements computed,
and the results stored as `TilingTemplate` entries in `known_tilings.cairo`. Below are the
complete tables for the primary n values, scored by rhythmic interest (higher = more
interesting; score=5 means neither R nor S is a pure AP):

**n = 8:**

```
score=5  k=2  d=2   R={0,2},             S={0,1,4,5}
score=4  k=2  d=1   R={0,1},             S={0,2,4,6}
score=4  k=2  d=4   R={0,4},             S={0,1,2,3}
score=4  k=4  d=1   R={0,1,2,3},         S={0,4}
score=4  k=4  d=2   R={0,2,4,6},         S={0,1}
```

**n = 12:**

```
score=5  k=2  d=2   R={0,2},             S={0,1,4,5,8,9}
score=5  k=2  d=3   R={0,3},             S={0,1,2,6,7,8}
score=5  k=3  d=2   R={0,2,4},           S={0,1,6,7}
score=4  k=2  d=1   R={0,1},             S={0,2,4,6,8,10}
score=4  k=2  d=6   R={0,6},             S={0,1,2,3,4,5}
score=4  k=3  d=4   R={0,4,8},           S={0,1,2,3}
score=4  k=4  d=1   R={0,1,2,3},         S={0,4,8}
score=4  k=4  d=3   R={0,3,6,9},         S={0,1,2}
score=4  k=6  d=1   R={0,1,2,3,4,5},     S={0,6}
score=4  k=6  d=2   R={0,2,4,6,8,10},    S={0,1}
```

**n = 16:**

```
score=5  k=2  d=2   R={0,2},             S={0,1,4,5,8,9,12,13}
score=5  k=2  d=4   R={0,4},             S={0,1,2,3,8,9,10,11}
score=5  k=4  d=2   R={0,2,4,6},         S={0,1,8,9}
score=4  k=2  d=1   R={0,1},             S={0,2,4,6,8,10,12,14}
score=4  k=2  d=8   R={0,8},             S={0,1,2,3,4,5,6,7}
score=4  k=4  d=1   R={0,1,2,3},         S={0,4,8,12}
score=4  k=4  d=4   R={0,4,8,12},        S={0,1,2,3}
score=4  k=8  d=1   R={0,1,2,3,4,5,6,7}, S={0,8}
score=4  k=8  d=2   R={0,2,4,6,8,10,12,14}, S={0,1}
```

**n = 24:**

```
score=5  k=2  d=2   R={0,2},     S={0,1,4,5,8,9,12,13,16,17,20,21}
score=5  k=2  d=3   R={0,3},     S={0,1,2,6,7,8,12,13,14,18,19,20}
score=5  k=2  d=4   R={0,4},     S={0,1,2,3,8,9,10,11,16,17,18,19}
score=5  k=2  d=6   R={0,6},     S={0,1,2,3,4,5,12,13,14,15,16,17}
score=5  k=3  d=2   R={0,2,4},   S={0,1,6,7,12,13,18,19}
score=5  k=3  d=4   R={0,4,8},   S={0,1,2,3,12,13,14,15}
score=5  k=4  d=2   R={0,2,4,6}, S={0,1,8,9,16,17}
score=5  k=4  d=3   R={0,3,6,9}, S={0,1,2,12,13,14}
score=5  k=6  d=2   R={0,2,4,6,8,10}, S={0,1,12,13}
... (further entries in known_tilings.cairo)
```

### Why Some (k, d) Pairs Work and Others Don't

The underlying reason is the cyclotomic polynomial factorization: a tiling `R ⊕ S = Z_n`
corresponds to a partition of the factors of `T_n(x) = 1+x+...+x^(n-1)` such that
`R(x)·S(x) = T_n(x)`. Not all APs yield an R(x) that divides T_n(x). For example:

```
n=12:  AP(2,4,12) = {0,4} does NOT tile Z_12  (R(x)=1+x^4 does not divide T_12(x))
n=12:  AP(2,2,12) = {0,2} DOES tile Z_12      (R(x)=1+x^2 = Φ_4(x) divides T_12(x))
```

The `find_complement` function is the definitive test. For the precomputed table it has already
been applied offline; at runtime the generator only indexes into the table.

### Rotation Offsets

After template selection, both R and S are shifted by seed-derived offsets:

```rust
fn apply_offset(set: Array<u32>, offset: u32, n: u32) -> Array<u32> {
    set.map(|x| (x + offset) % n)
}
```

Different offsets produce metrically shifted versions of the same tiling. The offset is the
primary source of per-seed variation within a given tiling class.

### TilingTemplate

```rust
struct TilingTemplate {
    n: u32,
    k: u32,           // |R|
    d: u32,           // AP step used to generate R
    r_base: Array<u32>,   // precomputed R = AP(k, d, n)
    s_base: Array<u32>,   // precomputed S = find_complement(n, r_base)
    default_interval_of_imitation: i32,
    default_distance_of_entrance: u32,
    syncopation_class: u8,  // 0=trivial-block, 1=one-side-AP, 2=neither-AP
}
```

`s_base` is always the output of `find_complement(n, r_base)`. It is never computed as a
direct sum of arithmetic progressions.

All entries in `known_tilings.cairo` must pass `is_direct_sum` in tests.

---

## Direct Sum Validator

`is_direct_sum` is a **correctness assertion**, not a search predicate. For tilings from the
precomputed table it must always pass. Failure means a bug in the template data.

```rust
fn is_direct_sum(n: u32, r: Array<u32>, s: Array<u32>) -> bool {
    if r.len() * s.len() != n {
        return false;
    }

    let mut hits = array![0; n];

    for ri in r {
        if ri >= n { return false; }
        for sj in s {
            if sj >= n { return false; }
            let pos = (ri + sj) % n;
            if hits[pos] == 1 { return false; }
            hits[pos] = 1;
        }
    }

    for i in 0..n {
        if hits[i] != 1 { return false; }
    }

    true
}
```

Use whichever existing pattern the project uses for fixed-size memory, span iteration, or
bounded arrays — Cairo arrays are not dynamically initialized the same way as JS/Python.

---

## Polynomial Complement

`find_complement` is the function that derives S analytically from R. It is used offline to
populate `known_tilings.cairo` and at runtime to handle user-supplied or Vuza-fragment R sets.

The tiling condition `R(x)·S(x) = T_n(x)` means `S(x) = T_n(x)/R(x)` over Z[x] where
`T_n(x) = 1+x+...+x^(n-1)`.

```rust
fn find_complement(n: u32, r: Array<u32>) -> Option<Array<u32>> {
    // 1. Build r_poly: coefficient array of length n, r_poly[i]=1 if i∈R else 0
    // 2. Build t_poly: all-ones array of length n
    // 3. Polynomial long division over Z mod (x^n - 1): t_poly / r_poly
    // 4. If remainder is zero AND all quotient coefficients are 0 or 1:
    //      return Some(positions where quotient coefficient == 1)
    // 5. Otherwise return None  (R does not tile Z_n)
}
```

Complexity O(n · |R|), feasible for all supported n. If the remainder is nonzero or any
coefficient is not 0 or 1, R is not a valid tile.

**Relationship to the precomputed table:** for every `r_base` in `known_tilings.cairo`,
`find_complement(n, r_base)` returns `Some(s_base)` — the table was built this way. The
function is present at runtime for:
- User-supplied R
- Vuza template fragments
- Any R arriving from outside the precomputed table

---

## Augmentation and Diminution in the Tiling Algebra

Augmentation (doubled time values) and diminution (halved time values) affect the tiling
guarantee. If a voice uses augmentation, its effective rhythm tile is
`scaled_R = {(r * factor) mod n : r ∈ R}`. This is not guaranteed to tile Z_n with the
original S. For example:

```
R = {0,2},  S = {0,1,4,5,8,9},  n=12   → valid
2R = {0,4}
{0,4} ⊕ {0,1,4,5,8,9}  → collision at 4; does not tile
```

**Design rule:** voices with `transform = augmentation` or `transform = diminution` are
**free voices** — excluded from the core tiling guarantee, validated independently.

```rust
struct VoiceSpec {
    voice_id: u32,
    translation: u32,
    mode: u8,
    root: u8,
    octave: i32,
    transposition: i32,
    transform: u8,
    tiling_participant: bool,  // false for augmented/diminuted voices
}
```

`is_direct_sum` operates only over voices where `tiling_participant = true`. Free voices may
overlap with tiling voices; they are governed only by the pitch constraint layer and the
polyphony limit.

To attempt tiling inclusion of a scaled voice:

```rust
fn validate_scaled_tile(n: u32, r: Array<u32>, s: Array<u32>, factor: u32) -> bool {
    let scaled_r = r.map(|x| (x * factor) % n);
    is_direct_sum(n, scaled_r, s)
}
```

If this passes, the voice can be promoted to `tiling_participant = true`.

---

## Canon Representation

### Canon Struct

```rust
struct Canon {
    n: u32,
    rhythm_tile: Array<u32>,          // R
    translations: Array<u32>,         // S
    interval_of_imitation: i32,       // semitones, signed (positive = risposta above)
    distance_of_entrance: u32,        // derived: min(s for s in S where s > 0)
    template_index: u32,              // index into TilingTemplate table for n
    r_offset: u32,                    // rotation applied to r_base
    s_offset: u32,                    // rotation applied to s_base
    seed: felt252,                    // drives deterministic propagation candidate order
    voices: Array<VoiceSpec>,
}
```

`distance_of_entrance` is derived, not set independently:

```rust
fn derive_distance_of_entrance(s: Array<u32>) -> u32 {
    s.iter().filter(|x| *x > 0).min()
}
```

Misalignment between `distance_of_entrance` and the actual S will cause the
constraint-graph lags to be computed against the wrong entry delay.

Supported `interval_of_imitation` values:

```
 0  = canon at the unison
 5  = canon at the fourth above
 7  = canon at the fifth above
12  = canon at the octave above
-5  = canon at the fourth below
-7  = canon at the fifth below
```

### VoiceSpec

```rust
struct VoiceSpec {
    voice_id: u32,
    translation: u32,
    mode: u8,
    root: u8,
    octave: i32,
    transposition: i32,
    transform: u8,
    tiling_participant: bool,
}
```

Transform values:

```
0 = normal
1 = retrograde            // risposta reverses the motif order
2 = inversion             // risposta mirrors melodic contour
3 = retrograde_inversion
4 = augmentation          // doubled note values; tiling_participant typically false
5 = diminution            // halved note values; tiling_participant typically false
```

---

## Layer 3: Harmonic Construction

This layer assigns pitches so that the canon is consonant at every instant. Unlike a homophonic
texture, a tiling canon has **no simultaneous attacks** — by the direct-sum property, every time
position belongs to exactly one voice. All vertical harmony therefore comes from *sustained*
notes overlapping. The harmonic model is built on that fact.

### The Legato Duration Model

Each voice is a continuous melodic line: every note sustains until that voice's next onset.
This makes durations fully determined (no free `derive_duration` parameter) and guarantees that
at every instant each voice is sounding exactly one pitch.

```rust
// Duration of motif note i in any voice = gap to that voice's next onset.
// Because every voice plays the same tile R, the duration pattern is shared.
fn legato_durations(r: Array<u32>, n: u32) -> Array<u32> {
    let k = r.len();
    let mut durs = ArrayTrait::new();
    for i in 0..k {
        let start = r[i];
        let end = r[(i + 1) % k];
        durs.append((end + n - start) % n);   // wraps around the cycle
    }
    durs   // durs[i] is the sustain length of motif note i
}
```

The leading line as a function of integer time is the piecewise-constant `h`:

```rust
// h_idx[t] = index of the motif note sounding in the leading voice at time t (legato).
fn build_leading_index(r: Array<u32>, n: u32) -> Array<u32> {
    let k = r.len();
    let mut h = array_of_len(n);          // h[t] : motif-note index
    for i in 0..k {
        let start = r[i];
        let end = r[(i + 1) % k];
        let mut t = start;
        loop {
            h[t] = i;
            t = (t + 1) % n;
            if t == end { break; }
        }
    }
    h
}
```

A voice entered at offset `s` and transposed by interval `I` sounds pitch
`P[h_idx[(t − s) mod n]] + I` at time `t`, where `P` is the motif's pitch sequence.

### The Lag-Difference Identity

The vertical interval between any two voices follows a single deterministic identity. For voices
at offsets `a` and `b` (lag `δ = b − a`) with interval-of-imitation difference `I`:

```
vertical interval at time t  =  h(t − a) − h(t − b) − I
                             =  [ h(τ) − h(τ − δ) ] − I        (τ = t − a)
```

The harmony is entirely governed by **how the single melody moves across the lag `δ`** — the
historical "dux pattern" (Cumming): the intervals between dux notes separated by the canon's
time interval. There is no need to compare voices independently; the leading line's lag-`δ`
difference determines every vertical sonority.

Harmonic correctness is therefore one condition, checked over all lags in the canon:

```
for every lag δ in LagSet(S), and every time t:
    ( h(t) − h(t − δ) − I )  mod 12  ∈  ConsonantSet
```

where `LagSet(S) = { (s_i − s_j) mod n : s_i, s_j ∈ S, i ≠ j }`.

### Working in Scale-Degree Space

The construction operates in **diatonic scale-degree space**, not chromatic semitones. The motif
`P` is a sequence of scale degrees, `ConsonantSet` is defined as diatonic consonances, and the
harmonic field shifts the degree origin per region. Because the melody is born on scale degrees,
there is no post-hoc `quantize_to_mode` step that could reintroduce dissonance — quantization and
consonance live in the same space.

Voicing-independent consonant set (pitch classes, safe under voice crossing — thirds, sixths,
octave; deliberately excluding the perfect fourth/fifth pair whose quality flips with voicing):

```rust
const CONS: [u8; 5] = [0, 3, 4, 8, 9];   // octave/unison, m3, M3, m6, M6
// symmetric: 3<->9, 4<->8, 0<->0  (needed for closure / fallback guarantees)
```

A project preferring to allow fifths can use `{0, 3, 4, 7, 8, 9}` and add a voice-crossing
guard, but the 5-element set above requires no such guard.

### The Constraint Graph

Because the leading line is legato (piecewise-constant), the only free variables are the motif
pitches `P[0..k−1]`. The lag condition collapses into a fixed set of edges between motif-note
indices — precomputable from `R`, `S`, and the durations, before any pitch is chosen.

```rust
struct ConstraintGraph {
    k: u32,                          // number of motif notes
    edges: Array<(u32, u32, i32)>,   // (now_index, delayed_index, I) : P[now] - (P[del]+I) ∈ CONS
}

fn build_constraint_graph(
    r: Array<u32>, s: Array<u32>, n: u32, interval_of_imitation: i32,
) -> ConstraintGraph {
    let h = build_leading_index(r, n);
    let mut edges = Set::new();
    for delta in lag_set(s, n) {
        for t in 0..n {
            let now = h[t];
            let del = h[(t + n - delta) % n];
            if now != del {
                edges.insert((now, del, interval_of_imitation));
            }
        }
    }
    ConstraintGraph { k: r.len(), edges: edges.to_array() }
}
```

Each edge `(now, del, I)` requires `(P[now] − P[del] − I) mod 12 ∈ CONS`.

### Two-Regime Pitch Assignment

**Regime A — consonant-clique reservoir (closed form, always valid).** Draw every motif note
from a consonant clique: a pitch-class set whose every pair is consonant in any voicing. The
augmented triad `{0, 4, 8}` qualifies (all pairs differ by M3 or m6). Any assignment of clique
members to motif notes satisfies *every* edge automatically — no propagation, guaranteed valid,
for any rhythm, any lag, any voice count. Harmonically static (one sonority class), but it never
fails. This is the fallback.

```rust
fn assign_clique(k: u32, contour: Array<u32>) -> Array<u8> {
    let clique = [0_u8, 4, 8];
    // map the motif contour onto clique members; any mapping is consonant
    (0..k).map(|i| clique[contour[i] % 3]).collect()
}
```

**Regime B — constraint propagation (richer melody, may dead-end).** Assign motif notes one at a
time in a fixed order; for each, pick the first scale degree (in a seed-determined candidate
order, biased toward following the motif contour) that is consonant with all already-assigned
neighbors in the constraint graph.

```rust
fn assign_propagate(
    graph: ConstraintGraph,
    motif_contour: Array<i32>,
    seed: felt252,
) -> Option<Array<u8>> {
    let mut p = array_of_none(graph.k);
    for node in 0..graph.k {
        let candidates = candidate_order(node, motif_contour, seed);  // contour-biased, deterministic
        let mut chosen = None;
        for cand in candidates {
            if edges_satisfied(node, cand, p, graph) {
                chosen = Some(cand);
                break;
            }
        }
        match chosen {
            Some(c) => p[node] = Some(c),
            None => return None,   // dead-end
        }
    }
    Some(unwrap_all(p))
}
```

Propagation is deterministic from the seed but can dead-end (odd cycles, high-degree nodes). On
`None`, fall back to Regime A. The combined procedure always returns a valid melody:

```rust
fn assign_pitches(graph: ConstraintGraph, motif: Motif, seed: felt252) -> Array<u8> {
    match assign_propagate(graph, motif.contour, seed) {
        Some(p) => p,
        None    => assign_clique(graph.k, motif.contour),   // provably valid fallback
    }
}
```

This is the honest meaning of "path 2": construct via propagation, fall back to a provably valid
clique on dead-end. Bounded, deterministic, never fails to produce consonant output.

### Harmonic Field

```rust
struct HarmonicField {
    n: u32,
    degrees: Array<i32>,   // scale-degree offset applied per time region
}
```

Example:

```
n = 12
harmonic_field = [0,0,0,3,3,3,4,4,4,5,5,5]
```

The harmonic field shifts the scale-degree origin across the cycle, letting the canon move
through harmonic regions while staying rhythmically invariant. It is applied to `P` *before*
the constraint graph is built, so consonance is checked against the shifted degrees. It is the
modern analog of the historical cantus firmus constraint, but it does not require a separate
consonance pass: once folded into `P`, the lag-difference condition covers it.

### Final Validator (assertion, not search)

After assignment, assert consonance directly by simulating the sounding pitches. For correctly
constructed melodies (either regime) this always passes; it guards against implementation bugs.

```rust
fn assert_harmony(
    p: Array<u8>, r: Array<u32>, s: Array<u32>, n: u32, interval_of_imitation: i32,
) -> bool {
    let h = build_leading_index(r, n);
    for (a, b) in voice_pairs(s) {
        for t in 0..n {
            let v_now = p[h[(t + n - a) % n]];
            let v_del = (p[h[(t + n - b) % n]] + interval_of_imitation) % 12;
            if (v_now + 12 - v_del) % 12 not in CONS { return false; }
        }
    }
    true
}
```

This scales to any number of voices: it checks every voice pair over the whole cycle, not just a
fixed three-voice (P+R / P+CF / R+CF) decomposition. The earlier three-combination framing was a
special case for two voices plus a cantus firmus; `assert_harmony` is the general statement.

---

## Event Generation

Pitches are assigned once per motif (the canon shares one melody `P`); each voice reuses `P`
with its transform and interval-of-imitation applied. Durations come from the legato model.

```rust
fn canon_to_events(canon: Canon, motif: Motif, harmonic_field: HarmonicField) -> Array<NoteEvent> {
    // 1. Fold harmonic field into the motif degrees, in scale-degree space
    let p_degrees = apply_harmonic_field(motif, harmonic_field, canon);
    // 2. Build the constraint graph from rhythm + voice offsets + interval of imitation
    let graph = build_constraint_graph(canon.rhythm_tile, canon.translations,
                                       canon.n, canon.interval_of_imitation);
    // 3. Assign consonant pitches (Regime B with Regime A fallback) — once, shared by all voices
    let P = assign_pitches(graph, p_degrees, canon.seed);
    // 4. Assert harmony (must pass by construction)
    assert(assert_harmony(P, canon.rhythm_tile, canon.translations,
                          canon.n, canon.interval_of_imitation));
    // 5. Compute legato durations once
    let durs = legato_durations(canon.rhythm_tile, canon.n);

    let mut events = ArrayTrait::new();
    for voice_index in 0..canon.translations.len() {
        let s = canon.translations[voice_index];
        let voice = canon.voices[voice_index];
        for i in 0..canon.rhythm_tile.len() {
            let r = canon.rhythm_tile[i];
            let time = (r + s) % canon.n;
            let degree = transform_degree(P, i, voice);      // retrograde/inversion reindex
            let pitch  = realize_pitch(degree, voice);       // + IoI, transposition, octave, MIDI clamp
            events.append(NoteEvent {
                time,
                duration: durs[transform_index(i, voice)],
                pitch,
                velocity: derive_velocity(motif, i, voice),
                voice_id: voice.voice_id,
            });
        }
    }
    sort_events_by_time(events)
}
```

`P` is computed **once** and shared, which is what makes the texture a genuine canon and what
makes the lag-difference identity apply. Per-voice variation is limited to transform (retrograde,
inversion), interval of imitation, transposition, octave, and velocity.

---

## Pitch Realization

```rust
fn realize_pitch(degree: i32, voice: VoiceSpec) -> u8 {
    // degree is already a consonant scale degree (from assign_pitches, in scale-degree space)
    // 1. raw = degree + voice.transposition         (still scale degrees)
    // 2. semitone = degree_to_semitone(raw, voice.mode, voice.root)   // no re-quantization:
    //               degrees map directly to scale tones, preserving the consonance
    // 3. semitone += voice.interval_of_imitation_semitones + 12 * voice.octave
    // 4. clamp to MIDI range 0..=127
    // 5. return semitone as u8
}

fn transform_degree(P: Array<u8>, i: u32, voice: VoiceSpec) -> i32 {
    // normal:     P[i]
    // retrograde: P[k-1-i]
    // inversion:  mirror P[i] about a pivot degree
    // ret_inv:    mirror P[k-1-i]
}
```

There is no `quantize_to_mode` clamp that could break consonance: `assign_pitches` already
produced consonant scale degrees, and `realize_pitch` maps degrees to tones without re-snapping.

**Transforms and the constraint graph.** Inversion negates melodic intervals and retrograde
reverses note order, both of which change the lag-difference structure. The constraint graph
must therefore be built against the *transformed* leading line when a transform is in use, or —
simpler and recommended for the MVP — transforms are disabled for voices that participate in the
harmonic constraint, and only applied to free (augmented/diminuted) voices. Full transform
support under the constraint graph is a documented extension.

---

## Generator Pipeline

Seed bit layout:

```
bits [0..3]   → index into supported n values
bits [4..7]   → index into cardinality split table for chosen n
bits [8..11]  → index into TilingTemplate table for chosen (n, cardinality)
bits [12..19] → r_offset (rotation shift for R, mod n)
bits [20..27] → s_offset (rotation shift for S, mod n)
bits [28..31] → index into supported interval_of_imitation values
bits [32..35] → index into harmonic field templates
bits [36..]   → motif selection, voice params, pitch propagation candidate order
```

Full pipeline:

```
seed
→ select n
→ select cardinality split (k, n/k) for n
→ select TilingTemplate for (n, k)          [from precomputed table]
→ apply r_offset to template.r_base → R
→ apply s_offset to template.s_base → S
→ assert is_direct_sum(n, R, S)             [Layer 2: rhythm correctness — must always pass]
→ derive distance_of_entrance from S
→ select interval_of_imitation
→ select HarmonicField
→ select motif (contour in scale-degree space)
→ assign VoiceSpecs
→ build_constraint_graph(R, S, n, interval_of_imitation)
→ assign_pitches: propagate (Regime B) → on dead-end fall back to clique (Regime A)
→ assert_harmony(...)                       [Layer 3: harmony — must always pass]
→ realize pitches (degrees → MIDI, clamped)
→ emit NoteEvents (legato durations)
→ sort by time
→ render
```

```rust
fn generate_canon(seed: felt252) -> Canon;
```

Fully deterministic. The only place that can "fail" is Regime B propagation, and that failure is
caught by the Regime A fallback — so the pipeline always produces consonant output without
unbounded retry. There is no search at any stage.

---

## Tiling Classification

### Trivial vs Non-Trivial

A **trivial block pattern** is a tiling where both R and S are pure arithmetic progressions
(equal-gap cyclic sequences). These produce metrically square, unsyncopated output.

```rust
fn is_arithmetic_progression(set: Array<u32>, n: u32) -> bool {
    let gaps = cyclic_gaps(n, set);
    gaps.windows(2).all(|w| w[0] == w[1])
}

fn is_trivial_block_pattern(n: u32, r: Array<u32>, s: Array<u32>) -> bool {
    is_arithmetic_progression(r, n) && is_arithmetic_progression(s, n)
}
```

From the tables above: `syncopation_class = 2` (score=5) means neither R nor S is a pure AP.
These are the most rhythmically interesting entries. In Z_12, examples include:

```
R={0,2}, S={0,1,4,5,8,9}   — R is AP, S is three copies of {0,1} shifted by 4
R={0,3}, S={0,1,2,6,7,8}   — R is AP, S is two copies of {0,1,2} shifted by 6
R={0,2,4}, S={0,1,6,7}     — R is AP, S is two copies of {0,1} shifted by 6
```

The "richness" of the output is primarily in the structure of S, not R.

### Scoring

```rust
fn score_tiling(n: u32, r: Array<u32>, s: Array<u32>) -> u32;
```

Factors:

* **penalize** trivial block patterns (both R and S are pure APs)
* **reward** S containing non-AP subsets (nested or irregular structure)
* **reward** varied gap sizes in R and S
* **reward** syncopation: onsets landing on off-beat positions
* **reward** moderate density (neither very sparse nor very dense)
* **penalize** adjacent attacks (consecutive positions in R)
* **reward** `distance_of_entrance` not equal to simple divisors of n

---

## Vuza / Nonperiodic Canons

Vuza canons are tilings where neither R nor S is a union of arithmetic progressions. They require
n values whose prime factorization satisfies conditions that prevent either factor from being
forced into a periodic structure (non-Hajós numbers).

**Critical constraint:** none of the currently supported n values (8, 12, 16, 24, 36, 48, 60,
72, 96, 120) support Vuza canons. All tilings constructible from these n values are Hajós —
one or both of R and S will always be a union of APs. Vuza tilings require considerably larger
n satisfying the specific non-Hajós condition, and those n values must be explicitly identified
and added to the supported list before Vuza templates can be used.

**Vuza tilings are always hardcoded. They are never generated by the AP construction pipeline.**

```rust
struct VuzaTemplate {
    n: u32,
    r: Array<u32>,
    s: Array<u32>,
    default_interval_of_imitation: i32,
    default_distance_of_entrance: u32,
}
```

Store Vuza templates in `known_tilings.cairo` alongside the AP tiling templates. All entries
must pass `is_direct_sum` in tests. `is_candidate_prime_canon` is stubbed now and activated
when Vuza-capable n values are added.

---

## Gap Analysis Helpers

```rust
fn cyclic_gaps(n: u32, positions: Array<u32>) -> Array<u32>;
```

Example:

```
positions = {0, 2, 5},  n = 8
gaps = {2, 3, 3}
```

Used for:
- detecting trivial block patterns
- scoring rhythmic interest
- deriving `distance_of_entrance` as the minimum non-zero gap in S

---

## Classification Helpers

```rust
fn is_periodic(n: u32, set: Array<u32>) -> bool;
fn is_arithmetic_progression(set: Array<u32>, n: u32) -> bool;
fn is_trivial_block_pattern(n: u32, r: Array<u32>, s: Array<u32>) -> bool;
fn is_candidate_prime_canon(n: u32, r: Array<u32>, s: Array<u32>) -> bool;
```

`is_candidate_prime_canon` returns true when both R and S are non-periodic and neither is a
union of APs. For all currently supported n values this always returns false. Stubbed now;
activated when Vuza-capable n values are added.

---

## Multiple Canons

```rust
struct MultipleCanon {
    n: u32,
    canons: Array<Canon>,
}
```

Each internal canon must be valid independently. Canons within a `MultipleCanon` may use
different `interval_of_imitation` values, different tiling templates, and different offsets —
enabling stacked canons at multiple imitation intervals over the same timeline.

Optional stricter rule: the union of all generated events obeys max polyphony constraints.

```rust
fn validate_polyphony(events: Array<NoteEvent>, max_polyphony: u32) -> bool;
```

---

## Musical Output

```rust
struct NoteEvent {
    time: u32,
    duration: u32,
    pitch: u8,
    velocity: u8,
    voice_id: u32,
}
```

Events sorted by time before rendering.

---

## MVP Strategy

### MVP 1: Hardcoded Templates

Populate `known_tilings.cairo` with the precomputed `TilingTemplate` entries from the tables
above for n = 8, 12, 16, 24. Include at least one `syncopation_class=2` entry per n.

Each entry carries: `n`, `k`, `d`, `r_base`, `s_base`, `default_interval_of_imitation`,
`default_distance_of_entrance`, `syncopation_class`.

All entries must pass `is_direct_sum` in tests.

### MVP 2: Validator + Complement

Implement `is_direct_sum`. Implement `find_complement`. Add tests (see Testing Requirements).

### MVP 3: Seeded Selection + Offsets

Implement `generate_canon(seed)` using the seed bit layout above.

```rust
fn select_template(seed: felt252, n: u32) -> TilingTemplate;
fn apply_offset(set: Array<u32>, offset: u32, n: u32) -> Array<u32>;
```

Implement `build_constraint_graph`, `assign_clique` (Regime A), and `assert_harmony`.

### MVP 4: AP Enumeration Builder (offline tool)

Implement the offline enumerator used to build `known_tilings.cairo`. This runs outside Cairo
(Rust or Python) and produces the tables embedded in the Cairo file.

```rust
fn enumerate_valid_ap_tilings(n: u32) -> Array<TilingTemplate>;
// For each divisor k of n (2 ≤ k ≤ n/2):
//   For each divisor d of n:
//     R = ap_set(k, d, n)?
//     S = find_complement(n, R)?
//     If both Some: record TilingTemplate, compute score
// Return sorted by score descending
```

At runtime in Cairo, the generator only indexes into the precomputed table.

### MVP 5: Musical Scoring and Ranking

Implement `score_tiling`. Rank templates within each `(n, k)` group so the seed selects
higher-scoring tilings by default via a weighted index.

---

## Testing Requirements

### Direct Sum

```
R={0,1}, S={0,2,4,6,8,10}, n=12  => true
R={0,3}, S={0,1,2,4}, n=8        => false
R={0,2}, S={0,1,4,5}, n=8        => true
```

### AP Construction

```
ap_set(2, 2, 12) == Some({0,2})
ap_set(2, 4, 12) == None    (AP self-intersects: {0,4,8} has 3 elements, not 2)
ap_set(3, 4, 12) == Some({0,4,8})
apply_offset({0,2,4,6,8,10}, 1, 12) == {1,3,5,7,9,11}
```

### Polynomial Complement

```
find_complement(12, {0,2})    == Some({0,1,4,5,8,9})
find_complement(12, {0,6})    == Some({0,1,2,3,4,5})
find_complement(12, {0,4})    == None   (does not tile Z_12)
find_complement(8,  {0,2})    == Some({0,1,4,5})
for all r_base in known_tilings: find_complement(n, r_base) == Some(s_base)
```

### Augmentation

```
validate_scaled_tile(12, {0,2}, {0,1,4,5,8,9}, 2) == false  (2R={0,4} collides)
free voices (tiling_participant=false) excluded from is_direct_sum check
```

### Event Generation

```
number_of_events == n  (for tiling_participant voices)
every time position covered exactly once
events deterministic for same seed
different seeds produce different outputs
```

### Pitch Generation

```
all pitches inside selected mode
voice transpositions work
octave offsets work
retrograde reverses motif order
inversion mirrors motif contour
legato_durations sum to n (durations tile the cycle per voice)
build_constraint_graph is deterministic for fixed (R, S, n, I)
assign_clique output passes assert_harmony for ALL templates (Regime A always valid)
assign_propagate output, when not None, passes assert_harmony
assign_pitches never returns a melody that fails assert_harmony (fallback guarantee)
a known dead-end case triggers the clique fallback and still passes assert_harmony
working in scale-degree space: realized pitches stay within the selected mode
```

### Vuza

```
all VuzaTemplate entries pass is_direct_sum
is_candidate_prime_canon returns false for all Hajós n tilings
```

---

## Known Tilings File (known_tilings.cairo)

Stores:

1. `TilingTemplate` entries — one per (n, k, d) combination, with precomputed `r_base`,
   `s_base`, `syncopation_class`
2. `VuzaTemplate` entries — hardcoded only, never constructed
3. consonant-set constant `CONS` and any precomputed constraint graphs for templated tilings
4. Harmonic field templates indexed by seed bits

---

## Deliverables

1. `canon_tiling.cairo` — core structs, AP construction, validator, complement, event generator
2. `canon_tiling_tests.cairo` — all tests above
3. `known_tilings.cairo` — precomputed templates, `CONS`, harmonic field templates
4. offline enumerator (Rust or Python) that generates `known_tilings.cairo` content
5. integration with existing modal quantization library
6. integration with existing media renderer
7. `generate_canon(seed: felt252) -> Canon` — deterministic, no search
8. tests proving Layer 2 (assertion) always passes for templated tilings
9. tests proving Layer 3 (pitch) always passes for generated events

---

## Onchain Token Traits

```
cycle_length              n
tile_density              |R| / n
number_of_voices          |S|
tiling_template_index     index into the template table
syncopation_class         0=trivial-block / 1=one-side-AP / 2=neither-AP
interval_of_imitation     semitones, signed
distance_of_entrance      Z_n steps
canon_type                normal / retrograde / inversion / augmentation / diminution
mode
root
periodicity_class         hajos / vuza
harmonic_complexity
```

---

## Future Extensions

### T_p Scaling / Hierarchical Composition

Build large-n tilings from smaller verified ones via the scaling property: if R⊕S=Z_m is
valid, then scaled versions tile Z_{p·m}.

```rust
fn scale_tiling(r: Array<u32>, s: Array<u32>, factor: u32, new_n: u32)
    -> (Array<u32>, Array<u32>);

fn compose_tilings(
    r1: Array<u32>, s1: Array<u32>, n1: u32,
    r2: Array<u32>, s2: Array<u32>, n2: u32,
) -> (Array<u32>, Array<u32>, u32);  // combined tiling in Z_{n1*n2} (when coprime)
```

This enables efficient extension to larger n without re-running the full AP enumeration.

### Vuza Support

Identify n values satisfying the non-Hajós condition. Add their tilings as hardcoded
VuzaTemplates. Activate `is_candidate_prime_canon`. Expose `periodicity_class` trait.

### Chord Tiling

Extend R from rhythmic onsets to chord objects:

```rust
struct ChordTile {
    onset: u32,
    pitch_classes: Array<u8>,
}
```

### Voice-Leading Constraints

Post-pass validator extending the pitch constraint layer:

```
max leap between consecutive dux notes
avoid parallel fifths/octaves between P+R
prefer stepwise motion in dux
enforce register separation between voices
common-tone retention between adjacent harmonic field segments
```

---

## Success Criteria

A generated canon is successful if:

1. `is_direct_sum(n, R, S) == true` — always the case for templated tilings
2. generated note events cover the cyclic timeline exactly once (tiling-participant voices)
3. `assert_harmony` returns true: every voice pair is consonant at every instant over the cycle
4. pitch assignment terminated via Regime B or the Regime A clique fallback (never unbounded)
5. all pitches are quantized to the selected mode
6. output is deterministic from seed
7. media renderer can consume the generated events
8. no search is performed at generation time; all steps are bounded and predictable

The system is a mathematically constrained generative music engine where:
- rhythm is *guaranteed* by AP construction + polynomial complement (proven correct by construction)
- harmony is *guaranteed consonant* by the lag-difference constraint graph, with a consonant-clique
  closed-form fallback that is always valid — so output is always consonant, though a richer
  (Regime B) melody is best-effort and may fall back to the static (Regime A) one
- the LCG selects; the algebra constructs; the validators assert

Honest scope statement: the engine guarantees a *valid consonant canon* on every seed. It does
not guarantee that the consonant melody is the *most musically interesting* one available for a
given tiling — that is the quality gradient between Regime B (rich, may dead-end) and Regime A
(static, always valid). Improving the dead-end rate of Regime B is a quality optimization, not a
correctness requirement.
