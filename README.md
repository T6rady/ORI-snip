# Tiger Snip

Tiger Snip is a local Windows screenshot and annotation tool. Capture an area or the full desktop, add text and drawings, then copy the image or save it as a PNG.

## Features

- Pen, highlight, text, shapes, arrows, checks, and lines.
- Crop, undo/redo, zoom, and ten recent editable captures.
- Optional borders, Samtec logos, and capture shortcuts.

## Installation

Open **dist/Tiger Snip Setup.msi**, then launch **Tiger Snip** from the Start menu. Each user gets a local installation. For shared-drive distribution, place the MSI in the shared folder.

The standalone **dist/Tiger Snip.exe** can also run directly.

## Use

Click **New snip** and drag a rectangle. Add annotations, then press **Ctrl+C** to copy or **Ctrl+S** to save. See [Quick Start](dist/Quick%20Start.txt) for the controls.

[Application information](APP-INFO.md) describes installation and data storage.

## Build

Using the toolchain described in [BUILD-TOOLCHAIN.md](BUILD-TOOLCHAIN.md):

```powershell
.\build.ps1 -Test
.\package.ps1
```

## Support

Jack Kempf — jack.kempf@samtec.com.
