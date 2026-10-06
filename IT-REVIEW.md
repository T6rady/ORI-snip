# Tiger Snip: open review items

Updated October 6, 2026. Only unfinished items remain below; original numbers are retained for selection. Completed fixes have been removed. The scope is a basic internal utility: no broad architecture rewrite or continuous-release system is proposed. Signing and automated CI were deferred; ten recent captures remain an intentional requirement.

The current build has separate settings, Windows helpers, color picker, capture, clipboard, file saving, and test-report modules. Palette validation, registry bounds, callback containment, transactional graphics initialization, settings transactions/retry, and checked isolated reports are implemented. The MSI installs locally per user and excludes personal INIs and captures. The app uses the Tiger Snip name and orange icon. Keep this PC's software-rendering preference; fresh users retain the normal default.

The user selected **22 (slow text-box editing)** as the next priority. Reproduce the measured typing cost, reduce unnecessary full-window rendering/readback, and verify before/after timing and unchanged behavior. Later, the recommended small batch is **12** (move developer tests out of the release app), **20** (interrupted-save leftovers), and **28** (rapid second launch). Complete **8 and 27** with a short owner/support note and IT pilot. Keep other work proportionate to actual requirements and measurements.

**5. Record the release build toolchain — Low; provenance documentation.**

Evidence: [build tool discovery](C:/Users/jackk/repos/jack-snip/build.ps1) picks the lexically newest matching local LLVM-MinGW directory, and [build instructions](C:/Users/jackk/repos/jack-snip/README.md) link to a changing release list. Record the supported compiler/SDK/CMake versions and verified toolchain download hashes. Add a repeatable clean-build configuration and release manifest containing commit, tools, options, and output hashes. The current source embeds build date/time in diagnostic traces, so bit-identical builds need deliberate timestamp handling. My rebuild differs in hash from the committed EXE; that alone does not prove source mismatch. Acceptance: a reviewer can reproduce a release using documented inputs and understand any expected binary differences.


**6. Make binary hardening explicit and verify the resulting EXE — Medium; defense in depth.**

