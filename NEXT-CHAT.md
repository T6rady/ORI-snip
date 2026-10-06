# Tiger Snip handoff

## Scope and next priority

Continue work on this small native Windows screenshot utility in `C:\Users\jackk\repos\jack-snip`. The product name is **Tiger Snip**, inspired by Samtec's tiger; the app icon now has an orange background. The GitHub repository/folder still uses `jack-snip`.

The user wants a dependable, understandable internal tool that IT can review and distribute. Keep changes proportionate: fix real bugs, awkward failure paths, and measured performance issues. No broad architecture rewrite, framework migration, continuous-release infrastructure, speculative threading, or ceremonial compliance work. Signing and CI were deferred. Explain recommendations in plain language; the user is not a coding expert.

**Slow text-box editing (original review item 22) is complete.** The user subsequently authorized items **20, 28, and 5**, which are also complete. Recommend another change before implementing it; do not automatically implement the remaining audit.

## Text performance change and evidence

The repeatable external test is `tests/text_edit.cpp`, included in `build.ps1 -Test` and optional CMake tests. It measures synchronous `WM_CHAR` handling over 30 characters, a 1050×740 window, 3840×2160 screenshot, software rendering, and a private noninteractive desktop/clipboard. It excludes the later queued window paint and is a fixture comparison, not a universal input-latency claim.

| Fixture | Before median / p95 (ms) | After median / p95 (ms) |
|---|---:|---:|
| Plain | 46.24 / 48.26 | 2.68 / 3.09 |
| Boxed | 46.45 / 48.95 | 3.02 / 3.25 |
| Boxed, border and soft logo | 111.89 / 113.55 | 4.59 / 4.86 |

- `previewImage()` reuses the screenshot while native EDIT supplies plain glyphs. A changing box re-renders the union of its old/new bounds from the original screenshot and all annotations in their existing order, then updates cached display bitmaps in place. Other annotation changes and export-option changes use full regeneration. Edits touching rounded outer edges or the adaptive logo use the established full export path; those cases remain slower.
- `syncTextEditor()` retains a WIC bitmap, software target, screenshot upload, and workspace brush for the editing session, and reads back only the field. It still uses the complete editor drawing routine: restricting its clip changed a few interpolation pixels at fractional positions. The retained resources are released when editing finishes and rebuilt for window-size/DPI changes. Font and native background-brush creation remain unchanged.
- All **55** generated scene/native-field/final-export PNGs match a build of the original source byte for byte. The focused checks compare every field-background pixel and cached preview with full rendering; cover shrink/grow, selection/replacement, multiline wrapping, font/bold/color, annotation overlap/order, cancel/undo/redo, crop, fractional zoom/pan, 100/150/200% DPI, styled edges/logo overlap; and check actual PNG/DIB clipboard payloads and PNG round trips.
- Final warning-enabled `build.ps1 -Test`, desktop smoke, and fresh-process preference verification passed. Smoke additionally checks text movement/resizing, box toggling, live renderer switching, and the ten-capture Recent behavior. Plain, boxed, and multiline smoke images were visually inspected.
- Evidence: `build/text-before/results-full.txt`; final focused artifacts in `build/test-output/53b96de2-722e-401b-849b-78474d3ddd32`; desktop artifacts in `build/text-desktop/{F9A3C052-EF39-450F-A96D-D97E828FADEB}`. Generated files stay ignored.
- No renderer-default, retention, threading, general editor rewrite, signing, or release-infrastructure change. Do not attribute the earlier hardware stalls to an unestablished graphics-driver cause.

## Completed work

Originally pulled `main` from `dedcd81` to `a3bca24`, then reviewed the code for internal deployment. Original audit IDs are retained in the open lists.

- **1:** PNG overwrites preserve existing file permissions/DACL, with safe replacement/recovery handling and dedicated tests.
- **2 / basic 27:** Per-user local MSI installation, Start menu shortcut, and independent LocalAppData preferences. Existing adjacent preferences migrate once. Installer excludes INIs/screenshots.
- **3:** Removed old executable/archive; current EXE and MSI are Tiger Snip 1.0.2.
- **9:** Corrected guide, defaults, data handling, retention, and installation instructions.
- **13:** Palette limited to 64 colors; compile-time disjoint palette/Recent command ranges; strict numeric loading and duplicate/invalid-entry handling.
- **14:** Startup registry strings are read with explicit type, size, alignment, and termination bounds.
- **15:** Shared callback exception boundary catches standard/unknown exceptions and recovery failures; failed window creation returns the appropriate failure value. Error reporting avoids allocation.
- **16:** Graphics initialization builds resources locally and publishes them only when complete; late failure/retry tests pass.
- **19:** Useful Windows error codes, checked executable-path lookup and message-loop errors, important API checks/fallbacks, and differentiated clipboard failures.
- **24:** Related preferences save through staged Unicode INI updates and atomic replacement; unrelated values survive, dirty flags remain on failure, and retry works.
- **30:** Checked report writes, unique test output folders, explicit restart fixtures, nonzero failure status, and contained report-write failures.
- **22:** Focused text-preview/resource reuse and field-only readback, with the timings and appearance/export verification above.
- **20:** GUID staging filenames, current-attempt cleanup on normal/exception failures, retained/documented crash/recovery leftovers, and forced-termination/permissions/retry tests.
- **28:** Second launches wait up to five seconds for the first window and check forwarding failures. Three simultaneous sender processes, exactly-once Open/Snip requests, existing-window delivery, and bounded timeout are tested on a private desktop.
- **5:** Pinned default LLVM-MinGW 20260922/Clang 23.1.2, verified publisher/archive SHA256 in BUILD-TOOLCHAIN.md, build JSON with compiler/options/input/EXE hashes, and package mismatch rejection tests. No CI/release service.
- Support contact and component inventory: Jack Kempf, jack.kempf@samtec.com, in IT-HANDOFF.md. Corporate artwork/distribution approval remains an IT/company decision.
- Initial **25 / 29** cleanup: separate settings, Windows helpers, color picker, capture, clipboard, file saving, and test-report modules. CMake shares target configuration and supports optional tests; PowerShell shares the module list. Existing editor structure remains.

