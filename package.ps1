param([string]$OutputPath)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
if (-not $OutputPath) { $OutputPath = Join-Path $taskRoot 'dist\Tiger Snip Setup.msi' }
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if ([IO.Path]::GetExtension($OutputPath) -ne '.msi') { throw 'OutputPath must name an MSI file.' }
New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($OutputPath)) | Out-Null
$taskStage = Join-Path $taskRoot 'build\package'
New-Item -ItemType Directory -Force -Path $taskStage | Out-Null
$taskPayload = @(
    @{ Id = 'AppExe'; Source = 'dist\Tiger Snip.exe'; Name = 'TIGERS~1.EXE|Tiger Snip.exe' },
    @{ Id = 'QuickStart'; Source = 'dist\Quick Start.txt'; Name = 'QUICKS~1.TXT|Quick Start.txt' },
    @{ Id = 'LLVMNotice'; Source = 'dist\LLVM.txt'; Name = 'LLVM.txt' },
    @{ Id = 'MinGWNotice'; Source = 'dist\MinGW-runtime.txt'; Name = 'MINGWR~1.TXT|MinGW-runtime.txt' }
)
foreach ($taskFile in $taskPayload) {
    $taskFile.Path = Join-Path $taskRoot $taskFile.Source
    if (-not (Test-Path -LiteralPath $taskFile.Path -PathType Leaf)) { throw "Missing $($taskFile.Source); run build.ps1 first." }
}
$taskVersionInfo = (Get-Item -LiteralPath $taskPayload[0].Path).VersionInfo
$taskBuildRecordPath = Join-Path $taskRoot 'dist\Tiger Snip Build.json'
if (-not (Test-Path -LiteralPath $taskBuildRecordPath)) { throw 'Run build.ps1 first to create the build record.' }
$taskBuildRecord = Get-Content -LiteralPath $taskBuildRecordPath -Raw | ConvertFrom-Json
if ($taskBuildRecord.formatVersion -ne 1 -or -not $taskBuildRecord.inputs.Count) {
    throw 'The build record is incomplete or unsupported. Run build.ps1 again.'
}
if ($taskBuildRecord.output.name -ne 'Tiger Snip.exe' -or
    $taskBuildRecord.output.sha256 -ne (Get-FileHash -LiteralPath $taskPayload[0].Path -Algorithm SHA256).Hash) {
    throw 'The executable does not match its build record. Run build.ps1 again.'
}
foreach ($taskInput in $taskBuildRecord.inputs) {
    if ($taskInput.sha256 -ne (Get-FileHash -LiteralPath (Join-Path $taskRoot $taskInput.path) -Algorithm SHA256).Hash) {
        throw "Build input changed after compilation: $($taskInput.path). Run build.ps1 again."
    }
}
$taskVersion = '{0}.{1}.{2}.{3}' -f $taskVersionInfo.FileMajorPart, $taskVersionInfo.FileMinorPart, $taskVersionInfo.FileBuildPart, $taskVersionInfo.FilePrivatePart
if ($taskVersion -ne '1.0.2.0') { throw 'This package definition expects Tiger Snip 1.0.2.0.' }

# A self-contained cabinet holds only the release files, never personal preferences.
$taskCab = Join-Path $taskStage 'payload.cab'
$taskDdf = Join-Path $taskStage 'payload.ddf'
$taskLines = @('.OPTION EXPLICIT', '.Set CabinetNameTemplate=payload.cab',
    ('.Set DiskDirectoryTemplate="' + $taskStage + '"'), '.Set CompressionType=LZX',
    '.Set Cabinet=ON', '.Set Compress=ON', '.Set MaxDiskSize=0',
    ('.Set RptFileName="' + (Join-Path $taskStage 'payload.rpt') + '"'),
    ('.Set InfFileName="' + (Join-Path $taskStage 'payload.inf') + '"')
)
$taskLines += $taskPayload | ForEach-Object { '"' + $_.Path + '" ' + $_.Id }
Set-Content -LiteralPath $taskDdf -Value $taskLines -Encoding ASCII
& (Join-Path $env:WINDIR 'System32\makecab.exe') /F $taskDdf > (Join-Path $taskStage 'makecab.log')
if ($LASTEXITCODE -ne 0) { throw 'Cabinet creation failed.' }