Evidence: [PowerShell linker flags](C:/Users/jackk/repos/jack-snip/build.ps1) set DEP/ASLR/high-entropy VA; [CMake options](C:/Users/jackk/repos/jack-snip/CMakeLists.txt) now use the same LLVM linker protections. The shipped PE advertises those three protections, but does not advertise `GUARD_CF` in DLL characteristics, has no CFG function table, and reports a zero SecurityCookie field. This is not proof of an exploitable memory bug. Enable supported stack-protection and CFG options consistently for the selected compiler and inspect the final artifact, rather than assuming flags worked. Microsoft documents that [CFG requires compiler and linker support](https://learn.microsoft.com/en-us/cpp/build/reference/guard-enable-control-flow-guard?view=msvc-170). Keep compatible release symbols separately rather than stripping every support artifact.


**8. Document ownership, runtime inventory, and approved brand assets — High / Policy.**

Evidence: the repository has runtime notices but no project LICENSE/ownership statement or dependency inventory. [Samtec asset notes](C:/Users/jackk/repos/jack-snip/resources/samtec-logo.md) and [additional asset notes](C:/Users/jackk/repos/jack-snip/resources/samtec-marks.md) explicitly describe image-generated cutouts from supplied artwork. Obtain the appropriate internal distribution/ownership decision and list embedded assets and statically linked runtimes with versions and notices. Prefer official approved transparent brand files over reconstructed cutouts; record their provenance and authorization. Preserve honest provenance records. This review does not establish that any existing notice or brand use violates a license. Acceptance: IT can identify who owns/supports the application and where every distributed component and brand asset came from.


**11. Decide clipboard-history and synchronization behavior — Medium / Policy.**

Evidence: [clipboard export](C:/Users/jackk/repos/jack-snip/src/clipboard.cpp) publishes PNG/DIB formats without history/synchronization control formats; [automatic copying](C:/Users/jackk/repos/jack-snip/src/main.cpp) defaults on. IT should decide whether screenshots may enter Windows clipboard history or synchronization. If restriction is required, add the supported exclusion formats and verify them with the deployment policy. Microsoft documents [clipboard history and synchronization control formats](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats). Do not present these controls as protection against every clipboard-reading process, and do not silently change the personal default merely to satisfy a speculative reviewer preference.


**12. Separate integration tests and screenshot-producing diagnostics from the release application — Medium; architecture and distribution improvement.**

Evidence: [large test block](C:/Users/jackk/repos/jack-snip/src/main.cpp), [production argument dispatch](C:/Users/jackk/repos/jack-snip/src/main.cpp), embedded model/graphics tests, and [test including main.cpp](C:/Users/jackk/repos/jack-snip/tests/autocopy.cpp). The shipping executable contains thousands of lines of test drivers. Certain explicit test flags perform real desktop captures and write images in isolated run directories. Move tests into test targets linked against shared application code; ship only intentionally supported diagnostics, with explicit output locations. Preserve performance tracing if useful, and document its scope. Acceptance: production behavior is readable independently of test scaffolding and ordinary release builds exclude fixture/capture-test code. This is not a claim that normal launch secretly writes screenshots.


**17. Expand RAII to Win32 handles and ownership transitions — Medium; maintainability/failure-safety improvement.**

Evidence: [Application raw handles](C:/Users/jackk/repos/jack-snip/src/main.cpp), [captureDesktop cleanup](C:/Users/jackk/repos/jack-snip/src/capture.cpp), [saveBytes handle](C:/Users/jackk/repos/jack-snip/src/file_io.cpp), and palette/menu/GDI rendering helpers. COM already has a small owning wrapper. Add similarly focused wrappers for file/registry handles, DCs, selected GDI objects, menus, and temporary files, using the correct corresponding release API. Audit allocation-after-acquisition paths. This is a cleanup recommendation rather than a claim that every normal path leaks. Acceptance: failure paths preserve the original image and release acquired resources without sprawling manual cleanup branches.


**18. Consolidate COM and graphics shutdown lifetime — Low; cleanup improvement.**

Evidence: [manual shutdown](C:/Users/jackk/repos/jack-snip/src/main.cpp) now includes `labelFont`, but still depends on manually ordered releases and window-destruction cleanup before `CoUninitialize`. Scope COM initialization around the full application/resource lifetime and release all objects consistently before leaving that scope. Apply the same discipline to the auto-copy test's global application state. No shutdown crash was reproduced; some DirectWrite objects have lifetime/threading behavior that makes this less severe than an apartment-bound resource violation. Acceptance: cleanup remains correct after both successful and partially failed startup.


**20. Improve interrupted-save temporary-file handling — Medium; privacy/reliability improvement.**

Evidence: [temporary filename](C:/Users/jackk/repos/jack-snip/src/file_io.cpp) uses only the destination plus process ID and `CREATE_NEW`. Normal failure deletes the file, but termination can leave an entire PNG behind. PID reuse can also make that name block a later save. Use unique temporary names with scoped cleanup and a carefully defined stale-file policy; avoid sweeping arbitrary neighboring files. Consider directory/file permissions for temporary content as part of item 1. Acceptance: interrupted saves preserve the existing output, a leftover does not prevent a later save, and residual-data behavior is documented.


**21. Bound aggregate memory, not only individual bitmaps — Medium; resource-exhaustion risk.**

Evidence: [bitmap size limit](C:/Users/jackk/repos/jack-snip/src/model.cpp) permits 128 million pixels, approximately 512 MB for one bitmap; [Recent limit](C:/Users/jackk/repos/jack-snip/src/main.cpp) bounds count to ten, not bytes. Full capture/dim buffers, source crops, previews, GPU copies, and multiple clipboard formats add more memory. [Document history](C:/Users/jackk/repos/jack-snip/src/model.cpp) copies complete annotation lists up to 50 edits, and point/annotation counts are unbounded. Preserve the required ten recent captures. Measure representative workloads, guard extreme capture/annotation allocations if needed, and handle failures before destructive state transitions. Do not silently reduce retained capture count. Stress representative multi-monitor captures and many long strokes. This is not an external image-upload vulnerability: the app has no identified ordinary image-import path.


**22. Optimize inline text editing using the measured hot path — Medium; measured performance issue.**

Evidence: [EN_CHANGE handler](C:/Users/jackk/repos/jack-snip/src/main.cpp) calls [syncTextEditor](C:/Users/jackk/repos/jack-snip/src/main.cpp), which invokes [renderEditorPreview](C:/Users/jackk/repos/jack-snip/src/main.cpp) to repaint/read back the entire window for the text field's background. In the stated synthetic benchmark, each character cost median 45.73 ms and p95 48.27 ms. Render/cache only the required region or the stable backing layer, reuse suitable resources, and avoid duplicate full-image exports during a single edit. Verify plain/boxed text, shrinking/growing input, colors, crop/zoom/DPI and final export after the optimization. Use before/after timings. The measured software resize path was fast, so do not attribute this work to an unestablished driver diagnosis.


**23. Add performance budgets for drawing and exports before optimizing broadly — Medium; engineering improvement.**

Evidence: [previewImage](C:/Users/jackk/repos/jack-snip/src/main.cpp) compares and duplicates complete annotation lists and regenerates the export on changes; [drawAnnotations](C:/Users/jackk/repos/jack-snip/src/graphics.cpp) builds geometries/layouts repeatedly; expensive capture/export/PNG/filesystem operations execute on the UI thread. The simple 4K auto-copy fixture completed in about 120 ms, and tested software resizes stayed below 18 ms. Long-stroke, effect-heavy, maximum-size and slow-network-save workloads were not benchmarked. Profile those cases, set budgets, then use explicit revision/cache invalidation and targeted resource reuse or background work where measured results warrant it. Preserve event-driven rendering and verify exported pixels. Avoid speculative threads or cache frameworks that increase complexity without evidence.


**25. Continue organization where it helps the next change — Low; partially completed.**

Settings, startup/error helpers, the color picker, capture, clipboard, file saving, and test reports now have focused source/header files. The editor and diagnostic drivers remain in [main.cpp](C:/Users/jackk/repos/jack-snip/src/main.cpp). The next useful split is item 12. Afterward, separate UI/input or session history only where that makes a concrete change easier to inspect. Removing every global or introducing a new app framework is unnecessary for this release.

**26. Assess and improve accessibility of custom controls — Medium / Policy.**

Evidence: [toolbar construction](C:/Users/jackk/repos/jack-snip/src/main.cpp), [owner-painted UI](C:/Users/jackk/repos/jack-snip/src/main.cpp), and [spectrum control](C:/Users/jackk/repos/jack-snip/src/color_picker.h). Toolbar buttons/swatches and Recent thumbnails are painted inside the main window, with no identified UI Automation provider or `WM_GETOBJECT` handling for these controls. Menus/shortcuts help, but tooltips alone do not establish screen-reader accessibility. Test keyboard-only operation, accessible names/states, focus indication, Narrator and Windows contrast themes. Use native controls or a focused accessibility provider as appropriate. IT should decide the required accessibility standard; actual screen-reader behavior was not tested in this review.


**27. Define supported environments and deployment/update/removal procedures — Medium / Policy.**

Evidence: [README](C:/Users/jackk/repos/jack-snip/README.md), [Windows target definition](C:/Users/jackk/repos/jack-snip/CMakeLists.txt), [startup registration](C:/Users/jackk/repos/jack-snip/src/main.cpp), and portable launch script. Specify supported Windows versions/architectures, deployment location, permissions, installation/update/removal steps, how to disable startup, and rollback/support ownership. Validate a standard non-admin corporate account, an approved network distribution/install model, endpoint controls, remote sessions if needed, physical mixed-DPI monitors and unavailable save shares. Document that running from a network share and saving to one have different requirements. If IT needs locked defaults for startup/clipboard/history/save destinations, add an explicit managed policy layer rather than treating writable user preferences as policy enforcement.


**28. Fix the second-launch race and document instance scope — Low; source-level robustness issue.**

Evidence: [single-instance startup](C:/Users/jackk/repos/jack-snip/src/main.cpp) creates the mutex before the main window. A second process can see the mutex, fail to find the not-yet-created window, and return success without opening/snipping. Use bounded readiness/handshake handling and verify rapid simultaneous launches. The mutex is session-local with a fixed name; document the intended per-user/per-session behavior and handle access errors appropriately. No cross-user security bypass was demonstrated. Keep supported diagnostic instances clearly distinguishable from normal single-instance launches.


**29. Validate the alternate build route — Low; build duplication reduced.**

[CMake](C:/Users/jackk/repos/jack-snip/CMakeLists.txt) now applies one set of options/libraries to its targets and supports BUILD_TESTING=OFF. [PowerShell](C:/Users/jackk/repos/jack-snip/build.ps1) uses a shared module source list. Test runs use fresh folders. LLVM-MinGW is verified here; MSVC/CMake is still unverified because those tools are unavailable on PATH. If IT will rebuild with MSVC, validate that route and inspect its finished binary. No new release service is needed.

**31. Simplify documentation and enforce the existing formatting convention — Low; reviewer-facing cleanup.**

Evidence: [README](C:/Users/jackk/repos/jack-snip/README.md) is a long combined user manual/test catalog, and [.clang-format](C:/Users/jackk/repos/jack-snip/.clang-format) is present without an automated formatting check. Keep a concise overview, build/test entry points, supported scope, security/data-handling facts, and contribution/support information; move detailed tools and test procedures into focused documents. Keep necessary technical explanations, remove repeated marketing/pixel-perfect claims where tests or limitations are clearer, and avoid stale release-specific text. Apply the existing formatter consistently with a reviewable formatting-only change, separate from behavioral fixes. Do not add boilerplate documents without an owner or purpose.


**32. Choose a neutral corporate presentation if desired — Low; optional polish.**

Evidence: [rotating welcome headlines](C:/Users/jackk/repos/jack-snip/src/main.cpp) and [About text](C:/Users/jackk/repos/jack-snip/src/main.cpp). The slogans are workplace-friendly, but a neutral welcome and a clear version/support identity may suit managed distribution. Decide with the intended audience; this is taste, not a code-security defect. Keep official branding consistent with item 8. An AI reviewer may dislike tone, but tone cleanup should not take priority over the reproduced bugs and deployment evidence.


## Verification and limits

The warning-enabled LLVM-MinGW build, native rendering/model tests, private clipboard and auto-copy tests, PNG-permission tests, independent-profile migration tests, and targeted failure tests pass. Tests cover palette limits/malformed values, invalid registry strings, standard/unknown/recovery exceptions at callback boundaries, late graphics initialization failure/retry, Windows error codes, interrupted settings updates, pending-save retry, damaged/read-only preferences, and checked reports. Each invocation creates a unique output directory. Release-only builds contain no fault injection hooks.

The rebuilt MSI was removed/reinstalled successfully, and its installed EXE hash matches the tested release. The installed-app desktop suite and fresh-process preference restoration passed. Personal preferences, including this PC's SoftwareRendering=1, remain intact. IT still needs to validate its standard-user policies, endpoint tools, shared-drive distribution, and representative PCs/monitors. No MSVC build, RDP fleet test, enterprise EDR/DLP test, sanitizer run, or packet capture is claimed. Earlier desktop testing exposed a timing-sensitive I-beam/arrow cursor comparison; subsequent runs passed. These tests do not establish the cause of earlier hardware-rendering stalls.

The original review followed git pull --ff-only from dedcd81 to a3bca24. The completed source, release artifacts, and handoff are being committed and pushed together. The release record contains matching installer/payload/source-input hashes; deployment still needs IT's own approval.
