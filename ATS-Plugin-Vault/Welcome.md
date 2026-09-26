# Welcome

This vault is the **working memory** for the **ATS Plugin** project: distilled PRDs, worktree layout, and reminders you need while designing and shipping the product.

## What we are making

We are building a **DAW plugin** (target: **VST3** on macOS and Windows) that brings **Analysis–Transformation–Synthesis (ATS)** into the host the way **Qatsh** does on the desktop today, but as an insert you can automate, recall in sessions, and distribute like any other professional plugin.

**ATS** models sound as **sinusoidal partial tracks** (amplitude, frequency, phase over time) plus **critical-band noise** (the residual after the sines are accounted for). The workflow is: get audio into a model (**analysis**), edit or morph the representation (**transformation**), then **resynthesize** through the host’s audio path. That is the core loop composers and sound designers used in the **ATSH / Qatsh** lineage; our job is to make it **host-native** (state, parameters, RT-safe synthesis) without pretending every legacy dialog must ship on day one.

The **DSP and analysis core** is anchored on **ATSA** (the C analysis/synthesis API used by Qatsh). The **longer-term engine story** (separate PRD) is a **modern ATSH-style** path: classic ATS for parity and regression tests, plus optional modes for better **tracking, transients, orphan residual bands, and correspondence-aware morphing**. A **spectral-envelope transfer** track (currently developed in the `spectral-envelope` worktree) adds **general** timbral hybridization—apply one sound’s broad spectral contour to another’s partial and residual detail—without assuming speech or a stable pitch.

**Non-goals for early releases** (unless you explicitly rescope): pixel-perfect Qatsh UI parity, guaranteed **live** low-latency analysis of arbitrary input, CLAP/AU/AAX as MVP, and neural training as the core product. **Licensing** (GPL-related stack around Qatsh/MathExpr/ATSA) remains a strategic constraint documented in the main PRD—engineering assumes compliance or a cleared alternative before a closed commercial ship.

## Where to go next

- **[[ATS Plugin - Developer Hub]]** — index of summaries, paths, and owner checkpoints
- **[[Feature_Matrix]]** — what is implemented in the plugin vs classic ATSH
