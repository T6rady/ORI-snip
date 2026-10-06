param([string]$SourcePath = (Join-Path $PSScriptRoot '..\resources\app-icon.png'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force -Path (Join-Path $PSScriptRoot '..\build') | Out-Null
if (-not (Test-Path -LiteralPath $SourcePath)) { throw "Icon source image missing: $SourcePath" }
$taskSourceImage = [System.Drawing.Image]::FromFile((Resolve-Path -LiteralPath $SourcePath).Path)
$taskFrames = @()
foreach ($taskSize in @(16, 24, 32, 48, 64, 128, 256)) {
    $taskBitmap = [System.Drawing.Bitmap]::new($taskSize, $taskSize, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $taskGraphics = [System.Drawing.Graphics]::FromImage($taskBitmap)
    $taskGraphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $taskGraphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $taskGraphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $taskGraphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $taskGraphics.Clear([System.Drawing.Color]::Transparent)
    # Preserve the supplied artwork and alpha; only resize to Windows icon sizes.
    $taskRatio = [Math]::Min($taskSize / $taskSourceImage.Width, $taskSize / $taskSourceImage.Height)
    $taskWidth = [single]($taskSourceImage.Width * $taskRatio)
    $taskHeight = [single]($taskSourceImage.Height * $taskRatio)
    $taskGraphics.DrawImage($taskSourceImage, [single](($taskSize - $taskWidth) / 2), [single](($taskSize - $taskHeight) / 2), $taskWidth, $taskHeight)
    $taskStream = [System.IO.MemoryStream]::new()
    $taskBitmap.Save($taskStream, [System.Drawing.Imaging.ImageFormat]::Png)
    $taskFrames += ,@{ Size = $taskSize; Bytes = $taskStream.ToArray() }
    if ($taskSize -eq 256) { $taskBitmap.Save((Join-Path $PSScriptRoot '..\build\app-icon-preview.png'), [System.Drawing.Imaging.ImageFormat]::Png) }
    $taskGraphics.Dispose(); $taskBitmap.Dispose(); $taskStream.Dispose()
}
$taskSourceImage.Dispose()
$taskOutput = Join-Path $PSScriptRoot '..\resources\tiger-snip.ico'
$taskFile = [System.IO.File]::Create($taskOutput)
$taskWriter = [System.IO.BinaryWriter]::new($taskFile)
$taskWriter.Write([uint16]0); $taskWriter.Write([uint16]1); $taskWriter.Write([uint16]$taskFrames.Count)
$taskOffset = 6 + 16 * $taskFrames.Count
foreach ($taskFrame in $taskFrames) {
    $taskDimension = if ($taskFrame.Size -eq 256) { 0 } else { $taskFrame.Size }
    $taskWriter.Write([byte]$taskDimension); $taskWriter.Write([byte]$taskDimension)
    $taskWriter.Write([byte]0); $taskWriter.Write([byte]0)
    $taskWriter.Write([uint16]1); $taskWriter.Write([uint16]32)
    $taskWriter.Write([uint32]$taskFrame.Bytes.Length); $taskWriter.Write([uint32]$taskOffset)
    $taskOffset += $taskFrame.Bytes.Length
}
foreach ($taskFrame in $taskFrames) { $taskWriter.Write([byte[]]$taskFrame.Bytes) }
$taskWriter.Dispose(); $taskFile.Dispose()
