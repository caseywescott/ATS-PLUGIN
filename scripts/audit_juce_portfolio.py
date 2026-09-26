#!/usr/bin/env python3
"""Read-only source inventory; writes only explicitly requested report files.

This lexical inventory is an evidence finder, not a compiled dependency graph
or a completeness/no-JUCE proof. Comments and inactive branches can match;
unqualified names, aliases, generated code and transitive dependencies require
manual/build-graph inspection. Tests and third-party sources are excluded.
"""

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


PROJECTS = {
    "ATS": ("ATS-PLUGIN", ["ATS Resynth"], ["src"], ["CMakeLists.txt", "src", "cmake"]),
    "CF": ("COMPRESSED_FEEDBACK/CompressedFeedbackVST", ["CompressedFeedback", "CFSynth"],
           ["kit", "plugins"], ["CMakeLists.txt", "cmake"]),
    "MIR": ("EnsoniqMirage/mir8", ["MIR-8", "MIR-8 FX"],
            ["engine/include", "engine/src", "plugins", "formats"], ["CMakeLists.txt", "plugins", "engine", "formats"]),
    "EQ": ("GraphicEQ", ["Avedis E12G"], ["source"], ["CMakeLists.txt"]),
    "HEART": ("HeartSynth", ["HeartSynth"], ["Source"], ["CMakeLists.txt"]),
    "HYDRO": ("HydroSynth", ["HydroSynth"], ["Source"], ["CMakeLists.txt"]),
    "MIDI": ("MIDI_COLLAB_MINIMAL", ["MIDI Network Plugin"],
             ["juce_plugin_server/plugin/Source"],
             ["juce_plugin_server/plugin/CMakeLists.txt", "juce_plugin_server/plugin/Source/replace_juce"]),
    "GATE": ("PunchGate", ["PunchGate"], ["plugin/Source"], ["CMakeLists.txt", "plugin"]),
    "WARP": ("SNDWARP", ["SNDWARP"], ["Source"], ["CMakeLists.txt"]),
}

EXCLUDE = {"tests", "test", "vendor", "third_party", "thirdparty", "libs", "juce", "juce_build",
           "node_modules", "_deps", "juceLibraryCode".lower(), "oracle"}
SOURCE_EXTENSIONS = {".h", ".hpp", ".hh", ".c", ".cpp", ".cc", ".mm", ".m"}
SYMBOL = re.compile(r"\bjuce::([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)")
MODULE = re.compile(r"\bjuce_(?:audio_basics|audio_devices|audio_formats|audio_plugin_client|audio_processors|"
                    r"audio_utils|core|data_structures|dsp|events|graphics|gui_basics|gui_extra|opengl|cryptography|osc|video)\b")
IMPLICIT = re.compile(r"\busing\s+namespace\s+juce\b|\busing\s+(?:\w+\s*=\s*)?juce::|\bnamespace\s+\w+\s*=\s*juce\b|"
                      r"\b(?:JUCE_[A-Z_]+|JuceHeader|jassertfalse|jassert|DBG|BinaryData)\b")

