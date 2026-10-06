# Tiger Snip: remaining choices

This list contains only open work. The original numbers are kept so you can select the next batch. The requested bug and failure-handling fixes are complete, and the app now has separate files for settings, Windows helpers, color picking, capture, clipboard, file saving, and test reports. No broad rewrite or release automation is needed.

The next priority is **22: slow text-box editing**, selected by the user. Measure and improve the unnecessary full-window redraw during typing. After that, the recommended cleanup/reliability batch is **12, 20, and 28**. Alongside them, finish **8 and 27** with owner/support details and a small IT pilot. See [the next-chat handoff](C:/Users/jackk/repos/jack-snip/NEXT-CHAT.md).

| Item | What it means in ordinary language | Priority for your release |
|---|---|---|
| 5 | Record the exact compiler and where it came from so IT can rebuild the program later. Users never need the compiler. | A brief build note is enough. |
| 6 | Verify the protection settings in whichever compiler IT actually uses. The current LLVM build already requests DEP/ASLR/high-entropy protections. | Useful verification; no exploit was found. |
| 8 | Identify who maintains Tiger Snip, where users report problems, and whether the corporate artwork is approved for internal use. Runtime notices already ship. | Finish before the handoff. |
| 11 | Decide whether Windows clipboard history/synchronization needs special treatment. The app itself has no upload/networking feature; Windows controls copied content afterward. | IT policy decision; no upload was observed. |
| 12 | Move developer tests out of the shipping app. Today they run only when someone explicitly supplies a test flag, but they make the main source file unnecessarily large. | Recommended next cleanup. |
| 17 | Make Windows resources clean themselves up when an operation fails, reducing leaks and missed cleanup. | Target vulnerable paths; no wholesale conversion. |
| 18 | Make graphics shutdown more straightforward. The missing font cleanup was corrected, but the remaining manual shutdown could be clearer. | Low priority. |
| 20 | An interrupted save can leave an image in a temporary file, and an old temporary name can obstruct another save. Use unique names and careful cleanup. | Recommended next reliability fix. |
| 21 | Ten very large captures plus edits can use substantial RAM. Add safeguards for extreme workloads if representative PC testing shows a problem. | Keep the required ten captures; decide safeguards from evidence. |
| 22 | Typing text redraws more of the image than needed. A previous large-image test measured about 46 ms per character. | Improve if typing feels sluggish. |
| 23 | Measure demanding drawing/copy/save cases before doing broader performance work. | Optional; no speculative threading framework. |
| 25 | The initial file split is done. The editor still combines several UI responsibilities. Move its test block first, then split more only as useful. | Continue incrementally with item 12. |
| 26 | Check whether custom buttons work adequately with keyboard navigation and screen readers. | IT should specify the required level. |
| 27 | Pilot the installer on a normal corporate account and representative PCs, and document support/removal. The local per-user installer and instructions already exist. | Finish the real IT pilot. |
| 28 | Two very rapid launches can occur before the first window is ready, leaving the second launch unable to open it. | Recommended small fix. |
| 29 | The repeated build settings are reduced and CMake tests are optional. The alternate MSVC/CMake build has not been run here. | Verify only if IT uses that build route. |
| 31 | Shorten the long README and move detailed developer test instructions into a focused document. Changed production files now follow the existing formatter. | Optional presentation cleanup. |
| 32 | Replace playful rotating welcome messages with neutral wording if desired. Tiger Snip naming and the orange icon are already done. | Taste; not a deployment blocker. |

Signing and continuous integration remain deferred as you requested. Ten captures stay in local process memory until File > Exit; closing to the tray retains them. Those are intentional choices, not unfinished bugs.

For distribution, IT puts **Tiger Snip Setup.msi** on the shared drive. Each person opens it once; it installs the app and Start menu shortcut locally under their Windows account. Their preferences live separately in `%LOCALAPPDATA%\Tiger Snip\TigerSnip.ini`. An ordinary EXE runs directly; opening it does not automatically install it.

The warning-enabled build, installed-app desktop smoke test, fresh-process preference check, and tests for rendering, clipboard, protected PNG saves, per-user migration, damaged settings, interrupted preference writes/retry, bounded inputs, callbacks, graphics initialization/retry, and isolated checked reports pass. IT still needs to validate its installation policies and actual PC setup. The detailed open findings are in [IT-REVIEW.md](C:/Users/jackk/repos/jack-snip/IT-REVIEW.md).
