# ATS Plugin — Developer Hub

Central index for the **ATS / Qatsh-as-VST** project. Use this vault for orientation, distilled requirements, and where to open canonical docs in the repo.

## Quick orientation

| Topic | Summary note |
| --- | --- |
| Classic ATSH vs plugin implementation status | [[Feature_Matrix]] |
| Overall product (VST3, MVP, architecture, licensing risks) | [[Plugin (VST) - PRD Summary]] |
| Analysis / morph / synthesis engine (classic vs modern, data model, QA) | [[Modern ATSH Engine - PRD Summary]] |
| Spectral-envelope hybridization / vocoding track | [[Spectral Envelope - PRD Summary]] |
| MCP server — 25 tools, architecture, test suite, Claude Desktop config | [[MCP Server - Spec]] |
| Historical dual-source vocal morph (Farinelli film) | [[Farinelli voice synthesis]] |
| Which tree has what, file paths | [[Worktrees and canonical paths]] |

## Canonical repo root

`file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/`

## Owner reminders (from PRDs)

- **Audio thread:** synthesis only — no analysis, I/O, locks, allocations, or MathExpr on the RT path.
- **GPL stack:** Qatsh/MathExpr/ATSA licensing affects distribution strategy; treat commercial closed-source as blocked until legal path is explicit.
- **MVP platform:** VST3 on macOS + Windows; AU/CLAP/AAX are later-phase.
- **Live analysis:** not promised at low latency in early releases; scope capture/import vs ring-buffer explicitly.
- **spectral-envelope worktree:** adds **general** spectral-envelope transfer (not speech-default); see [[Spectral Envelope - PRD Summary]].

## Tags (optional)

`#ats-plugin` `#prd` `#worktree`
