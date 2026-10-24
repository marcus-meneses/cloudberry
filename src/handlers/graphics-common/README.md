# Shared graphics provider semantics

`provider.inc` implements public request decoding, ownership/focus checks,
primitive/text rasterization, surface lifetime and the provider receive loop.
The `graphics-vga.c` and `graphics-opengl.c` entry files select presentation only.
Public names, interface IDs and operation numbers do not change with the backend.

The reference path uses the existing software scene/damage compositor. The
OpenGL path uses cached GPU textures and a separate GPU composition target via
its display provider. CPU raster/font/scene helpers remain shared source under
`../graphics-vga`; they contain no calls to another running backend.
