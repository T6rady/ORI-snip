# Snipper

A small, native Windows screenshot tool written in C++20. No browser engine, .NET runtime, installer, account, or network connection is required to run it.

## Run

Double-click **Run Snipper.cmd** or **dist/Snipper.exe**. Click **New snip**, drag a rectangle, and release to open the image with **Pen already selected**. Press **Ctrl+C** to copy the screenshot and all annotations, or **Ctrl+S** to save a PNG. Capturing does not change your clipboard. Shapes, Arrow, Check, and Line have large visual style dropdowns.

The editor combines its capture icon and **New snip** action in one button, with undo/redo beside it and Copy/Save on the right. Select/Pen/Highlight/Text and the right-aligned Shapes tools have separate outlined groups. In narrow windows, Highlight uses its chisel icon with a tooltip. Color, stroke or font size, and zoom controls occupy their own row; text also shows Bold and Box toggles. Hover a control for its shortcut or a quick hint; Copy briefly shows **Copied!** after a successful export. The dotted workspace, violet selection states, and circular handles belong to the editor only and do not appear in exported images.

Each of the three toolbar rows has a small chevron at the far right to collapse or expand it. Collapsed rows keep a slim labeled strip so they are easy to reopen; the View menu also toggles each row. All rows start expanded. Their visibility is remembered on close or exit and restored after restarting.

Ctrl+mouse wheel zooms around the pointer. When a snip extends beyond the workspace, visible horizontal and vertical scrollbars let you drag the handles, click their arrows, or click the tracks to pan. **Center** in the bottom bar recenters the image while keeping its current zoom. After zooming or panning away from the center, **Center image** also appears in empty workspace when it can fit without covering the screenshot. **Fit** fits the image to the available space; **100%** shows its original pixel size.

Ordinary wheel scrolling is enabled only when the image is taller than the workspace; Shift+wheel scrolls horizontally only when it is wider. Panning and scrollbars use the same bounds and stop at the screenshot edges. Images that fit, including those at 100%, do not scroll or show unnecessary scrollbars. Ctrl+wheel still zooms even when scrolling is unavailable.

The full-screen button beside Undo/Redo (or **View > Full screen**, **F11**) fills the current monitor and hides the menu and toolbars. A slim bottom strip keeps Fit, 100%, Center, and Exit full screen available. F11 toggles back; Esc also exits after finishing or canceling any active editing action. Returning restores the window's previous size, position, maximized state, and collapsed rows. Full screen does not change the copied/saved image.

