# Tiger Snip app icon

`app-icon.png` is the orange master for the Windows app, tray, and shortcut icon. It edits the existing blue scissors/capture-frame artwork from the repository, using the orange RGB(248,109,26), `#F86D1A`, already documented for the project's Samtec mark. The tile uses lighter and darker orange shading rather than a flat fill. The scissors remain silver, the capture frame remains white, and the outer corners are transparent.

Edited with the built-in imagegen tool on October 6, 2026 at the user's request. The second edit refined the transparent exterior. No application behavior was changed. `scripts/make-icon.ps1` converts this master into the 16, 24, 32, 48, 64, 128, and 256 px PNG frames in `tiger-snip.ico`; `app.rc` embeds that ICO into the EXE. Re-run the icon script after changing the master, then rebuild and package.

## Edit prompts

Initial recoloring, with the original blue master as the edit target:

> Use case: precise-object-edit. Edit target: the existing Tiger Snip app icon in the supplied PNG. Recolor ONLY the blue/cyan rounded-square tile background and its colored edge/gloss/shadow accents into a Samtec-orange palette anchored on RGB(248,109,26), hex #F86D1A. Retain the existing subtle glossy shading, highlights, and darker warm-orange shadows so the tile keeps the same dimensional appearance. Recolor blue/cyan areas visible inside the scissors handles and underneath the metallic scissors too. Preserve the exact silver scissors design, placement, angles, contours, texture, blades and pivot; preserve the white dashed capture rectangle, its corners, positions and spacing. Preserve the exact rounded-square tile silhouette, proportions, padding and composition. No redesign, no text, no tiger, no added elements. Preserve true transparency outside the rounded tile and antialiased outer edges; no white or black canvas background. Keep this as a square app-icon master.

Exterior refinement, with the orange edit as the target and the original blue master as the silhouette reference:

> Use case: precise-object-edit / background-extraction. Image 1 is the orange app icon to finish; Image 2 is the original blue icon ONLY as a reference for its perfectly clean rounded-tile silhouette and transparent margins. Keep the orange tile artwork from Image 1 unchanged: metallic silver scissors, white dashed capture frame, warm orange glossy background anchored on #F86D1A, positions and proportions. Fix only the exterior alpha: remove ALL stray red/yellow/orange speckles, flecks, fuzzy scraps and detached pixels outside the rounded-square tile. Everything beyond the smooth antialiased outer tile contour must be truly zero-alpha transparent. No exterior colored halo, no scattering or speckles, no black/white rectangle. Clean cutout for a Windows app icon. Preserve a small even transparent margin on all four sides. Square image.
