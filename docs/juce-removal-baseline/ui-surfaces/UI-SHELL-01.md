# UI-SHELL-01 — Editor page selector (AtsPageContainer)

Status: implemented

Replaces the `TabbedComponent` page bar in `PluginEditor.cpp` with an
`AtsPageContainer` instance holding the five page labels
`[SESSION | ANALYSIS | SYNTHESIS | PARTIALS | MATH]`.

## Ownership

`EditorShellModel` (src/ui/EditorShell.h) is the single source of truth
for the current page.  `AtsPageContainer` calls back into the shell model
via its `PageChange` callback; no widget stores secondary page state.

## Geometry (§10.3)

| Property | Value |
|---|---|
| Height | 30 px |
| Width | `min(640, bodyWidth)` |
| Font | `uiFont 10` |
| Tab count | 5 |
| Segment labels | SESSION / ANALYSIS / SYNTHESIS / PARTIALS / MATH |
| Below gap | 10 px |

The `AtsPageContainer::kTabHeight` constant (28.0f + 2 px fudge = 30 total)
matches this geometry.  Page switch is an immediate `setVisible` swap;
no animation.

## Interaction

`AtsPageContainer::handlePageClick(x)` maps the x position to a tab index
using `floor(x / tabWidth)`.  `selectPage(n)` hides all non-selected page
child frames and shows frame `n`.

The harness test `real_dispatch_harness_test` drives a page switch by
sending `NSEventTypeLeftMouseDown` at a computed x inside the tab bar row.
The `pageSelected` sink callback fires with the selected page index.

## Action protocol

No `EditorAction` is emitted; page selection is UI state only.  The
`EditorEventSink::pageSelected` callback allows the host test harness to
observe switches without coupling to action dispatch.

Evidence:

```text
(build-review-ui) ats_widget_dispatch_test — testPageContainer()
(build-review)    real_dispatch_harness_test — state.pageSelections + state.lastPage
```

Deviation: none.
