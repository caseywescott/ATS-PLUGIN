# Release, signing, and packaging (Phase 5)

For current native targets, architecture separation, AU version packing,
Standalone verification, atomic AU installation, and Logic registrar refresh,
see [`MACOS_BUILD_INSTALL_SPEC.md`](MACOS_BUILD_INSTALL_SPEC.md). That
specification supersedes JUCE-era target names and artifact paths below for the
current JUCE-free build.

Checklist and templates for shipping **ATS Resynth** binaries. **This is not legal advice.** Verify Apple, Microsoft, and Steinberg obligations for your distribution model. JUCE has been removed from this project; see `THIRD_PARTY.md` for current dependency inventory.

## License pointers for release notes

Every public release should link or name:

| Component | Pointer |
|-----------|---------|
| **This project** | Repository `LICENSE` (add one if missing before distribution). |
| **ATSA / synthesis** | Qatsh `qatsh/src/atsa/` — see upstream `README.txt` / `LICENSE`; see `THIRD_PARTY.md`. |
| **Visage** (UI backend) | VitalAudio/visage — see upstream license; retain notices in distributed binaries. |
| **VST3 SDK** | [Steinberg VST3 SDK](https://developer.steinberg.help/) — SDK and trademark guidelines for VST3 builds. |
| **AudioUnitSDK** (macOS) | Apple AudioUnitSDK — AU and AudioToolbox redistribution requirements; notarization for outside-MAS distribution. |
| **libsndfile** | LGPL — comply if linking statically; arm64 bundle in `vendor/sndfile-arm64/`. See `THIRD_PARTY.md`. |

### Release notes template (copy for `CHANGELOG` or GitHub Release)

```markdown
## ATS Resynth <version>

### Binaries
- macOS: VST3, AU, Standalone (arm64 / x86_64)
- Windows: VST3, Standalone (x64) — …

### Highlights
- Smearnov: model-domain onset detection + attack-smearing (Blur / Sharpen / Exaggerate) with editable spectrogram overlay — Session tab `[Morph | Smearnov]`. See `docs/SMEARNOV_SPEC.md`.
- …

### Upstream / licenses
- DSP: ATSA via Qatsh (`https://gitlab.com/dxarts/projects/ats/qatsh`). See bundled `THIRD_PARTY.md`.
- UI backend: Visage (VitalAudio/visage). See upstream license.
- Audio I/O: libsndfile (LGPL). See `THIRD_PARTY.md`.
- VST3: Steinberg VST3 SDK terms apply to VST3 builds.
- AU (macOS): Apple AudioUnitSDK and AudioToolbox terms apply.

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

Collect the native artifacts:

- `build/src/host/vst3/ATS Resynth.vst3`
- `build/src/host/au/ATS Resynth.component` (macOS only)
- `build/src/host/standalone/ATS Resynth.app`

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