# Contract owners, not literal drop-in replacement types. APVTS, for example,
# also needs STATE and UI bindings. See the strategy spec for cross-module work.
CATEGORY_GROUPS = {
    "BASE": "Array CharPointer_UTF8 HashMap ListenerList MathConstants Random Range ScopedValueSetter "
            "SparseSet String StringArray StringPairArray Uuid degreesToRadians dontSendNotification "
            "ignoreUnused int64 isPositiveAndBelow jlimit jmap jmax jmin juce_wchar roundToInt "
            "sendNotification sendNotificationSync uint32 uint8",
    "RT": "AbstractFifo AudioBuffer FloatVectorOperations ScopedNoDenormals",
    "HOST": "AudioChannelSet AudioPlayHead AudioProcessor PluginHostType",
    "PARAM": "AudioParameterBool AudioParameterBoolAttributes AudioParameterChoice "
             "AudioParameterChoiceAttributes AudioParameterFloat AudioParameterFloatAttributes "
             "AudioParameterInt AudioProcessorParameter AudioProcessorParameterWithID "
             "AudioProcessorValueTreeState NormalisableRange ParameterAttachment ParameterID RangedAudioParameter",
    "STATE": "DynamicObject Identifier JSON MemoryBlock ValueTree XmlDocument XmlElement var",
    "EDIT": "UndoManager",
    "JOBS": "AsyncUpdater CriticalSection MessageManager MultiTimer ScopedLock Thread ThreadPool "
            "Time Timer VBlankAttachment WaitableEvent",
    "MIDI": "MidiBuffer MidiFile MidiKeyboardComponent MidiKeyboardState MidiMessage "
            "MidiMessageCollector MidiMessageSequence",
    "VOICE": "Synthesiser SynthesiserSound SynthesiserVoice",
    "DSP": "ADSR Decibels SmoothedValue ValueSmoothingTypes dsp",
    "FILES": "File FileInputStream FileOutputStream MemoryOutputStream",
    "ASSETS": "AudioFormatManager AudioFormatReader",
    "NET": "InterprocessConnection StreamingSocket URL",
    "UI": "AlertWindow AudioProcessorEditor Button CallOutBox CaretComponent ComboBox Component "
          "DocumentWindow DragAndDropContainer FileBrowserComponent FileChooser FileDragAndDropTarget "
          "GenericAudioProcessorEditor ImageComponent KeyPress Label ListBox ListBoxModel LookAndFeel_V4 "
          "MessageBoxIconType ModalCallbackFunction ModifierKeys MouseCursor MouseEvent MouseWheelDetails "
          "PopupMenu ResizableWindow ScrollBar SettableTooltipClient Slider StretchableLayoutManager "
          "StretchableLayoutResizerBar TabbedButtonBar TabbedComponent TableHeaderComponent TableListBox "
          "TableListBoxModel TextButton TextEditor ToggleButton TooltipWindow Viewport",
    "DRAW": "AffineTransform Colour ColourGradient Colours Drawable Font FontOptions GlyphArrangement "
            "Graphics Image ImageFileFormat Justification Line Path PathStrokeType Point Rectangle RectanglePlacement",
    "GPU": "OpenGLContext OpenGLRenderer OpenGLShaderProgram Vector3D gl",
    "OS": "Desktop PropertiesFile SystemClipboard SystemStats",
}
CATEGORY = {symbol: group for group, symbols in CATEGORY_GROUPS.items() for symbol in symbols.split()}


def files_in(root, entries, cmake=False):
    paths = set()
    for entry in entries:
        start = root / entry
        candidates = [start] if start.is_file() else start.rglob("*") if start.is_dir() else []
        for path in candidates:
            parts = path.relative_to(root).parts
            if any(p.startswith((".", "build")) or p.lower() in EXCLUDE for p in parts):
                continue
            if not path.is_file() or path.is_symlink():
                continue
            if (cmake and (path.name == "CMakeLists.txt" or path.suffix == ".cmake")) or (
                    not cmake and path.suffix in SOURCE_EXTENSIONS):
                paths.add(path)
    return sorted(paths)


def git_head(root):
    run = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"],
                         text=True, capture_output=True, check=False)
    return run.stdout.strip() if run.returncode == 0 else None


def audit(base, key, config):
    relative, products, source_dirs, build_dirs = config
    root = base / relative
    if not root.is_dir():
        return {"key": key, "root": str(root), "products": products, "status": "missing"}
    sources = files_in(root, source_dirs)
    builds = files_in(root, build_dirs, cmake=True)
    symbols, modules, implicit = {}, {}, []
    fingerprint = hashlib.sha256()
    touched = []
    errors = [{"path": entry, "error": "configured scope missing"}
              for entry in sorted(set(source_dirs + build_dirs)) if not (root / entry).exists()]
    for path in sorted(set(sources + builds)):
        relative_path = path.relative_to(root).as_posix()
        try:
            data = path.read_bytes()
            content = data.decode("utf-8")
        except (OSError, UnicodeError) as exc:
            errors.append({"path": relative_path, "error": str(exc)})
            continue
        fingerprint.update(relative_path.encode() + b"\0" + data + b"\0")
        matched = False
        for number, line in enumerate(content.splitlines(), 1):
            reference = {"path": relative_path, "line": number}
            for match in MODULE.finditer(line):
                modules.setdefault(match.group(), []).append(reference)
            if path not in sources:
                continue
            for match in SYMBOL.finditer(line):
                name = match.group(1)
                record = symbols.setdefault(name, {"category": CATEGORY.get(name.split("::")[0], "UNCLASSIFIED"),
                                                    "occurrences": 0, "references": []})
                record["occurrences"] += 1
                if reference not in record["references"]:
                    record["references"].append(reference)
                matched = True
            if IMPLICIT.search(line):
                implicit.append(reference)
                matched = True
        if matched:
            touched.append(relative_path)
    return {"key": key, "root": str(root), "products": products,
            "status": "read_errors" if errors else "scanned", "git_head": git_head(root),
            "source_sha256": fingerprint.hexdigest(), "source_files_scanned": len(sources),
            "build_files_scanned": len(builds), "files_with_lexical_juce_or_macro_matches": touched,
            "source_scopes": source_dirs, "build_scopes": build_dirs,
            "modules": dict(sorted(modules.items())), "symbols": dict(sorted(symbols.items())),
            "unclassified_symbols": sorted(s for s, record in symbols.items()
                                            if record["category"] == "UNCLASSIFIED"),
            "implicit_macro_or_alias_references": implicit, "read_errors": errors}


