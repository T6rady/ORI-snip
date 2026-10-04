# Snipper

A small, native Windows screenshot tool written in C++20. No browser engine, .NET runtime, installer, account, or network connection is required to run it.

## Run

Double-click **Run Snipper.cmd** or **dist/Snipper.exe**. Click **New snip**, drag a rectangle, and release to open the image with **Pen already selected**. Press **Ctrl+C** to copy the screenshot and all annotations, or **Ctrl+S** to save a PNG. Capturing does not change your clipboard. Circle, Arrow, Check, and Line have shape previews and style dropdowns.

The editor combines its capture icon and **New snip** action in one button, with undo/redo beside it and Copy/Save on the right. Select/Pen and the right-aligned Shapes tools have separate outlined groups with generous spacing. Color / stroke / zoom controls occupy their own row. Hover a control for its shortcut or a quick hint; Copy briefly shows **Copied!** after a successful export. The dotted workspace, violet selection states, and circular handles belong to the editor only and do not appear in exported images.

- Pen: freehand drawing with 1–40 px brush widths, eight quick colors, and a custom color picker.
- Circle: drag an ellipse; hold Shift for a circle. Its dropdown offers outline, soft highlight, and dashed styles.
- Arrow: drag from tail to tip. Select it and drag either endpoint to resize or turn it. Choose Classic, Outlined (colored fill with a black border), Curved Gloss, or Straight Gloss (tapered arrows with a black border and highlight).
- Check: click to place a green boxed check or drag to choose its size. Its dropdown offers boxed, circle-badge, and simple-check styles.
- Line: drag a solid, dashed, or dotted line. Hold Shift to snap to 45-degree angles. Select it and drag either endpoint to change its length or angle.
- Select: drag annotations to move them. Use corner handles to resize circles, checks, or strokes. Shapes automatically switch to Select after placement.
- Choose a color while an annotation is selected to recolor it. Brush size changes affect selected pen, circle, arrow, and line strokes.
- Undo/redo covers drawing, placement, moving, resizing, recoloring, size changes, and deletion. History stores annotations, not copies of screenshots, and keeps the latest 50 edits.
- Fit/100% changes the view only; exported images always preserve the captured pixel dimensions.

**Settings > Keyboard shortcut** changes the global shortcut (default **Ctrl+Alt+S**). It works while Snipper is running, including in the tray, and immediately opens selection. Conflicting shortcuts are rejected. Clear the field with Backspace to disable it. Settings are stored in `JackSnip.ini` beside the executable, so existing shortcut settings stay compatible across the rename.

Closing the editor releases the screenshot and leaves Snipper in the tray. Click its tray icon to snip; right-click for its menu. **File > Exit** quits completely. Unexported annotation changes prompt you to save before closing or replacing a capture. Copy or Save marks the current annotations as exported. **Settings > Run at sign-in** is optional and off by default; it registers this executable under your own Windows account and starts it quietly in the tray. Keep the executable at the same location when using that setting, and turn the setting off before moving or deleting it.

Each tool remembers its own previous color (including custom colors) and shape style. Recoloring a selected annotation also updates the remembered color for its tool. These preferences are saved in `JackSnip.ini` on close or exit only when changed, and restored on the next launch; drawing does not write settings to disk. New captures still start with Pen, and shapes still switch to Select after placement. Brush width retains its existing session behavior.

PNG saves are flattened. They can be viewed in any image app; this version does not save an editable project format. Clipboard output includes DIB, DIBV5, and PNG for compatibility with common paste destinations.

## Shortcuts

| Action | Shortcut |
|---|---|
| New snip | Ctrl+N |
| Copy flattened image | Ctrl+C |
| Save / Save As | Ctrl+S / Ctrl+Shift+S |
| Undo / redo | Ctrl+Z / Ctrl+Y (also Ctrl+Shift+Z) |
| Select / pen / circle / arrow / check / line | V / P / O / A / K / L |
| Brush smaller / larger | [ / ] |
| Delete selection | Delete |
| Zoom | Ctrl+mouse wheel |
| Pan | Middle-drag or Space+drag; wheel scrolls vertically, Shift+wheel horizontally |
| Cancel selection or current edit | Esc (right-click also cancels screen selection) |

## Capture behavior

Captures use one frozen image of the Windows virtual desktop, supporting monitors above or to the left of the primary monitor and selection across monitor boundaries. The app is Per-Monitor DPI Aware V2. Selection and export use physical screenshot pixels; UI scaling and zoom are separate. The global shortcut freezes the desktop immediately after removing the editor's pixels through compositor cloaking, before hiding the editor, showing selection, or asking to save the previous snip. This preserves transient hover menus that disappear when capture takes focus. Selection and any save confirmation use that same frozen frame; no second capture occurs after a prompt. Toolbar/menu captures retain their existing preparation delay to avoid editor/dialog fade artifacts. Esc returns to the previous image.

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

`Snipper.exe --self-test` runs native model and rendering tests without changing the clipboard. It checks undo/redo and cancellation, hit testing, coordinate transforms, crop boundaries, object resizing, all five annotation types, solid/dashed/dotted line exports, outlined/curved/straight gloss arrow artwork and selection, moved annotations, and a pixel-perfect PNG encode/decode round trip. It writes `self-test-results.txt` and `annotation-style-preview.png` to the current directory and returns a nonzero exit code on failure.

`build/clipboard_test.exe` (compiled with `build.ps1 -Test`, or through CMake) tests actual Windows clipboard output on a private, noninteractive window station. It verifies all three formats, pixel orientation, alpha/color metadata, and PNG round-trip fidelity without changing your clipboard. Run outside a restrictive sandbox if Windows denies access to window stations.

`Snipper.exe --smoke-test` additionally exercises a real editor window, live desktop capture, unsaved-snip confirmation dialogs, rapid repeated shortcuts while a dialog is open, cancellation restoration, ordinary and instant capture checked for editor/dialog pixels, focus-dismissed hover popups preserved before selection or save prompts with visible/hidden editors, stale capture-timer rejection, mouse-driven rectangle selection, all annotation tools, moving/resizing/recoloring, arrow endpoint rotation, deletion, toolbar press/release and cancellation, mouse undo/redo, compact toolbar layouts at 100/150/200% scale, annotated PNG export, and shortcut settings persistence, then exits. It writes `smoke-test-export.png`, editor preview images, `smoke-settings.ini`, and `smoke-test-results.txt` to the current directory. Run it when your normal Snipper instance is closed. This test also leaves the clipboard untouched.

For hands-on testing, try a capture spanning differently scaled monitors; move and resize each sticker; cancel a drag with Esc; copy into your usual chat/email app; save and inspect a PNG; change the shortcut and test it from the tray.

The smoke test saves tool preferences to its isolated `smoke-settings.ini`, including a change made after closing to the tray. After it exits, run `Snipper.exe --verify-smoke-preferences` in the same directory to verify a fresh process restores every tool's style and custom color without restoring an active tool. It writes `preference-test-results.txt` and leaves normal settings untouched.
