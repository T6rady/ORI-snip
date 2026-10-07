# Tiger Snip

Tiger Snip is a local Windows screenshot and annotation tool. Capture an area or the full desktop, add text and drawings, then copy the image or save it as a PNG.

## Features

- Pen, highlight, text, shapes, arrows, checks, and lines.
- Crop, undo/redo, zoom, and ten recent editable captures.
- Top toolbars or side panels, with Purple, Orange, Blue and Teal color themes.
- Light or Dark appearance in either layout.
- Stroke presets plus an adjustable pixel slider, and per-annotation opacity.
- Optional borders, Samtec logos, and capture shortcuts.

## Installation

Open **dist/Tiger Snip Setup.msi** and click **Install**. When setup confirms success, click **Finish** to launch Tiger Snip (or uncheck **Launch Tiger Snip**). You can also open it from the Start menu. Each user gets a local installation. For shared-drive distribution, place the MSI in the shared folder.

The standalone **dist/Tiger Snip.exe** can also run directly.

## Use

Click **New snip** and drag a rectangle. Add annotations, then press **Ctrl+C** to copy or **Ctrl+S** to save. See [Quick Start](dist/Quick%20Start.txt) for the controls.

**Settings → Toolbar layout** switches immediately between **Top toolbars** (the original ribbon)
and **Side panels** (the tool rail and properties panel). The choice and each layout's visibility
settings are remembered. Switching keeps the current image, annotations, selection and undo history.

With Side panels, choose drawing tools on the left. The properties panel on the right shows color, pixel size,
style presets and opacity for the current tool or selected annotation. Use the small chevrons
on Shapes, Arrow, Check/X and Line to choose a preset, or use the visual style picker in the
properties panel. Pixel sizes have quick presets, a continuous slider and plus/minus controls;
each slider drag is one undoable edit. **Esc** cancels a slider drag.

The **Settings** gear (or **F10**) opens a matching settings panel with appearance, capture,
export, view and app actions. Settings save immediately. Choose **Purple**, **Orange**, **Blue**
or **Teal**, then **Light** or **Dark**; both choices work with either layout and affect only
the editor. Capture shortcuts, auto copy, rendering, all border options and all six logo styles
remain available. The capture button's chevron
also offers an instant capture of all monitors. Zoom and Fit are in the bottom bar.
Top toolbars retains the native menu bar and the original controls above the image.
Copy feedback is a brief white pulse over the image, including its transparency; it never changes exports.

[Application information](APP-INFO.md) describes installation and data storage.

## Build

Using the toolchain described in [BUILD-TOOLCHAIN.md](BUILD-TOOLCHAIN.md):

```powershell
.\build.ps1 -Test
.\package.ps1
```

## Support

Jack Kempf — jack.kempf@samtec.com.