function Invoke-MsiCom($Object, [string]$Name, [object[]]$Arguments = @(), [switch]$Set, [switch]$Get) {
    $taskFlags = [Reflection.BindingFlags]::InvokeMethod
    if ($Set) { $taskFlags = [Reflection.BindingFlags]::SetProperty }
    if ($Get) { $taskFlags = [Reflection.BindingFlags]::GetProperty }
    try { $Object.GetType().InvokeMember($Name, $taskFlags, $null, $Object, $Arguments) }
    catch { throw "Windows Installer $Name failed: $($_.Exception.Message)" }
}
function Invoke-MsiSql([string]$Sql, [object[]]$Values = @()) {
    $taskView = Invoke-MsiCom $taskDatabase 'OpenView' @($Sql)
    try {
        if ($Values.Count) {
            $taskRecord = Invoke-MsiCom $taskInstaller 'CreateRecord' @($Values.Count)
            try {
                for ($taskIndex = 0; $taskIndex -lt $Values.Count; ++$taskIndex) {
                    if ($null -eq $Values[$taskIndex]) { continue }
                    $taskField = 'StringData'
                    if ($Values[$taskIndex] -is [int]) { $taskField = 'IntegerData' }
                    Invoke-MsiCom $taskRecord $taskField @(($taskIndex + 1), $Values[$taskIndex]) -Set
                }
                Invoke-MsiCom $taskView 'Execute' @($taskRecord)
            } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($taskRecord) }
        } else { Invoke-MsiCom $taskView 'Execute' }
        Invoke-MsiCom $taskView 'Close'
    } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($taskView) }
}