- Pen: freehand drawing with 1–40 px brush widths, eight quick colors, and a custom color picker. The cursor previews the brush's color and actual size at the current zoom, with a contrasting outline for visibility.
- Highlight (H): drag a translucent chisel brush over text or other content. It starts yellow at 24 px wide; color and width (4–80 px) are remembered on close/exit separately from the pen. The cursor previews the slanted nib at the current zoom. Each stroke blends once, so joins and retraced segments within that stroke stay evenly translucent; separate strokes can build up color. Highlights support selection, moving, resizing, recoloring, deletion, undo/redo, Copy, and PNG Save.
- Eyedropper: click the pipette beside the color controls (or press I), then click the image to use that pixel's color. It samples the screenshot and annotations, returns to your current tool, and can recolor a selected annotation with undo support. Press Esc or click the pipette again to cancel.
- Hold the size **+ / −** buttons to change brush or font size in one-pixel steps after a short delay. Move off the button to pause, move back to resume, and release to stop. Resizing a selected stroke or text annotation with one hold is one undoable change.
- Text: select Text (T), click the image, and type. The borderless typing field grows and shrinks with the text; plain text sits directly on the screenshot without an opaque input bar, and resize handles appear after typing finishes. Annotations use Bahnschrift (with Segoe UI fallback when unavailable). The Size controls choose an 8–144 px font size, alongside bold and color; toggle Box for a rounded background with a colored border. Light text automatically gets a dark box for readability. Enter adds a new line, Ctrl+Enter finishes, and Esc cancels the current text edit. Clicking elsewhere commits the text and returns to Select; select Text again to add another annotation, or double-click existing text to edit it again. Select text to move it, scale it with corner handles, or change its formatting. Finish typing before using Ctrl+C to copy the annotated screenshot; normal text selection/copy/paste works inside the editing field.
- Shapes: circles and rectangles share one dropdown with large artwork previews. Choose an outline, highlight, or dashed circle, or a square, rounded square, highlight box, or filled box. Drag to draw; hold Shift for a perfect circle or square. The main Shapes button (O) remembers the last circle/rectangle choice; R selects the last rectangle style.
- Arrow: drag from tail to tip. Select it and drag either endpoint to resize or turn it. Choose Classic, Outlined (colored fill with a black border), Curved Gloss, Straight Gloss (tapered arrows with a black border and highlight), or Block Gloss (a wide, straight shaft with a square tail, triangular head, black border, and highlight).
- Check: click to place a sticker or drag to choose its size. Its dropdown offers boxed, circle-badge, and simple styles for both green checks and red Xs. Switching between checks and Xs chooses green or red; switching styles within either group keeps your chosen color. Color controls can recolor either symbol, and the chosen style and color are remembered.
- Line: drag a solid, dashed, or dotted line. Hold Shift to snap to 45-degree angles. Select it and drag either endpoint to change its length or angle.
- Select: drag annotations to move them. Use corner handles to resize text, circles, rectangles, checks, or strokes. Shapes automatically switch to Select after placement.
- Choose a color while an annotation is selected to recolor it. Brush size changes affect selected pen, highlight, circle, arrow, and line strokes.
- Undo/redo covers drawing, placement, moving, resizing, recoloring, size changes, and deletion. History stores annotations, not copies of screenshots, and keeps the latest 50 edits.
- Fit/100% changes the view only; screenshot content exports at its captured pixel dimensions.

**Settings > Keyboard shortcut** changes the global shortcut (default **Ctrl+Alt+S**). It works while Snipper is running, including in the tray, and immediately opens selection. Conflicting shortcuts are rejected. Clear the field with Backspace to disable it. Settings are stored in `JackSnip.ini` beside the executable, so existing shortcut settings stay compatible across the rename.

**Settings > Save location...** opens a folder picker. Choose a folder and click **Use this folder** to remember it immediately. Future Save/Save As dialogs start there, and you can still choose another destination for any individual snip. Ctrl+S on an already saved snip continues to update its existing file. The tray menu also offers Save location. If the folder is removed or becomes unavailable, the save dialog falls back to its normal location.

**Settings > Rendering...** lets you enable **Use software rendering (compatibility mode)** if the window freezes, shows stale content, or takes seconds to redraw during resizing or maximizing. Leave it unchecked for hardware acceleration, the default for most PCs. Software rendering can help with graphics-driver issues but may use more CPU on large displays. **Apply** switches immediately and remembers the choice in `JackSnip.ini` beside the executable; no restart is needed. This affects Snipper only and does not change copied or saved image quality.

**Settings > Professional Border** is optional and **off by default**. Its submenu has **Enabled**, **Blur**, and **Rounded corners**. Turning Enabled on activates both effects; uncheck either component to use just the other. The shared export renderer adds a soft neutral-gray halo (48% opacity, 16 px blur, 1 px spread, 2 px downward offset) without a hard outline, and optionally clips the corners to an 8 px radius. Blur adds 20 px transparent padding on every side, increasing export dimensions by 40 px in each direction; rounded corners alone keep the original dimensions. With both components unchecked, the output is unchanged. The screenshot is not scaled or tinted. The effects are baked into copied images and saved PNGs, and all three settings are remembered on close or exit. The editor shows the same styled image as Copy and PNG Save; changing these settings immediately updates the preview, including transparent padding. Fit and scrollbars include the complete output bounds.

