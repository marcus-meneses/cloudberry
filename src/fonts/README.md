# System font

Ubuntu Mono Regular is the default text/UI face at 16 pixels, installed as
`/system/fonts/UbuntuMono-R.ttf`. Its measured cells are 8×17 pixels; callers
query graphics.font_info instead of assuming a grid size.

The original, unmodified font is distributed under the Ubuntu Font Licence 1.0
in LICENCE-Ubuntu.txt. Source: https://design.ubuntu.com/font (also packaged by
Ubuntu as fonts-ubuntu). SHA-256:
`b35dd9d2131d5d83a9b87fe9ad22c6288fa3d17688d43302c14da29812417d63`.

The graphics provider embeds FreeType 2.15.0, downloaded and hash-verified by
`tools/build-freetype.py`, using its FreeType Project License (FTL). License and
notice files are included in the provider's installed resources. Only memory
faces and selected raster/font modules are built; font loading uses startup
assets, with no FreeType host file access. No complex-script shaping or fallback
font collection is implemented yet. Missing glyphs use the face's replacement.
