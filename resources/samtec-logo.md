# Samtec logo asset

`samtec-logo.png` is the transparent 1254 × 1254 PNG master embedded in the executable. The source was the logo image supplied by the user. The shared export renderer crops transparent margins, preserves the mark's proportions, and scales it to each screenshot. `samtec-logo-transparent.png` is the cropped transparent version produced by that renderer.

Created with the built-in imagegen tool in image-edit mode, with `transparent_background: true` and the user's original logo as the reference image.

Final prompt:

> Remove only the white background of this exact Samtec logo. This is an exact brand asset cutout, not a new logo. All white pixels including the two white cutouts in the orange S become transparent. Preserve the two black rectangular bars and the orange S shape exactly as in the original. Flat solid orange RGB(248,109,26), pure black bars RGB(0,0,0). No color texture, no speckles, no gradients, no effects, no shadows, no outline, no extra shapes. Sharp clean antialiased edges. Make all empty space transparent with zero opacity, including the spaces within the S. Preserve proportions and retain every part of the mark. A clean transparent PNG logo for a native screenshot application.
