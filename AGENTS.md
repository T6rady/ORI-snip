# Development preferences

- The user authorizes discarding any current snip or unsaved annotations, closing or terminating Snipper, and restarting it when this speeds up coding or testing. Do not ask for confirmation or preserve a snip before doing so.
- Keep Snipper fast and responsive. For intermittent display problems, measure rendering and window handling before attributing the cause to the app or the PC's graphics drivers.
- On this work PC, the user reported multi-second display stalls with hardware rendering and immediate updates after switching to software rendering. Keep `SoftwareRendering=1` in its local `dist/JackSnip.ini`; other PCs should retain the default renderer unless evidence warrants changing it. The specific driver/compositor cause has not been established.
