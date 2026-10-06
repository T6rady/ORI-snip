# Tiger Snip build record

The verified build route is Windows PowerShell/PowerShell with the x64 UCRT LLVM-MinGW 20260922 toolchain, containing Clang and LLVM resource tools 23.1.2. The headers, Windows import libraries, C++ runtime, and linker come from that archive; a separate Visual Studio SDK or CMake installation is not needed for this route. CMake/MSVC remains an alternate, unverified route here.

Official release: [LLVM-MinGW 20260922](https://github.com/mstorsjo/llvm-mingw/releases/tag/20260922).

Download: [llvm-mingw-20260922-ucrt-x86_64.zip](https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip).

Archive SHA256, checked against the publisher's GitHub release-asset digest on October 6, 2026:

```text
E3AD77D117A4BEA19A7A3B333341824D79A5A371004A10E25B8504E7B3047666
```

The local 190,725,905-byte archive matches. Extract it into `.tools/llvm-mingw-20260922-ucrt-x86_64`. `build.ps1` now selects this exact directory by default; a newer download cannot silently change the compiler. An explicit `-CompilerDirectory` is still supported and recorded.

From a fresh source checkout with the toolchain extracted, run:

```powershell
Get-FileHash '.tools/llvm-mingw-20260922-ucrt-x86_64.zip' -Algorithm SHA256
.\build.ps1 -Test
.\package.ps1
```

The first command should match the digest above. No compiler download or installation is performed by the build. The bundled runtime notices are copied to `dist` and included in the MSI. Runtime libraries are linked statically; Windows supplies its own system DLLs and UCRT.

`dist/Tiger Snip Build.json` records the actual compiler version and tool hashes, compile/link arguments, PowerShell version, base Git commit and uncommitted-state flag, EXE hash, and source/resource input hashes. `package.ps1` checks those hashes before packaging, so an older EXE cannot silently be packaged with changed source. `dist/Tiger Snip Release.txt` records package/payload hashes and references the build record. Text hashes are of actual working-file bytes; Git line-ending conversion can change them.

The build uses C++20, size optimization, compiler warnings, static linking, and DEP/ASLR/high-entropy-address flags. Exact flags are in the build record. Build date/time appears in diagnostic traces, and MSI package identifiers/timestamps vary, so equivalent rebuilds are not promised to have identical binary hashes.

Packaging uses Windows' `makecab.exe` and Windows Installer COM APIs. On the verified PC, makecab reports `5.00 (WinBuild.160101.0800)` and `msi.dll` reports `5.0.26100.9444`; these are OS components, not dependencies shipped with Tiger Snip. No release server, automatic updater, or CI system is involved.
