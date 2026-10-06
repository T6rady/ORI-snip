# Tiger Snip local verification — October 6, 2026

The current local source includes items 22, 20, 28, and 5. All checks below passed on Windows 11 x64 (10.0.26200), using the documented LLVM-MinGW toolchain. The installer operations ran without an elevated token. This is local evidence for IT review, not approval of corporate deployment policies.

- `build.ps1 -Test`: warning-enabled production/test compilation; native model/rendering/PNG checks; private clipboard; auto-copy; protected PNG saves and interrupted-save recovery; independent-profile settings/migration; robustness/failure paths; focused text appearance/export/performance; simultaneous-process launch forwarding. Eight suites, all successful.
- Save tests: forced termination after staging, original/private DACL preserved, crash temporary pixels and private DACL retained, later save succeeds, older staging/recovery files untouched, current-attempt exception cleanup, locked/read-only preservation, and recovery-name collision protection.
- Launch tests: three independent sender processes wait for a deliberately delayed window, two Open and one Snip request arrive once each, existing-window forwarding succeeds, and a missing window gives a bounded error. Private desktop/window station; no ordinary settings or app state changed.
- Text tests: all **55** scene/native-field/export PNGs still match the original source build byte for byte. All field-background pixels and cached preview pixels match complete rendering. Actual PNG/DIB clipboard content matches final export. Current median/p95 synchronous character-handler timings: plain **2.47/2.79 ms**, boxed **3.43/3.99 ms**, boxed with border/logo **4.53/4.75 ms**. Original corresponding medians: 46.24, 46.45, and 111.89 ms. Fixture: 30 characters, 1050×740 window, 3840×2160 source, private software-rendering desktop. Later queued paint is outside these timings.
- `package.ps1`: current executable/input hashes match the build record. Deliberately altered output and input checksums are rejected before changing the existing MSI; the original record was restored byte for byte.
- MSI removal/installation: both exit codes **0**. Installed EXE SHA256 matches the tested distribution EXE. Both local/personal INIs were unchanged through the installer operations.
- Installed EXE `--smoke-test --software-rendering`: passed. Covers actual editor/capture, annotation/text/crop/navigation/history, ten recent captures, native plain/boxed/multiline backgrounds, renderer switches, settings, and actual PNG saving.
- Installed EXE `--verify-smoke-preferences` with the explicit producer fixture: passed after full process exit.
- This PC still has `SoftwareRendering=1` in both `dist/TigerSnip.ini` and `%LOCALAPPDATA%\Tiger Snip\TigerSnip.ini`. Fresh-user defaults and `RecentLimit = 10` are unchanged.
- Current PE flags still include DEP (`NX_COMPAT`), ASLR (`DYNAMIC_BASE`), and high-entropy address space (`HIGH_ENTROPY_VA`). CFG/stack-protection assessment remains open item 6.

Current EXE SHA256:

```text
62148BE247F7146B9424CF4A91818C0E602E6DE2B681D7661235BB4FECA8D520
```

Current MSI SHA256:

```text
7691825C6FEB271F1ED793BC4B2ED1E9C23D91ACEF1C9D3948EA59B38F5B2489
```

Exact compiler/options/source-input/payload hashes are in `dist/Tiger Snip Build.json` and `dist/Tiger Snip Release.txt`. The build records retain the base Git commit `0b1433f0bcd705bce58c0f5b01bec61acbd2831c` and dirty working-tree state at build time. The current repository revision includes the tested fixes and release artifacts; use the input hashes to identify the exact compiled source bytes.

Local artifacts/logs remain ignored: `build/test-output/1cd3b5d9-6509-423a-9464-ac95475a9f16`, `build/it-final-smoke`, `build/it-final-restart`, `build/it-final-install.log`, and `build/it-final-uninstall.log`. They can contain desktop images and are excluded from the review/distribution bundle.

Not validated here: actual corporate share/account/endpoint policy, other physical PCs or mixed-monitor fleets, RDP, MSVC/CMake, screen-reader requirements, a sanitizer run, packet capture, or a complete security audit. Previous hardware stalls have no established driver/compositor cause. IT's small pilot and deployment approval remain necessary.
