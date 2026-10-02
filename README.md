# Jack Snip

A small, native Windows screenshot tool written in C++20. No browser engine, .NET runtime, installer, account, or network connection is required to run it.

## Run

Double-click **Run Jack Snip.cmd** or **dist/JackSnip.exe**. Click **Snip**, drag a rectangle, and release to open the image with **Pen already selected**. Press **Ctrl+C** to copy the screenshot and all annotations, or **Ctrl+S** to save a PNG. Capturing does not change your clipboard. The Circle, Arrow, and Check buttons include small previews of their shapes.

- Pen: freehand drawing with 1–40 px brush widths, eight quick colors, and a custom color picker.
- Circle: drag an ellipse; hold Shift for a circle.
- Arrow: drag from tail to tip. Select it and drag either endpoint to resize or turn it.
- Check: click to place a green boxed check or drag to choose its size.
- Select: drag annotations to move them. Use corner handles to resize circles, checks, or strokes. Shapes automatically switch to Select after placement.
- Choose a color while an annotation is selected to recolor it. Brush size changes affect selected pen, circle, and arrow strokes.
- Undo/redo covers drawing, placement, moving, resizing, recoloring, size changes, and deletion. History stores annotations, not copies of screenshots, and keeps the latest 50 edits.
- Fit/100% changes the view only; exported images always preserve the captured pixel dimensions.

**Settings > Keyboard shortcut** changes the global shortcut (default **Ctrl+Alt+S**). It works while Jack Snip is running, including in the tray, and immediately opens selection. Conflicting shortcuts are rejected. Clear the field with Backspace to disable it. Settings are stored in `JackSnip.ini` beside the executable, so keep it in a writable folder.

Closing the editor releases the screenshot and leaves Jack Snip in the tray. Click its tray icon to snip; right-click for its menu. **File > Exit** quits completely. Unexported annotation changes prompt you to save before closing or replacing a capture. Copy or Save marks the current annotations as exported. **Settings > Run at sign-in** is optional and off by default; it registers this executable under your own Windows account and starts it quietly in the tray. Keep the executable at the same location when using that setting, and turn the setting off before moving or deleting it.

PNG saves are flattened. They can be viewed in any image app; this version does not save an editable project format. Clipboard output includes DIB, DIBV5, and PNG for compatibility with common paste destinations.

## Shortcuts

| Action | Shortcut |
|---|---|
| New snip | Ctrl+N |
| Copy flattened image | Ctrl+C |
| Save / Save As | Ctrl+S / Ctrl+Shift+S |
| Undo / redo | Ctrl+Z / Ctrl+Y (also Ctrl+Shift+Z) |
| Select / pen / circle / arrow / check | V / P / O / A / K |
| Brush smaller / larger | [ / ] |
| Delete selection | Delete |
| Zoom | Ctrl+mouse wheel |
| Pan | Middle-drag or Space+drag; wheel scrolls vertically, Shift+wheel horizontally |
| Cancel selection or current edit | Esc (right-click also cancels screen selection) |

## Capture behavior

Captures use one frozen image of the Windows virtual desktop, supporting monitors above or to the left of the primary monitor and selection across monitor boundaries. The app is Per-Monitor DPI Aware V2. Selection and export use physical screenshot pixels; UI scaling and zoom are separate. Before capture, the app disables its own window transitions, cloaks and hides the editor, and waits for the desktop compositor to finish. This prevents the editor's fade animation from appearing in screenshots. Esc returns to the previous image.

This version captures rectangular areas of the SDR desktop. HDR color fidelity, scrolling screenshots, recording, and capture of protected content are outside its scope. Image processing happens only when needed, with no continuous capture or render loop. Large multi-monitor captures temporarily need additional buffers during selection and export.

## Build

With a portable [LLVM-MinGW toolchain](https://github.com/mstorsjo/llvm-mingw/releases), run:

```powershell
.\build.ps1 -CompilerDirectory 'C:\path\to\llvm-mingw\bin' -Test
```

The script also discovers an extracted `llvm-mingw-*` directory under `.tools`. The result is `dist/JackSnip.exe`, with the C++ runtime linked statically. LLVM-MinGW is only a build dependency.

Alternatively, in a Visual Studio developer shell with C++ Build Tools and CMake:

```powershell
cmake -S . -B build-msvc -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

Windows SDK libraries: Win32, Direct2D, DirectWrite, WIC, shell, common controls, common dialogs, and DWM. The icon and DPI manifest are embedded in the executable.

## Verification

`JackSnip.exe --self-test` runs native model and rendering tests without changing the clipboard. It checks undo/redo and cancellation, hit testing, coordinate transforms, crop boundaries, object resizing, pixel checks for all four annotation types, moved annotations, and a pixel-perfect PNG encode/decode round trip. It writes `self-test-results.txt` to the current directory and returns a nonzero exit code on failure.

`build/clipboard_test.exe` (compiled with `build.ps1 -Test`, or through CMake) tests actual Windows clipboard output on a private, noninteractive window station. It verifies all three formats, pixel orientation, alpha/color metadata, and PNG round-trip fidelity without changing your clipboard. Run outside a restrictive sandbox if Windows denies access to window stations.

`JackSnip.exe --smoke-test` additionally exercises a real editor window, live desktop capture, mouse-driven rectangle selection, all annotation tools, moving/resizing/recoloring, arrow endpoint rotation, deletion, undo/redo, annotated PNG export, and shortcut settings persistence, then exits. It writes `smoke-test-export.png`, editor preview images, `smoke-settings.ini`, and `smoke-test-results.txt` to the current directory. Run it when your normal Jack Snip instance is closed. This test also leaves the clipboard untouched.

For hands-on testing, try a capture spanning differently scaled monitors; move and resize each sticker; cancel a drag with Esc; copy into your usual chat/email app; save and inspect a PNG; change the shortcut and test it from the tray.
