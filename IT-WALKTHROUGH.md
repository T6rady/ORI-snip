# Tiger Snip: remaining choices

This list contains only open work. The original numbers are kept so you can select the next batch. The requested bug and failure-handling fixes are complete, and the app now has separate files for settings, Windows helpers, color picking, capture, clipboard, file saving, and test reports. No broad rewrite or release automation is needed.

**22: slow text-box editing is complete.** In the repeated large-image fixture, boxed typing improved from 46.45 to 3.02 ms per character; appearance and exports matched the original build. **5, 20, and 28 are complete** as well: the build is recorded, crash leftovers no longer block saving, and rapid launches retain their requests. **12** remains optional cleanup after review; **8 and 27** still need company/artwork approval and a small IT pilot. The support contact is Jack Kempf (jack.kempf@samtec.com). These are recommendations; choose another change before implementation. See [the timings and verification](NEXT-CHAT.md).

| Item | What it means in ordinary language | Priority for your release |
|---|---|---|
| 6 | Verify the protection settings in whichever compiler IT actually uses. The current LLVM build already requests DEP/ASLR/high-entropy protections. | Useful verification; no exploit was found. |
| 8 | Maintainer/contact and component inventory are documented. Confirm company distribution ownership and approval of the corporate artwork. Runtime notices already ship. | Finish before the handoff. |
| 11 | Decide whether Windows clipboard history/synchronization needs special treatment. The app itself has no upload/networking feature; Windows controls copied content afterward. | IT policy decision; no upload was observed. |
| 12 | Move developer tests out of the shipping app. Today they run only when someone explicitly supplies a test flag, but they make the main source file unnecessarily large. | Useful separate cleanup after IT review. |
| 17 | Make Windows resources clean themselves up when an operation fails, reducing leaks and missed cleanup. | Target vulnerable paths; no wholesale conversion. |
| 18 | Make graphics shutdown more straightforward. The missing font cleanup was corrected, but the remaining manual shutdown could be clearer. | Low priority. |
| 21 | Ten very large captures plus edits can use substantial RAM. Add safeguards for extreme workloads if representative PC testing shows a problem. | Keep the required ten captures; decide safeguards from evidence. |
| 23 | Measure demanding drawing/copy/save cases before doing broader performance work. | Optional; no speculative threading framework. |
| 25 | The initial file split is done. The editor still combines several UI responsibilities. Move its test block first, then split more only as useful. | Continue incrementally with item 12. |
| 26 | Check whether custom buttons work adequately with keyboard navigation and screen readers. | IT should specify the required level. |
| 27 | Pilot the installer on a normal corporate account and representative PCs, and document support/removal. The local per-user installer and instructions already exist. | Finish the real IT pilot. |
| 29 | The repeated build settings are reduced and CMake tests are optional. The alternate MSVC/CMake build has not been run here. | Verify only if IT uses that build route. |
| 31 | Shorten the long README and move detailed developer test instructions into a focused document. Changed production files now follow the existing formatter. | Optional presentation cleanup. |
| 32 | Replace playful rotating welcome messages with neutral wording if desired. Tiger Snip naming and the orange icon are already done. | Taste; not a deployment blocker. |

Signing and continuous integration remain deferred as you requested. Ten captures stay in local process memory until File > Exit; closing to the tray retains them. Those are intentional choices, not unfinished bugs.

For distribution, IT puts **Tiger Snip Setup.msi** on the shared drive. Each person opens it once; it installs the app and Start menu shortcut locally under their Windows account. Their preferences live separately in `%LOCALAPPDATA%\Tiger Snip\TigerSnip.ini`. An ordinary EXE runs directly; opening it does not automatically install it.

The warning-enabled build, installed-app desktop smoke test, fresh-process preference check, and tests for rendering, clipboard, protected PNG saves, per-user migration, damaged settings, interrupted preference writes/retry, bounded inputs, callbacks, graphics initialization/retry, and isolated checked reports pass. IT still needs to validate its installation policies and actual PC setup. The detailed open findings are in [IT-REVIEW.md](IT-REVIEW.md).
