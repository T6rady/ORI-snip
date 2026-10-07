# Application icon

`app-icon.png` is the master artwork: an orange tile, silver scissors, and a white capture frame. The existing icon was recolored with image editing; the orange is RGB(248,109,26), `#F86D1A`.

`scripts/make-icon.ps1` converts the master into the Windows icon sizes in `ori-snip.ico`. `app.rc` embeds the icon in the executable.