## Current release and personal settings

- Distribution: `dist/Tiger Snip Setup.msi`. IT places this on the share; users open it once to install locally. Running an ordinary EXE does not install it automatically. **The current EXE/MSI include items 22, 20, 28, and 5. The MSI was rebuilt and uninstalled/reinstalled without elevation; its installed EXE matches the distribution hash. Installed-app smoke and fresh-process preference checks passed.**
- Installed program: `%LOCALAPPDATA%\Programs\Tiger Snip\Tiger Snip.exe`.
- Personal preferences: `%LOCALAPPDATA%\Tiger Snip\TigerSnip.ini`.
- **Keep `SoftwareRendering=1` on this PC**, both in its ignored `dist/TigerSnip.ini` and personal preferences. Fresh users retain the normal hardware/default renderer. Read `AGENTS.md`.
- The user authorizes closing/terminating/restarting the app and discarding current snips/unsaved annotations for coding/testing without confirmation.
- Ten recent captures remain a requirement. They live in process memory until File > Exit; closing to the tray keeps the app/history alive. Saved PNGs and Windows clipboard content have separate lifetimes.
- No application networking/telemetry was identified. Optional Windows clipboard history/synchronization is an IT policy question, not evidence that the app uploads images.
- The MSI remains unsigned. For another changed development build of this same 1.0.2 MSI, uninstall the previous copy, then install the new one. Preferences survive. Same-version repair/reinstall flags previously failed Windows SecureRepair; do not weaken policy.

## Verification and commands

The warning-enabled LLVM-MinGW build, native model/rendering/PNG tests, isolated clipboard/auto-copy tests, protected PNG-save tests, independent-profile migration tests, and robustness failure tests passed. Installed-app desktop smoke and fresh-process preference restoration passed. MSI uninstall/install returned zero and its installed EXE matched the distribution hash. DEP/ASLR/high-entropy flags were verified in the PE. Source-input hashes matched the tested source.

An earlier desktop test exposed a timing-sensitive I-beam/arrow cursor comparison; subsequent runs passed. CMake/MSVC, enterprise EDR/DLP, RDP, and a real mixed-monitor fleet have not been validated here. The earlier hardware-rendering stalls have no established driver/compositor diagnosis.

Local build tools: `.tools/llvm-mingw-20260922-ucrt-x86_64/bin` (Clang 23.1.2). They are ignored, not distributed.

```powershell
.\build.ps1 -Test
.\package.ps1
```

Developer app test flags create a unique `test-output/{GUID}` directory. `--test-output <root>` chooses its parent. Fresh-process checks require `--test-fixture <producer-run-directory>` and create their own report folder. Tests can capture real desktop images; keep generated outputs under ignored `build/`, never in the release payload.

```powershell
# Close the ordinary app first for desktop suites; preserve this PC's renderer preference.
& '.\dist\Tiger Snip.exe' --smoke-test --software-rendering --test-output 'C:\Users\jackk\repos\jack-snip\build\text-smoke'
# After the producer exits, substitute the actual GUID folder:
& '.\dist\Tiger Snip.exe' --verify-smoke-preferences --test-fixture '<producer-run-directory>' --test-output 'C:\Users\jackk\repos\jack-snip\build\text-restart'
```

Fault injection hooks exist only in `TIGER_SNIP_TESTING` builds. Some integration tests still include `main.cpp`; moving diagnostic drivers out of the shipping app is open item 12.

## Remaining choices

`IT-WALKTHROUGH.md` is the plain-language **open-only** list; `IT-REVIEW.md` has the technical evidence. **22 is complete.** **5, 20, and 28 are complete.** **12** (separate older built-in developer tests) is worthwhile after review, or before rollout if IT requires it; it has not been implemented. **8 and 27** need company/artwork approval and a small corporate IT pilot. Recommend additional changes before implementation. Other memory, accessibility, performance, and style work depends on actual requirements and measurements.

This checkpoint includes the follow-up fixes and current release artifacts. The user authorized committing and pushing them. Do not treat the next performance change as a mandate to finish every remaining audit item.

## Git checkpoint

Earlier published source/release-artifact snapshot: 41023eba83ebfae3466e4ca829729e95a0b63985, followed by handoff commit 0b1433f. This checkpoint includes items 22, 20, 28, and 5, the updated EXE/MSI, and IT handoff documents. The build records retain their original Git base and dirty state at build time; their source-input and artifact hashes identify the tested bytes.

Latest verification: build/test-output/1cd3b5d9-6509-423a-9464-ac95475a9f16; build/it-final-smoke and build/it-final-restart; build/it-final-install.log and build/it-final-uninstall.log. All eight suites passed. Latest text medians: plain 2.47 ms, boxed 3.43 ms, boxed border/logo 4.53 ms. All 55 baseline PNGs still match. See IT-HANDOFF.md and VALIDATION.md for tomorrow's review; leave the app stopped so the user can launch it.