**Settings > Samtec Logo** is optional and **off by default**. Its submenu offers **Enabled** and six visual style previews: White badge and Soft watermark versions of the S, tiger, and full Samtec wordmark. Choosing a style enables the logo; uncheck Enabled to turn it off while keeping the selected style. White badges use a compact rounded white background, a fine gray edge, and a faint halo, with the original artwork at 94% opacity. Soft watermarks have no card, border, or orange underline: they use the transparent mark in monochrome at 32% opacity, with a very faint opposing halo around just the artwork. The renderer samples the background beneath the watermark and chooses gray on light backgrounds or light gray on dark backgrounds to keep the faint mark visible.

The complete logo treatment scales with the screenshot's shorter side, including its halo. White badges use 9% for the S, 8.5% for the tiger, and 5.5% for the wordmark; soft watermarks use 7.5%, 7%, and 5% respectively. White badge heights are capped at 80 px for S/tiger and 52 px for wordmarks; soft watermarks at 64 px and 44 px. All use a 2.5% inset from the bottom right. The assets are embedded in the executable. The shared export renderer places the logo after annotations and before Professional Border, so the settings work independently for preview, Copy, and PNG Save. Enabled state and selected style are remembered on close or exit. The logo appears in the image preview as soon as it is enabled or its style changes, and disappears when disabled. Preview, Copy, and PNG Save share the same rendering pipeline.

A successful Copy (including Ctrl+C) briefly flashes the screenshot with a subtle violet tint and edge, fading out in about 280 ms. The Copied button and status message also confirm success. The flash is an editor effect and never appears in copied or saved pixels. Busy-clipboard failures do not start it.

Closing the editor releases the screenshot and leaves Snipper in the tray. Click its tray icon to snip; right-click for its menu. **File > Exit** quits completely. Closing, exiting, and starting a new snip proceed immediately without a save confirmation. Copy or Save explicitly exports your annotations. Canceling screen selection with Esc restores the previous snip and its annotations. **Settings > Run at sign-in** is optional and off by default; it registers this executable under your own Windows account and starts it quietly in the tray. Keep the executable at the same location when using that setting, and turn the setting off before moving or deleting it.

Each tool remembers its own previous color (including custom colors) and shape style. Text also remembers font size, bold, and Box; Shapes remembers whether you last used a circle or rectangle. Recoloring a selected annotation also updates the remembered color for its tool. These preferences are saved in `JackSnip.ini` on close or exit only when changed, and restored on the next launch; drawing does not write settings to disk. New captures still start with Pen, and shapes still switch to Select after placement. Brush width retains its existing session behavior.

PNG saves contain the screenshot and annotations in one image, retaining transparency when Professional Border is enabled. They can be viewed in any image app; this version does not save an editable project format. Clipboard output offers PNG first, followed by DIBV5 with alpha metadata and DIB for compatibility with common paste destinations. The destination application chooses which clipboard format it uses.

## Shortcuts

| Action | Shortcut |
|---|---|
| New snip | Ctrl+N |
| Copy flattened image | Ctrl+C |
| Save / Save As | Ctrl+S / Ctrl+Shift+S |
| Undo / redo | Ctrl+Z / Ctrl+Y (also Ctrl+Shift+Z) |
| Select / pen / text / Shapes / rectangle / arrow / check / line | V / P / T / O / R / A / K / L |
| Finish / cancel text editing | Ctrl+Enter / Esc |
| Bold text | Ctrl+B |
| Pick a color from the image | I |
| Brush or font smaller / larger | [ / ] |
| Delete selection | Delete |
| Zoom | Ctrl+mouse wheel |
| Full screen / return to editor | F11 (Esc also returns when no edit is active) |
| Pan | Middle-drag or Space+drag; wheel scrolls vertically, Shift+wheel horizontally |
| Cancel selection or current edit | Esc (right-click also cancels screen selection) |

## Capture behavior

