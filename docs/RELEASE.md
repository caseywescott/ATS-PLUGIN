# Release, signing, and packaging (Phase 5)

Checklist and templates for shipping **ATS Resynth** binaries. **This is not legal advice.** Verify Apple, Microsoft, Steinberg, and JUCE obligations for your distribution model.

## License pointers for release notes

Every public release should link or name:

| Component | Pointer |
|-----------|---------|
| **This project** | Repository `LICENSE` (add one if missing before distribution). |
| **ATSA / synthesis** | Qatsh `qatsh/src/atsa/` — see upstream `README.txt` / `LICENSE`; see `THIRD_PARTY.md`. |
| **JUCE** | [juce.com](https://juce.com/) — GPL vs commercial terms per your JUCE license. |
| **VST3** | [Steinberg VST3 SDK](https://developer.steinberg.help/) — SDK and trademark guidelines for VST3 builds. |
| **AU (macOS)** | Apple Audio Unit distribution requirements; code signing and (for distribution outside Mac App Store) **notarization** expectations. |
| **libsndfile** | LGPL — comply if linking statically (see `THIRD_PARTY.md`). |

### Release notes template (copy for `CHANGELOG` or GitHub Release)

```markdown
## ATS Resynth <version>

### Binaries
- macOS: VST3, AU, Standalone (architecture: …)
- Windows: VST3, Standalone (x64) — …

### Highlights
- Smearnov: model-domain onset detection + attack-smearing (Blur / Sharpen / Exaggerate) with editable spectrogram overlay — Session tab `[Morph | Smearnov]`. See `docs/SMEARNOV_SPEC.md`.
- …

### Upstream / licenses
- DSP: ATSA via Qatsh (`https://gitlab.com/dxarts/projects/ats/qatsh`). See bundled `THIRD_PARTY.md`.
- UI / plug-in shell: JUCE (`https://juce.com/`).
- VST3: Steinberg VST3 SDK terms apply to VST3 builds.

### Install
- macOS: copy `.vst3` / `.component` / `.app` to the usual plug-in or Applications locations.
- Windows: copy `.vst3` to `%CommonProgramFiles%\VST3` or host-specific path.
```

Replace `<version>` with the actual version string when publishing.

---

## macOS: code signing and notarization (checklist)

Prerequisites:

- **Apple Developer** membership (paid program) for distribution signing and notarization.
- **Developer ID Application** certificate installed in Keychain (for distribution outside the Mac App Store).
- **App-specific password** or API key for `notarytool`.

### 1. Build Release binaries

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_PLUGIN=ON
cmake --build build --config Release
```

Collect artefacts, for example:

- `build/src/plugin/AtsResynth_artefacts/Release/VST3/ATS Resynth.vst3`
- `build/src/plugin/AtsResynth_artefacts/Release/AU/ATS Resynth.component` (macOS only)
- `build/src/plugin/AtsResynth_artefacts/Release/Standalone/ATS Resynth.app`

### 2. Sign each bundle deeply

Use your **Developer ID Application** identity name from `security find-identity -v -p codesigning`:

```sh
IDENTITY="Developer ID Application: Your Name (TEAMID)"
codesign --force --options runtime --deep --sign "$IDENTITY" "path/to/ATS Resynth.vst3"
codesign --force --options runtime --deep --sign "$IDENTITY" "path/to/ATS Resynth.component"
codesign --force --options runtime --deep --sign "$IDENTITY" "path/to/ATS Resynth.app"
```

Verify:

```sh
codesign -dv --verbose=4 "path/to/ATS Resynth.vst3"
spctl -a -vv -t install "path/to/ATS Resynth.vst3"
```

### 3. Notarize (distribution outside App Store)

Zip each deliverable (or one zip), then submit:

```sh
ditto -c -k --keepParent "ATS Resynth.vst3" "ATS-Resynth-vst3.zip"
xcrun notarytool submit "ATS-Resynth-vst3.zip" --keychain-profile "AC_NOTARY" --wait
```

Staple the ticket to the bundle **after** successful notarization:

```sh
xcrun stapler staple "path/to/ATS Resynth.vst3"
```

Repeat for `.component` and `.app` as needed.

### 4. Gatekeeper smoke test

On a clean machine (or after `spctl` reset for testing): open the host, rescan plug-ins, load the plug-in once.

---

## Windows: packaging (minimum)

Recommended **minimum** for independent distribution:

1. Build **Release** `x64` with `BUILD_PLUGIN=ON`.
2. Ship a **zip** containing:
   - `ATS Resynth.vst3` (bundle folder)
   - `ATS Resynth.exe` standalone if desired
   - **`THIRD_PARTY.md`** (or a short `NOTICE.txt`) plus any required VC++ runtime note.
3. Optional: **Inno Setup** or **WiX** installer with Start Menu shortcuts—out of scope for this repo’s default; teams add their own scripts.

**Authenticode** signing for `.dll` / `.vst3` contents is strongly recommended for enterprise deployments; obtain a Windows code-signing certificate and sign with `signtool` as per Microsoft documentation.

---

## CI vs release builds

- **CI** in this repository builds **unsigned** artefacts for smoke testing.
- **Release** builds should use the steps above on a **trusted machine** with certificates—not by embedding secrets in GitHub Actions unless you use approved secret storage and hardened workflows.

---

## See also

- `THIRD_PARTY.md` — licenses and distribution assumptions.  
- `BUILDING.md` — artefact paths and options.  
- PRD §6.4, §8.