# Fixed product/component identities belong to this version; change ProductCode
# for a future major-upgrade package. PackageCode identifies this particular build.
$taskProductCode = '{58B5D5B9-F665-42BE-A14A-6B46EE0B5827}'
$taskInstaller = New-Object -ComObject WindowsInstaller.Installer
$taskDatabase = $null
try {
    if (Test-Path -LiteralPath $OutputPath) { Remove-Item -LiteralPath $OutputPath }
    $taskDatabase = Invoke-MsiCom $taskInstaller 'OpenDatabase' @($OutputPath, 3)
    $taskSchema = @(
        'CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` CHAR(0) LOCALIZABLE PRIMARY KEY `Property`)',
        'CREATE TABLE `Directory` (`Directory` CHAR(72) NOT NULL, `Directory_Parent` CHAR(72), `DefaultDir` CHAR(255) NOT NULL LOCALIZABLE PRIMARY KEY `Directory`)',
        'CREATE TABLE `Component` (`Component` CHAR(72) NOT NULL, `ComponentId` CHAR(38), `Directory_` CHAR(72) NOT NULL, `Attributes` SHORT NOT NULL, `Condition` CHAR(255), `KeyPath` CHAR(72) PRIMARY KEY `Component`)',
        'CREATE TABLE `Feature` (`Feature` CHAR(38) NOT NULL, `Feature_Parent` CHAR(38), `Title` CHAR(64) LOCALIZABLE, `Description` CHAR(255) LOCALIZABLE, `Display` SHORT, `Level` SHORT NOT NULL, `Directory_` CHAR(72), `Attributes` SHORT NOT NULL PRIMARY KEY `Feature`)',
        'CREATE TABLE `FeatureComponents` (`Feature_` CHAR(38) NOT NULL, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Feature_`, `Component_`)',
        'CREATE TABLE `File` (`File` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL LOCALIZABLE, `FileSize` LONG NOT NULL, `Version` CHAR(72), `Language` CHAR(20), `Attributes` SHORT, `Sequence` SHORT NOT NULL PRIMARY KEY `File`)',
        'CREATE TABLE `Media` (`DiskId` SHORT NOT NULL, `LastSequence` SHORT NOT NULL, `DiskPrompt` CHAR(64) LOCALIZABLE, `Cabinet` CHAR(255), `VolumeLabel` CHAR(32), `Source` CHAR(72) PRIMARY KEY `DiskId`)',
        'CREATE TABLE `Registry` (`Registry` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL LOCALIZABLE, `Name` CHAR(255) LOCALIZABLE, `Value` CHAR(0) LOCALIZABLE, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Registry`)',
        'CREATE TABLE `Shortcut` (`Shortcut` CHAR(72) NOT NULL, `Directory_` CHAR(72) NOT NULL, `Name` CHAR(128) NOT NULL LOCALIZABLE, `Component_` CHAR(72) NOT NULL, `Target` CHAR(255) NOT NULL LOCALIZABLE, `Arguments` CHAR(255) LOCALIZABLE, `Description` CHAR(255) LOCALIZABLE, `Hotkey` SHORT, `Icon_` CHAR(72), `IconIndex` SHORT, `ShowCmd` SHORT, `WkDir` CHAR(72) PRIMARY KEY `Shortcut`)',
        'CREATE TABLE `RemoveFile` (`FileKey` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) LOCALIZABLE, `DirProperty` CHAR(72) NOT NULL, `InstallMode` SHORT NOT NULL PRIMARY KEY `FileKey`)',
        'CREATE TABLE `InstallExecuteSequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)',
        'CREATE TABLE `LaunchCondition` (`Condition` CHAR(255) NOT NULL, `Description` CHAR(255) NOT NULL LOCALIZABLE PRIMARY KEY `Condition`)'
    )
    foreach ($taskSql in $taskSchema) { Invoke-MsiSql $taskSql }
    $taskProperties = [ordered]@{
        ProductCode = $taskProductCode; ProductName = 'Tiger Snip'; ProductVersion = '1.0.2'
        ProductLanguage = '1033'; Manufacturer = 'Jack Kempf'
        UpgradeCode = '{74410596-744A-4B20-8FBA-F55363372B9A}'; INSTALLLEVEL = '1'
        ARPNOMODIFY = '1'; ARPCOMMENTS = 'Local screenshot capture and annotation utility.'
    }
    foreach ($taskProperty in $taskProperties.GetEnumerator()) {
        Invoke-MsiSql 'INSERT INTO `Property` (`Property`, `Value`) VALUES (?, ?)' @($taskProperty.Key, $taskProperty.Value)
    }
    foreach ($taskRow in @(
        @('TARGETDIR', $null, 'SourceDir'), @('LocalAppDataFolder', 'TARGETDIR', '.'),
        @('ProgramsFolder', 'LocalAppDataFolder', 'Programs'), @('INSTALLFOLDER', 'ProgramsFolder', 'TIGERS~1|Tiger Snip'),
        @('ProgramMenuFolder', 'TARGETDIR', '.'), @('TigerMenu', 'ProgramMenuFolder', 'TIGERS~1|Tiger Snip')
    )) { Invoke-MsiSql 'INSERT INTO `Directory` (`Directory`, `Directory_Parent`, `DefaultDir`) VALUES (?, ?, ?)' $taskRow }
    Invoke-MsiSql 'INSERT INTO `Component` (`Component`, `ComponentId`, `Directory_`, `Attributes`, `KeyPath`) VALUES (?, ?, ?, ?, ?)' @('AppFiles', '{ADAB546A-CE97-4DED-8934-F080D67BB0C7}', 'INSTALLFOLDER', 260, 'AppInstallDir')
    Invoke-MsiSql 'INSERT INTO `Feature` (`Feature`, `Title`, `Display`, `Level`, `Directory_`, `Attributes`) VALUES (?, ?, ?, ?, ?, ?)' @('Main', 'Tiger Snip', 1, 1, 'INSTALLFOLDER', 0)
    Invoke-MsiSql 'INSERT INTO `FeatureComponents` (`Feature_`, `Component_`) VALUES (?, ?)' @('Main', 'AppFiles')
    $taskSequence = 0
    foreach ($taskFile in $taskPayload) {
        ++$taskSequence
        $taskFileVersion = $null
        if ($taskFile.Id -eq 'AppExe') { $taskFileVersion = $taskVersion }
        Invoke-MsiSql 'INSERT INTO `File` (`File`, `Component_`, `FileName`, `FileSize`, `Version`, `Attributes`, `Sequence`) VALUES (?, ?, ?, ?, ?, ?, ?)' @($taskFile.Id, 'AppFiles', $taskFile.Name, [int](Get-Item -LiteralPath $taskFile.Path).Length, $taskFileVersion, 16384, $taskSequence)
    }
    Invoke-MsiSql 'INSERT INTO `Media` (`DiskId`, `LastSequence`, `Cabinet`) VALUES (?, ?, ?)' @(1, $taskSequence, '#payload.cab')
    Invoke-MsiSql 'INSERT INTO `Registry` (`Registry`, `Root`, `Key`, `Name`, `Value`, `Component_`) VALUES (?, ?, ?, ?, ?, ?)' @('AppInstallDir', 1, 'Software\Tiger Snip', 'InstallDir', '[INSTALLFOLDER]', 'AppFiles')
    Invoke-MsiSql 'INSERT INTO `Shortcut` (`Shortcut`, `Directory_`, `Name`, `Component_`, `Target`, `Description`, `ShowCmd`, `WkDir`) VALUES (?, ?, ?, ?, ?, ?, ?, ?)' @('StartMenu', 'TigerMenu', 'TIGERS~1|Tiger Snip', 'AppFiles', '[INSTALLFOLDER]Tiger Snip.exe', 'Capture and annotate screenshots', 1, 'INSTALLFOLDER')
    foreach ($taskFolder in @('INSTALLFOLDER', 'TigerMenu')) {
        Invoke-MsiSql 'INSERT INTO `RemoveFile` (`FileKey`, `Component_`, `DirProperty`, `InstallMode`) VALUES (?, ?, ?, ?)' @($taskFolder, 'AppFiles', $taskFolder, 2)
    }
    Invoke-MsiSql 'INSERT INTO `LaunchCondition` (`Condition`, `Description`) VALUES (?, ?)' @('VersionNT64', 'Tiger Snip requires 64-bit Windows.')
    $taskActions = [ordered]@{
        LaunchConditions = 100; CostInitialize = 800; FileCost = 900; CostFinalize = 1000
        InstallValidate = 1400; InstallInitialize = 1500; ProcessComponents = 1600
        UnpublishFeatures = 1800; RemoveRegistryValues = 2600; RemoveShortcuts = 3200
        RemoveFiles = 3500; RemoveFolders = 3600; CreateFolders = 3700; InstallFiles = 4000
        CreateShortcuts = 4500; WriteRegistryValues = 5000; RegisterUser = 6000
        RegisterProduct = 6100; PublishFeatures = 6300; PublishProduct = 6400; InstallFinalize = 6600
    }
    foreach ($taskAction in $taskActions.GetEnumerator()) {
        Invoke-MsiSql 'INSERT INTO `InstallExecuteSequence` (`Action`, `Sequence`) VALUES (?, ?)' @($taskAction.Key, [int]$taskAction.Value)
    }
    $taskStream = Invoke-MsiCom $taskInstaller 'CreateRecord' @(2)
    $taskView = Invoke-MsiCom $taskDatabase 'OpenView' @('INSERT INTO `_Streams` (`Name`, `Data`) VALUES (?, ?)')
    try {
        Invoke-MsiCom $taskStream 'StringData' @(1, 'payload.cab') -Set
        Invoke-MsiCom $taskStream 'SetStream' @(2, [string]$taskCab)
        Invoke-MsiCom $taskView 'Execute' @($taskStream)
        Invoke-MsiCom $taskView 'Close'
    } finally {
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($taskView)
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($taskStream)
    }
    $taskSummary = Invoke-MsiCom $taskDatabase 'SummaryInformation' @(20) -Get
    try {
        foreach ($taskPair in @(@(1, 1252), @(2, 'Tiger Snip Setup'), @(3, 'Tiger Snip 1.0.2'),
            @(4, 'Jack Kempf'), @(7, 'x64;1033'), @(9, ('{' + [guid]::NewGuid().ToString().ToUpperInvariant() + '}')),
            @(14, 500), @(15, 10), @(18, 'Windows Installer'), @(19, 2))) {
            Invoke-MsiCom $taskSummary 'Property' $taskPair -Set
        }
        Invoke-MsiCom $taskSummary 'Persist'
    } finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($taskSummary) }
    Invoke-MsiCom $taskDatabase 'Commit'
} finally {
    if ($taskDatabase) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($taskDatabase) }
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($taskInstaller)
}
Get-Item -LiteralPath $OutputPath | Select-Object FullName, Length
$taskHashes = @('Tiger Snip 1.0.2 release information',
    ('Packaged: ' + [DateTime]::UtcNow.ToString('yyyy-MM-dd HH:mm:ss') + ' UTC'),
    'Distribution file: Tiger Snip Setup.msi',
    'Installation: current user; local app and Start menu shortcut; no automatic startup.',
    'Settings: current user LocalAppData; personal preferences are excluded from this package.',
    'Build: C++20, static runtime; package authored with Windows Installer and makecab.',
    ('Compiler: ' + $taskBuildRecord.compilerVersion[0]),
    ('Executable built: ' + $taskBuildRecord.builtAtUtc),
    'Build record: Tiger Snip Build.json (compiler/tool hashes, options, source-input and EXE hashes).',
    'Validated toolchain provenance: BUILD-TOOLCHAIN.md in the source repository.',
    'Signing: unsigned. IT deployment policy remains to be validated.')
