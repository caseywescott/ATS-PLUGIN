# Worktrees and canonical paths

## Repo root (primary)

`/Users/caseywescott/Documents/Projects/ATS-PLUGIN/`

| Document | Path |
| --- | --- |
| Main plugin PRD | `PRD.md` |
| Modern engine PRD | `docs/MODERN_ATSH_PRD.md` |
| Commands / tasks (main) | `COMMANDS.md`, `TASKS.md` |

## Git worktree: `spectral-envelope`

`/Users/caseywescott/Documents/Projects/ATS-PLUGIN/.worktrees/spectral-envelope/`

Same **root PRD** and **MODERN_ATSH_PRD** as primary (content aligned when merged). **Additional** product spec:

| Document | Path |
| --- | --- |
| Spectral envelope hybridization PRD | `docs/SPECTRAL_ENVELOPE_PRD.md` |
| Tasks for that track | `docs/SPECTRAL_ENVELOPE_TASKS.md` |
| ADRs (effect-first, UI stack) | `docs/adr/` |

Plugin implementation files under this worktree include `src/plugin/`, `src/core/`, `tools/`, `tests/` — treat this tree as the **integration branch** for envelope work until merged to root.

## Vault ↔ disk

This vault lives **inside** the repo:

`/Users/caseywescott/Documents/Projects/ATS-PLUGIN/ATS-Plugin-Vault/`

Use `file://` links in notes or open paths in Finder / Cursor from the table above.

## Summaries in this vault

- [[ATS Plugin - Developer Hub]]  
- [[Plugin (VST) - PRD Summary]]  
- [[Modern ATSH Engine - PRD Summary]]  
- [[Spectral Envelope - PRD Summary]]  
