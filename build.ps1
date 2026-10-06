param([string]$CompilerDirectory, [switch]$Test, [string]$OutputName = 'Snipper.exe')
$ErrorActionPreference = 'Stop'
if ($OutputName -notmatch '^[A-Za-z0-9._-]+\.exe$') { throw 'OutputName must be an executable filename.' }
$taskRoot = $PSScriptRoot
if (-not $CompilerDirectory) {
    $taskPortable = Get-ChildItem -LiteralPath (Join-Path $taskRoot '.tools') -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like 'llvm-mingw-*' } | Sort-Object Name -Descending | Select-Object -First 1
    if ($taskPortable) { $CompilerDirectory = Join-Path $taskPortable.FullName 'bin' }
}
if (-not $CompilerDirectory -or -not (Test-Path -LiteralPath (Join-Path $CompilerDirectory 'clang++.exe'))) {
    throw 'Provide -CompilerDirectory with an LLVM-MinGW bin folder, or use CMake with Visual Studio C++ Build Tools. See README.md.'
}
$taskCompiler = Join-Path $CompilerDirectory 'clang++.exe'
$taskResourceCompiler = Join-Path $CompilerDirectory 'llvm-windres.exe'
$taskBuild = Join-Path $taskRoot 'build'
$taskDist = Join-Path $taskRoot 'dist'
New-Item -ItemType Directory -Force -Path $taskBuild, $taskDist | Out-Null
$taskIcon = Join-Path $taskRoot 'resources\snipper.ico'
if (-not (Test-Path -LiteralPath $taskIcon)) { & (Join-Path $taskRoot 'scripts\make-icon.ps1') }
Push-Location (Join-Path $taskRoot 'resources')
try {
    & $taskResourceCompiler -i app.rc -o (Join-Path $taskBuild 'app.res.o') -O coff
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }
} finally { Pop-Location }
$taskArguments = @('-std=c++20','-Os','-Wall','-Wextra','-Wpedantic','-DUNICODE','-D_UNICODE','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX','-D_WIN32_WINNT=0x0A00',
    (Join-Path $taskRoot 'src\main.cpp'),(Join-Path $taskRoot 'src\model.cpp'),(Join-Path $taskRoot 'src\graphics.cpp'),(Join-Path $taskBuild 'app.res.o'),
    '-o',(Join-Path $taskDist $OutputName),'-municode','-mwindows','-static','-Wl,--nxcompat','-Wl,--dynamicbase','-Wl,--high-entropy-va','-s',
    '-ld2d1','-ldwrite','-lwindowscodecs','-lole32','-luser32','-lgdi32','-lcomdlg32','-lcomctl32','-lshell32','-ladvapi32','-ldwmapi')
& $taskCompiler @taskArguments
if ($LASTEXITCODE -ne 0) { throw 'C++ compilation failed.' }
Get-Item -LiteralPath (Join-Path $taskDist $OutputName) | Select-Object FullName, Length, LastWriteTime
Copy-Item -LiteralPath (Join-Path $taskRoot 'resources\licenses\LLVM.txt'), (Join-Path $taskRoot 'resources\licenses\MinGW-runtime.txt') -Destination $taskDist
if ($Test) {
    $taskClipboardArguments = @('-std=c++20','-Os','-Wall','-Wextra','-Wpedantic','-DUNICODE','-D_UNICODE','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX','-D_WIN32_WINNT=0x0A00',
        '-I',(Join-Path $taskRoot 'src'),(Join-Path $taskRoot 'tests\clipboard.cpp'),(Join-Path $taskRoot 'src\model.cpp'),(Join-Path $taskRoot 'src\graphics.cpp'),(Join-Path $taskBuild 'app.res.o'),
        '-o',(Join-Path $taskBuild 'clipboard_test.exe'),'-municode','-static','-s','-ld2d1','-ldwrite','-lwindowscodecs','-lole32','-luser32','-lgdi32')
    & $taskCompiler @taskClipboardArguments
    if ($LASTEXITCODE -ne 0) { throw 'Clipboard test compilation failed.' }
    $taskAutoCopyArguments = @($taskArguments)
    $taskAutoCopyArguments[$taskAutoCopyArguments.IndexOf((Join-Path $taskRoot 'src\main.cpp'))] = Join-Path $taskRoot 'tests\autocopy.cpp'
    $taskAutoCopyArguments[$taskAutoCopyArguments.IndexOf((Join-Path $taskDist $OutputName))] = Join-Path $taskBuild 'autocopy_test.exe'
    $taskAutoCopyArguments = @($taskAutoCopyArguments | Where-Object { $_ -ne '-mwindows' })
    & $taskCompiler @taskAutoCopyArguments
    if ($LASTEXITCODE -ne 0) { throw 'Auto copy test compilation failed.' }
    Push-Location $taskBuild
    try {
        $taskProcess = Start-Process -FilePath (Join-Path $taskDist $OutputName) -ArgumentList '--self-test' -WindowStyle Hidden -Wait -PassThru
        Get-Content -LiteralPath 'self-test-results.txt'
        if ($taskProcess.ExitCode -ne 0) { throw 'Native self-test failed.' }
        & (Join-Path $taskBuild 'clipboard_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Isolated clipboard test failed.' }
        & (Join-Path $taskBuild 'autocopy_test.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Isolated auto copy test failed.' }
    } finally { Pop-Location }
}