$taskHashes += 'Source base commit at build time: ' + $taskBuildRecord.sourceBaseCommit
$taskHashes += 'Working tree had uncommitted changes at build time: ' + $taskBuildRecord.workingTreeHasUncommittedChanges
$taskHashes += 'A source archive without Git metadata records those fields as unknown; input hashes still identify the actual files.'
$taskHashes += @('', 'SHA256:')
foreach ($taskArtifact in @($OutputPath, $taskBuildRecordPath) + @($taskPayload.Path)) {
    $taskHash = Get-FileHash -LiteralPath $taskArtifact -Algorithm SHA256
    $taskHashes += $taskHash.Hash + '  ' + [IO.Path]::GetFileName($taskArtifact)
}
$taskHashes += @('', 'Build input SHA256:')
foreach ($taskInput in @('build.ps1', 'package.ps1', 'CMakeLists.txt', 'scripts\run-test.ps1', 'scripts\write-build-record.ps1') +
    @((Get-ChildItem -LiteralPath (Join-Path $taskRoot 'src') -File | Sort-Object Name |
        ForEach-Object { 'src\' + $_.Name })) + @('resources\app.rc',
    'resources\app.manifest', 'resources\tiger-snip.ico', 'resources\app-icon.png', 'scripts\make-icon.ps1')) {
    $taskInputPath = Join-Path $taskRoot $taskInput
    if (Test-Path -LiteralPath $taskInputPath) {
        $taskHashes += (Get-FileHash -LiteralPath $taskInputPath -Algorithm SHA256).Hash + '  ' + $taskInput
    }
}
Set-Content -LiteralPath (Join-Path ([IO.Path]::GetDirectoryName($OutputPath)) 'Tiger Snip Release.txt') -Value $taskHashes -Encoding UTF8
Write-Output ('Installer SHA256: ' + (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash)
