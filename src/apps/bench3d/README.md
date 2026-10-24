# 3D benchmark

`bench3d` is a bundled native application using the ordinary `/@graphics`,
`/@keyboard`, `/@mouse`, `/@io` and standard stream handles. Run it by name:

```text
bench3d
bench3d --cubes 16
bench3d --frames 120 --cubes 4
```

The scene contains rotating, shaded cubes. Fixed-point CPU transforms,
perspective projection, backface culling and depth-sorted polygon submission
feed the existing graphics primitives. It measures the current application →
graphics → display pipeline with either backend; it does not submit native GPU
3D commands. The app never acquires the private GPU/display-render interface.

The HUD shows rolling FPS, average/last/minimum/maximum frame time, cube and
transformed vertex counts, visible faces, graphics calls in the last frame and
build/draw/present intervals. FPS includes time between completed frames; frame
intervals cover geometry, graphics requests, HUD updates when due and presentation.
Phase timings are elapsed intervals, including service waits and clock endpoints,
rather than exclusive CPU time. Min/max retain cold frames until reset. No frame
cap or deliberate sleep is imposed while running. The HUD refreshes about once
per second; its own rendering cost remains part of the benchmark.

| Key | Action |
| --- | --- |
| Esc | Quit and return control to the terminal |
| + / - | Increase/decrease load, from 1 to 64 cubes |
| W | Toggle solid/wireframe rendering |
| Space | Pause/resume; paused execution waits for keyboard events |
| R | Reset the timing window and min/max |

`--frames N` exits after N successful presentations. `--cubes N` selects the
initial workload; the default is four. Dimensions and font metrics come from
the graphics handle. Owned scene/HUD/footer surfaces are released on exit,
pending input is abandoned, focus is released and cursor visibility is restored.
A final frame count/time summary goes to stdout. Standard input is inherited as
required by the current standard-stream binding contract; interactive controls
use the keyboard handle directly. The app needs no filesystem permissions.

Validation:

```sh
make -C src test-bench3d-model
make -C src test-bench3d QEMU_CPUS=4 QEMU_ACCEL=tcg
```

The UBSan model check covers 1,668 scenes, rotation periodicity, culling,
depth order, bounded projection and maximum workload. Guest acceptance checks
visible filled geometry, updated HUD, load/wireframe/pause controls, Esc/focus
return and a two-frame invocation. It uses disposable media and retains an
actual paused screenshot as `bench3d.png` beside its report. This is a functional
check; measured performance depends on accelerator/backend and workload.