Captures use one frozen image of the Windows virtual desktop, supporting monitors above or to the left of the primary monitor and selection across monitor boundaries. The app is Per-Monitor DPI Aware V2. Selection and export use physical screenshot pixels; UI scaling and zoom are separate. The global shortcut freezes the desktop immediately after removing the editor's pixels through compositor cloaking, before hiding the editor or showing selection. This preserves transient hover menus that disappear when capture takes focus. Selection uses that same frozen frame. Toolbar/menu captures retain their existing preparation delay to avoid editor fade artifacts. Esc returns to the previous image.

This version captures rectangular areas of the SDR desktop. HDR color fidelity, scrolling screenshots, recording, and capture of protected content are outside its scope. Image processing happens only when needed, with no continuous capture or render loop. Large multi-monitor captures temporarily need additional buffers during selection and export.

## Build

With a portable [LLVM-MinGW toolchain](https://github.com/mstorsjo/llvm-mingw/releases), run:

```powershell
.\build.ps1 -CompilerDirectory 'C:\path\to\llvm-mingw\bin' -Test
```

The script also discovers an extracted `llvm-mingw-*` directory under `.tools`. The result is `dist/Snipper.exe`, with the C++ runtime linked statically. LLVM-MinGW is only a build dependency.

Alternatively, in a Visual Studio developer shell with C++ Build Tools and CMake:

```powershell
cmake -S . -B build-msvc -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

Windows SDK libraries: Win32, Direct2D, DirectWrite, WIC, shell, common controls, common dialogs, and DWM. The icon and DPI manifest are embedded in the executable.

## Verification

`Snipper.exe --self-test` runs native model and rendering tests without changing the clipboard. It checks undo/redo and cancellation, hit testing, coordinate transforms, crop boundaries, object resizing, all annotation types, all six check/X styles at small and large sizes, solid/dashed/dotted line exports, outlined/curved/straight/block gloss arrow artwork and selection, moved annotations, image color sampling, plain/bold/multiline/boxed text, rectangle styles, native pen cursor size/color/hotspot across brush widths, zoom levels, and DPI scales, and pixel-perfect PNG encode/decode round trips. Professional Border checks cover default OFF, independent blur/rounding combinations, transparent padding only with blur, neutral halo falloff and visible contrast on white/black backgrounds without a hard outline, unchanged screenshot pixels, tiny snips, and transparency through PNG encoding. It writes `self-test-results.txt`, `annotation-style-preview.png`, `text-shape-preview.png`, `check-x-style-preview.png`, `professional-border.png`, and `professional-border-preview.png` and `professional-border-dark-preview.png` (OFF/ON against white/black backgrounds) to the current directory and returns a nonzero exit code on failure.

`build/clipboard_test.exe` (compiled with `build.ps1 -Test`, or through CMake) tests actual Windows clipboard output on a private, noninteractive window station. It verifies all three formats, PNG-first ordering, pixel orientation, alpha/color metadata, and PNG round-trip fidelity with Professional Border OFF and ON without changing your clipboard. Run outside a restrictive sandbox if Windows denies access to window stations.

`Snipper.exe --smoke-test` additionally exercises a real editor window, live desktop capture, unsaved new snips/closing/exiting without confirmation, closing during text editing, rapid repeated capture shortcuts, cancellation restoration, ordinary and instant capture checked for editor pixels, focus-dismissed hover popups preserved before selection with visible/hidden and edited/unedited snips, stale capture-timer rejection, mouse-driven rectangle selection, all annotation tools, red X click/drag placement and editing, check/X default and custom colors, moving/resizing/recoloring, arrow endpoint rotation, deletion, toolbar press/release and cancellation, mouse undo/redo, click-to-type text and live formatting, text editing/cancellation/history/resizing, native visual shape menus, the eyedropper, compact text/shape toolbar layouts at 100/150/200% scale, annotated PNG export, and preferences persistence, then exits. It writes `smoke-test-export.png`, editor and menu preview images, `smoke-settings.ini`, and `smoke-test-results.txt` to the current directory. Run it when your normal Snipper instance is closed. This test also leaves the clipboard untouched.

Samtec Logo tests check transparent cutouts, brand colors and proportions, scaling and bottom-right placement, translucent rendering, transparent and tiny images, and PNG round trips. Soft watermark checks cover low opacity, neutral color without an orange accent, transparent backgrounds, compact dimensions, and visible but faint contrast on white and dark backgrounds. All six styles are checked over white, dark, and text backgrounds, with Professional Border enabled/disabled. Clipboard tests cover PNG, DIBV5, and DIB for every style. Smoke tests check the Enabled checkmark, radio selection, live native menu previews, actual PNG Save, and style persistence across a fresh process. Copy feedback tests verify that the flash is visible, expires, and leaves exported pixels unchanged without touching the user's clipboard. Rendering samples include `samtec-styles-preview.png`, `smoke-test-samtec-menu.png`, and `smoke-test-copy-flash.png`.

Save location smoke checks cover the menu command, filenames in a folder containing spaces, immediate persistence, preservation of existing save paths, invalid-folder rejection, unavailable-folder fallback, and PNG output in the configured folder. The fresh-process preference test also verifies the chosen folder survives a restart.

Navigation smoke checks verify visible scrollbar handles and native panning, Center clicks outside the screenshot without changing zoom, all eight toolbar visibility combinations at 100/150/200% DPI, and F11/Esc full-screen transitions from normal and maximized windows. The fresh-process preference test verifies collapsed rows are remembered. UI samples include `smoke-test-scrollbars.png`, `smoke-test-center.png`, `smoke-test-collapsed.png`, and `smoke-test-full-screen.png`.

For hands-on testing, try a capture spanning differently scaled monitors; move and resize each sticker; cancel a drag with Esc; copy into your usual chat/email app; save and inspect a PNG; change the shortcut and test it from the tray.

`Snipper.exe --resize-test` checks immediate repainting and image centering during resizing, maximize/restore, and title-bar double-clicks. It reports median, 95th-percentile, and worst operation times plus drawing-phase timings in `resize-test-results.txt`, using a blank editor and synthetic 4K snips with and without a border. It checks that resizing reuses the screenshot preview and drawing resources. It opens a separate test window, exits afterward, and leaves your current snip, saved settings, and clipboard untouched. Timings include synchronous window handling and painting, rather than the duration of Windows' compositor animations.

For intermittent display stalls, `--resize-idle-test` runs the same checks with 350 ms of normal message processing between operations. Add `--software-rendering` to compare against a CPU-rendered editor; normal launches retain the default hardware renderer. Add `--trace-resize` to log individual window messages, render-target resizing, layout, and presentation durations to `%TEMP%/Snipper-resize-<process-id>.log`. A 250 ms heartbeat records whether the message loop stays responsive, and other window messages taking over 100 ms are also logged. Diagnostic I/O and heartbeat polling are disabled during normal launches, and traces contain timings rather than screenshot pixels.

`Snipper.exe --diagnostic-instance --trace-resize` opens a separate window titled **Snipper - Resize diagnostic** for testing actual clicks while your normal snip stays open. This instance uses a separate window class and instance lock, does not register the global capture shortcut, and writes its own preferences to `%TEMP%/Snipper-diagnostic-<process-id>.ini`. Add `--software-rendering` to test software rendering in that window. Use **File > Exit** to end the diagnostic instance.

The Rendering dialog stores `SoftwareRendering=1` (software) or `SoftwareRendering=0` (hardware/default) under `[Settings]` in `JackSnip.ini` beside the executable. Without that setting, Snipper uses hardware rendering when available. `--software-rendering` and `--hardware-rendering` override the saved preference for a single run; applying a choice in the dialog takes precedence over that launch override. Normal rendering remains event-driven in either mode, with no continuous render loop. Smoke tests verify Apply/Cancel, live switching, persistence, and unchanged snips, annotations, history, selection, view, and exported pixels.

The smoke test saves tool preferences to its isolated `smoke-settings.ini`, including a change made after closing to the tray. After it exits, run `Snipper.exe --verify-smoke-preferences` in the same directory to verify a fresh process restores every tool's style and custom color without restoring an active tool. It writes `preference-test-results.txt` and leaves normal settings untouched.
