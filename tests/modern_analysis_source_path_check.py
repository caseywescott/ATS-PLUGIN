#!/usr/bin/env python3
"""Guard the Modern ATS plugin route against classic-derived model regressions."""

from __future__ import annotations

import pathlib
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"error: {message}", file=sys.stderr)
        raise SystemExit(1)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: modern_analysis_source_path_check.py <repo-root>", file=sys.stderr)
        return 2

    root = pathlib.Path(sys.argv[1])
    plugin_processor = root / "src" / "plugin" / "PluginProcessor.cpp"
    pipeline_header = root / "src" / "core" / "AtsAnalysisPipeline.h"

    plugin_text = plugin_processor.read_text(encoding="utf-8")
    pipeline_text = pipeline_header.read_text(encoding="utf-8")

    require("runModernAtsAnalysis(ap, localMono, fileSr, modernConfig)" in plugin_text,
            "plugin Modern/Experimental analysis must call runModernAtsAnalysis on source audio")
    require("overlayClassicResidualBands(modernModel" in plugin_text,
            "plugin should only use classic ATS data to overlay residual bands")
    require("cfgP.transientGain = 1.0;" in plugin_text,
            "plugin Modern preview must render captured transient buffers")
    require("ModernAtsAnalysisResult" in pipeline_text,
            "core direct modern analysis result type is missing")

    forbidden = [
        "buildModernModelFromClassicAtsSound",
        "trackingPeakFramesFromAtsSound",
        "classicPartialsAsModern",
        "populateModernFramesFromPartials",
        "derived ATS peak frames",
        "sourceHash = \"classic:\"",
    ]
    for pattern in forbidden:
        require(pattern not in plugin_text,
                f"plugin still contains classic-derived Modern ATS path pattern: {pattern}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
