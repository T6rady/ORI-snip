# Tiger Snip: internal IT review

Prepared October 6, 2026 for the October 7 review. This is a small, local Windows screenshot/annotation utility. The proposed deployment is an installer on a company shared drive, with each employee installing and running a local copy.

Maintainer and problem-reporting contact: **Jack Kempf — jack.kempf@samtec.com**. Company distribution and artwork approval remain decisions for IT/the appropriate Samtec owner; this document does not invent an approval or ownership license.

## What to give IT

Give reviewers the current source snapshot, this handoff, `IT-WALKTHROUGH.md` (plain-English open items), `IT-REVIEW.md` (technical evidence), `BUILD-TOOLCHAIN.md`, and the rebuilt MSI with `Tiger Snip Release.txt` and `Tiger Snip Build.json`. The build record identifies the actual tested source bytes, compiler, options, and EXE; the release record identifies the packaged files. These latest changes and release artifacts are included in this repository revision. The records preserve the Git base and working-tree state at build time.

For employee distribution after approval, place **Tiger Snip Setup.msi** on the shared drive. Runtime notices and Quick Start are already inside the installer. The release checksum record may accompany it for IT verification. Employees do not need the repository, build tools, test executables, test screenshots, or personal `TigerSnip.ini`. Do not share the entire development `dist` folder because it contains this PC's personal INI.

The ordinary EXE is useful for local demonstration and runs directly, but opening an EXE does not install the application. The MSI is the installation path.

## Installation, use, update, and removal

- Intended environment: x64 Windows 10/11 desktop. Local validation is on Windows 11 x64; other corporate versions and PCs still need the pilot. No separate downloaded application framework is required by the verified build.
- Users open the MSI from the shared folder, then launch **Tiger Snip** from the Start menu. The package installs per user into `%LOCALAPPDATA%\Programs\Tiger Snip`; it requests no administrator elevation. Company policies can still block the installer or app.
- Personal settings live in `%LOCALAPPDATA%\Tiger Snip\TigerSnip.ini`, independent of the shared folder. Adjacent portable settings migrate only when personal preferences do not already exist. Fresh users retain normal renderer defaults; software rendering stays enabled on Jack's current PC.
- Startup at sign-in is optional and defaults off. Turn it off in Settings before removal or switching executable locations. There is one ordinary instance per interactive Windows sign-in session; rapid additional launches forward Open/Snip requests to that instance. Separate diagnostic instances have separate names.
- The current package is version 1.0.2. For a changed package of this same version, close Tiger Snip, uninstall the old copy through Windows Installed apps, and install the replacement. Local testing previously found same-version repair/reinstall flags blocked by Windows SecureRepair; do not weaken that policy. Future numbered updates require deliberate installer version/upgrade handling, not an automatic update service.
- Removal uses Windows Installed apps. Personal preferences remain for a reinstall. Delete the settings folder separately only when a complete reset is wanted. Saved PNGs and clipboard content are separate from the application.
- Rollback: retain the previously approved MSI, close/uninstall the new copy, and install that earlier package. Confirm preferences and startup behavior after rollback; rollback is a manual support procedure.

## Data and components

The app has no identified application networking, upload, or telemetry path. It captures the local desktop, keeps editing state in memory, copies images to the Windows clipboard, and saves PNGs to a user-selected location. A selected save folder may itself be a network or synchronized folder. Windows clipboard history/synchronization and other clipboard-reading applications are separate from Tiger Snip; IT decides the company policy.

The last **ten captures**, including editable annotations and crop history, stay in process memory until File > Exit or capture eviction. Closing to the tray retains them. There is no editable-project file or automatic recovery of that history after exit.

Ordinary save failures attempt to remove only the current save's temporary file. Forced termination or power loss can leave an adjacent `*.tiger-snip-{GUID}.tmp` containing image pixels; an exceptional replacement failure may retain a `.tmp.previous` recovery copy. Leftovers are deliberately not automatically swept, because deleting an older recovery copy could destroy the only surviving original. Unique names prevent those leftovers from blocking later saves. If one needs removal, Jack/IT should inspect the destination folder and remove the specifically identified leftover after confirming recovery is unnecessary. Overwrites preserve the existing file's permissions; new files inherit the destination folder's permissions.

Eraser removes annotations, Highlight is translucent, and crops remain reversible during the session. These features are not permanent redaction tools.

| Component | Where it comes from / what IT receives |
|---|---|
| Tiger Snip 1.0.2 | Custom C++ source, EXE, MSI, documentation, and tests in the review snapshot. |
| Static C++/MinGW runtime components | Verified LLVM-MinGW 20260922 toolchain; `LLVM.txt` and `MinGW-runtime.txt` notices ship inside the MSI. Exact compiler/archive provenance is in `BUILD-TOOLCHAIN.md`. |
| Windows graphics/system components | Direct2D, DirectWrite, WIC, Win32/COM and UCRT supplied by Windows. |
| Company artwork | S, tiger, and wordmark assets embedded in the EXE. Source/provenance notes are in `resources/samtec-logo.md` and `resources/samtec-marks.md`; some transparent cutouts were produced with image editing from supplied artwork. Brand approval remains to be confirmed. |

