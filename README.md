# Tiger Snip

Tiger Snip is a local Windows screenshot and annotation tool. Capture an area or the full desktop, add text and drawings, then copy the image or save it as a PNG.

## Features

- Pen, highlight, text, shapes, arrows, checks, and lines.
- Crop, undo/redo, zoom, and ten recent editable captures.
- Orange and charcoal editor with a vertical tool rail and contextual properties panel.
- Stroke presets plus an adjustable pixel slider, and per-annotation opacity.
- Optional borders, Samtec logos, and capture shortcuts.

## Installation

Open **dist/Tiger Snip Setup.msi** and click **Install**. When setup confirms success, click **Finish** to launch Tiger Snip (or uncheck **Launch Tiger Snip**). You can also open it from the Start menu. Each user gets a local installation. For shared-drive distribution, place the MSI in the shared folder.

The standalone **dist/Tiger Snip.exe** can also run directly.

## Use

Click **New snip** and drag a rectangle. Add annotations, then press **Ctrl+C** to copy or **Ctrl+S** to save. See [Quick Start](dist/Quick%20Start.txt) for the controls.

Choose drawing tools on the left. The properties panel on the right shows color, pixel size,
style presets and opacity for the current tool or selected annotation. Use the small chevrons
on Shapes, Arrow, Check/X and Line to choose a preset, or use the visual style picker in the
properties panel. Pixel sizes have quick presets, a continuous slider and plus/minus controls;
each slider drag is one undoable edit. **Esc** cancels a slider drag.

The **Settings** gear (or **F10**) opens the File, Edit, View, Settings and Help menus, including
capture shortcuts, auto copy, rendering, export borders and logos. The capture button's chevron
also offers an instant capture of all monitors. Zoom and Fit are in the bottom bar.

[Application information](APP-INFO.md) describes installation and data storage.

## Build

Using the toolchain described in [BUILD-TOOLCHAIN.md](BUILD-TOOLCHAIN.md):

```powershell
.\build.ps1 -Test
.\package.ps1
```

## Support

Jack Kempf — jack.kempf@samtec.com.
