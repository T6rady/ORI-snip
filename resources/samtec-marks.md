# Additional Samtec assets

`samtec-wordmark.png` is the user's original 300 × 300 PNG, copied without image editing. Inspection confirmed its background was already transparent. The native renderer crops transparent margins and preserves the orange lettering and black bars.

`samtec-tiger.png` was made from the user's supplied tiger artwork with the built-in imagegen tool in image-edit mode, with `transparent_background: true`. The result keeps the wide tiger and speed-stripe composition with a transparent background. It is embedded alongside the S and wordmark in the executable.

Final tiger prompt:

> Edit target: the supplied Samtec tiger artwork. Remove only its white background to true zero-alpha transparency, including all white spaces inside the tiger head, eyes, whiskers, and between the speed stripes. Preserve the exact original tiger face and every gray and orange stripe, their location, color, proportions and fine detail. This is a faithful brand cutout, not a redesigned tiger. Keep gray marks gray, orange marks orange, with clean antialiased edges. No new marks, text, glow, shadows, borders, gradients, textures, speckles, or white rectangle. Keep the full wide composition tightly cropped with a small transparent margin. Produce a clean transparent PNG suitable for a small logo watermark.

The six badge variations are rendered in native code from these three transparent marks. Each mark has a compact White badge and a Soft card with an orange accent. Both include rounded backgrounds, a fine gray edge, and a subtle gray halo. This treatment is shared by the Settings previews and all image exports. See `samtec-logo.md` for the original S asset's imagegen prompt.
