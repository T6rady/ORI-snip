# Tiger Snip handoff

## Scope and next priority

Continue work on this small native Windows screenshot utility in `C:\Users\jackk\repos\jack-snip`. The product name is **Tiger Snip**, inspired by Samtec's tiger; the app icon now has an orange background. The GitHub repository/folder still uses `jack-snip`.

The user wants a dependable, understandable internal tool that IT can review and distribute. Keep changes proportionate: fix real bugs, awkward failure paths, and measured performance issues. No broad architecture rewrite, framework migration, continuous-release infrastructure, speculative threading, or ceremonial compliance work. Signing and CI were deferred. Explain recommendations in plain language; the user is not a coding expert.

**The next authorized development focus is slow text-box editing (original review item 22).** Measure it again, make a focused improvement, compare before/after timings, and preserve appearance and export behavior. Other open items remain selectable; do not automatically implement the entire audit.

## Text performance evidence and starting points

- The earlier synthetic benchmark measured **45.73 ms median / 48.27 ms p95 per character**, across 30 samples, with a 1050×740 editor, 3840×2160 screenshot, software rendering, and a private noninteractive desktop. This is a particular fixture, not a universal latency claim.
- In `src/main.cpp`, `WM_COMMAND / EN_CHANGE` calls `updateTextFromEditor()`, which calls `syncTextEditor()`.
- `syncTextEditor()` recreates font/background resources and calls `renderEditorPreview()`. The latter creates a WIC bitmap and Direct2D software target, paints the entire editor, and reads all its pixels back. Only the text field's background region is then copied into a pattern brush.
- Repainting/readback of the full window on each character is the identified expensive path. Consider reusing a stable background or rendering only the needed region. Inspect invalidation carefully so edits to other annotations, crop, zoom, DPI, formatting, and renderer changes remain correct.
- Preserve plain/boxed/multiline text, deletion and shrinking, growing/wrapping, selection backgrounds, font/bold/color changes, undo/cancel, move/resize, crop/zoom/DPI, and final PNG/clipboard pixels. Avoid stale backing pixels or text trails.
- The existing desktop smoke suite checks these behaviors and saves previews. Start with a repeatable focused benchmark, then use the relevant integration checks. Do not attribute latency to an unestablished graphics-driver cause.

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
- Initial **25 / 29** cleanup: separate settings, Windows helpers, color picker, capture, clipboard, file saving, and test-report modules. CMake shares target configuration and supports optional tests; PowerShell shares the module list. Existing editor structure remains.

## Current release and personal settings

- Distribution: `dist/Tiger Snip Setup.msi`. IT places this on the share; users open it once to install locally. Running an ordinary EXE does not install it automatically.
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

`IT-WALKTHROUGH.md` is the plain-language **open-only** list; `IT-REVIEW.md` has the technical evidence. Prioritize **22 now**. Previously recommended follow-ups were **12** (separate developer tests), **20** (interrupted-save leftovers/unique names), and **28** (rapid second-launch race). **8 and 27** need owner/support details and a small IT pilot. Other memory, accessibility, performance, and style work depends on actual requirements and measurements.

The user asked to push the completed work and prepare this handoff. Check Git status/history for the published revision before editing. Do not treat the next performance change as a mandate to finish every remaining audit item.

## Git checkpoint

Tested source and release-artifact snapshot: 41023eba83ebfae3466e4ca829729e95a0b63985. A following documentation commit records this checkpoint. The text-box optimization has not been implemented yet; it is the selected next task.