## Changes and verification

Completed in the current work: **22**, faster text editing with unchanged appearance/exports; **20**, unique temporary save names and scoped cleanup; **28**, bounded handling of simultaneous launches; **5**, exact toolchain and build/package records. The software-rendering preference and ten-capture retention are preserved.

The new save test forcibly terminates a producer after staging pixels are flushed, checks that the original and protected permissions survive, and checks a later save succeeds while the leftover remains untouched. It also checks exception cleanup, locked/read-only files, and recovery-file collisions. The launch test starts three independent processes before the first window exists and checks each Open/Snip request arrives exactly once; missing-window startup produces a bounded error.

The text comparison previously matched all 55 original scene/native-field/export PNGs byte for byte. Text editing dropped from about 46 to 3 ms per boxed character in its fixture, and from about 112 to 5 ms with border/logo effects. These are synchronous character-handler timings on one fixture, not a guarantee for every PC or workload. Styled-edge/logo-overlap cases retain the full-render fallback.

Final verification passed: warning-enabled build and all eight native/integration suites; package hash checks; MSI uninstall/install with exit code zero and no elevation; installed-app desktop smoke; fresh-process preference restoration; unchanged personal INIs and SoftwareRendering=1; and all 55 original PNG comparisons. The installed EXE hash matches the distributed EXE. See VALIDATION.md and the delivered build/release records. The local checks cover drawing/history/crop, text behavior, PNGs and actual clipboard formats, file permissions, settings migration/failure recovery, ten recent captures, desktop smoke behavior, and fresh-process preference restoration. No claim is made for enterprise endpoint tools, a real corporate standard-user account, physical mixed-monitor fleets, RDP, MSVC/CMake, or a complete security audit.

## Separating developer tests from the shipped app

**Yes, this is worthwhile later (item 12), but it is not the next urgent change before this review.** Some developer diagnostics are still compiled into the EXE and run only through explicit test flags. Normal startup does not run the test suite. Fault-injection hooks are compiled only into dedicated test builds; the new text/save/launch integration checks are separate test executables and are not installed.

Moving the existing diagnostic block out would make the production source easier to review and reduce the shipping EXE. It touches a large, intertwined part of the entry point, so it deserves a separate change with the current smoke suite retained. If IT requires a production binary containing no test commands, make that a condition before broad deployment; otherwise defer it until after the review/pilot. No framework or editor rewrite is required.

## Readiness and remaining choices

This work is ready for IT review and an approved small pilot; the final local verification passed. Broad company rollout still needs IT's approval and a small test using the actual shared drive and a normal managed account: install, launch, capture, edit, copy/paste, save, restart, and uninstall. Confirm endpoint policy, intended supported PCs, artwork approval, support ownership, and relevant clipboard/accessibility requirements. Signing and CI remain deferred unless IT requires them.

| Item | Plain-English meaning | Suggested treatment |
|---|---|---|
| 6 | Check additional compiler protections against memory-corruption exploits. DEP/ASLR/high-entropy address protections are already enabled; CFG/stack-protection details remain to assess. | Follow IT's compiler/security baseline; no reproduced exploit is claimed. |
| 8 (partial) | Maintainer/contact and component inventory are now documented. Company distribution ownership and approval of the artwork still need confirmation. | IT/company decision before broad distribution. |
| 11 | Decide whether copied screenshots may enter Windows clipboard history or cloud synchronization. | IT policy decision; code changes only if required. |
| 12 | Move developer diagnostic/test commands out of the installed EXE. | Useful separate cleanup; prioritize if IT requires it. |
| 17 | Make more Windows handles clean themselves up on errors. The save staging file now does this; other raw handles remain. | Fix a concrete failure path when needed, rather than a wholesale conversion. |
| 18 | Make the remaining graphics/COM shutdown order easier to follow. | Low-priority cleanup; no shutdown crash reproduced. |
| 21 | Ten huge captures and long edit histories can consume substantial RAM. | Measure representative PCs; retain the required ten captures. |
| 23 | Measure heavy drawing, effects, copying, and slow save locations before optimizing them. | Respond to observed delays; avoid speculative threading. |
| 25 (partial) | Continue separating the editor's responsibilities into understandable files. Several modules are already split. | Start with item 12 when approved; no broad rewrite. |
| 26 | Check keyboard/screen-reader/contrast usability of custom controls. | IT specifies the required accessibility level. |
| 27 (partial) | Local installer/procedures are present; test the real company account, share, endpoint controls, and relevant monitors/remote sessions. | Main rollout prerequisite; a small pilot is enough to start. |
| 29 | Validate the alternate Visual Studio/CMake build. | Only urgent if IT intends to build that way. |
| 31 | Shorten the long README and consolidate developer documentation/formatting checks. | Optional cleanup; the new handoff gives IT a concise starting point. |
| 32 | Replace playful welcome messages with neutral wording. | Optional taste/presentation choice. |

No remaining item is automatically authorized for implementation. Choose a concrete next change after IT's feedback.