def markdown(report):
    lines = ["# Portfolio JUCE source inventory", "", f"Audit date: {report['audit_date']}", "",
             "Generated by `scripts/audit_juce_portfolio.py`. Counts describe lexical source matches,",
             "not unique dependencies, active runtime calls, linkage, or completed migrations.",
             "Tests, oracle fixtures, bundled libraries, build output and generated code are excluded.",
             "Aliases, inheritance, transitive modules, resources and platform wrappers require manual review.",
             "Source fingerprints describe the working files read, including uncommitted edits; Git HEAD alone",
             "does not identify this snapshot. A missing root or read error is reported explicitly.", "",
             "Owner codes map to the modules in `PLUGIN_PLATFORM_STRATEGY_SPEC.md`; they identify",
             "the primary contract owner, not a complete implementation or one-to-one substitution.", "",
             "| Project | Products | Source files | Qualified JUCE symbols | Direct module mentions |",
             "|---|---|---:|---:|---|"]
    for p in report["projects"]:
        if p["status"] == "missing":
            lines.append(f"| {p['key']} | {', '.join(p['products'])} | missing | — | — |")
            continue
        lines.append(f"| {p['key']} | {', '.join(p['products'])} | {p['source_files_scanned']} | "
                     f"{len(p['symbols'])} | {', '.join(p['modules']) or 'None in scoped files'} |")
    for p in report["projects"]:
        if p["status"] == "missing":
            continue
        lines.extend(["", f"## {p['key']} — {', '.join(p['products'])}", "",
                      f"Root: `{p['root']}`  ", f"Git HEAD: `{p['git_head']}`  ",
                      f"Working source SHA-256: `{p['source_sha256']}`", "",
                      f"Unclassified symbols: {len(p['unclassified_symbols'])}", "",
                      "| Qualified symbol | Owner | Occurrences | First source reference |", "|---|---|---:|---|"])
        for name, record in p["symbols"].items():
            ref = record["references"][0]
            lines.append(f"| `juce::{name}` | {record['category']} | {record['occurrences']} | `{ref['path']}:{ref['line']}` |")
        if not p["symbols"]:
            lines.append("| None found | — | 0 | Does not establish a JUCE-free compiled artifact |")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-dir", type=Path, default=Path("/Volumes/Extreme SSD"))
    parser.add_argument("--audit-date", required=True)
    parser.add_argument("--json-output", type=Path, required=True)
    parser.add_argument("--markdown-output", type=Path, required=True)
    args = parser.parse_args()
    report = {"schema_version": 2, "audit_date": args.audit_date,
              "method": "lexical source and CMake inspection, not compiled reachability",
              "projects": [audit(args.base_dir, key, config) for key, config in PROJECTS.items()]}
    args.json_output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.markdown_output.write_text(markdown(report), encoding="utf-8")
    for p in report["projects"]:
        print(p["key"], p["status"], p.get("source_files_scanned", 0), "files,",
              len(p.get("symbols", {})), "qualified symbols,",
              len(p.get("unclassified_symbols", [])), "unclassified")
    return int(any(p["status"] != "scanned" or p.get("unclassified_symbols") for p in report["projects"]))


if __name__ == "__main__":
    raise SystemExit(main())
