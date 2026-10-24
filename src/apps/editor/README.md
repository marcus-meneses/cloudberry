# Native graphical editor

`editor [path]` launches the ordinary system application `/system/apps/editor`.
This is a compiled C application using the native SDK/Newlib, with its own
manifest and installation identity. It does not embed or invoke JavaScript.
The JavaScript sample stays available at `/system/script/samples/editor.js`.

It follows the sample's colors, Ubuntu Mono font metrics, menu/status bars,
line-number gutter, dialogs and file browser. The graphics provider selects the
system font. Each visible line is a cached bitmap surface; the editing area is a
clipping container. Small vertical scrolls move/recycle line surfaces instead of
rasterizing the entire document. Ordinary frames send changed cells in deferred
glyph batches and present once. The mouse handler's arrow is hidden during a
frame and restored afterward; pointer motion alone does not repaint the editor.

Controls match the [JavaScript sample](../../script/samples/README.md):

- Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Q quit; F4 Save As.
- Ctrl+Z/Y undo/redo; Ctrl+C/X copy/cut the current line; Ctrl+V paste.
- Ctrl+F find; F3 find next; F1 about; F10 menus.
- Arrows/Home/End move the file caret. Insert toggles overwrite.
- Page Up/Down and the mouse wheel move only the viewport. They never move the
  file caret. Typing edits at that original position and reveals it.
- Open/Save As: folders first, underscore-hidden files omitted by default,
  Ctrl+L path, Ctrl+H hidden, Tab/Shift+Tab focus, Backspace/Alt+Up parent,
  keyboard or double-click navigation, working-directory and new-folder buttons.
- Unsaved changes ask Save/Discard/Cancel. Save As confirms replacement.

UTF-8 scalar columns, leading BOM and uniform LF/CRLF round trips are supported.
Malformed input leaves the old document intact. Tabs loaded from files occupy
one cell; Tab inserts four spaces. Clipboard and undo/redo belong to this app;
there is no selection clipboard. Immutable lines are shared between snapshots;
changed lines and line-pointer arrays use process memory, without an added
file-size, line-count or history-count cap. Filesystem path/transport limits
still apply. The minimum display is 60 by 20 font cells.

Saving uses recreate/write, like the sample. A failed save leaves the document
unsaved, but cannot roll back bytes already written to an existing file. Saved
state is allocated before starting the write. File failures stay in dialogs;
fatal graphics/input errors clean up pending reads, surfaces, cursor ownership
and keyboard focus. Keyboard/mouse reads use owned asynchronous requests and
wait-any; no busy loop or terminal input forwarding is introduced.

Validation:

```
make -C src test-editor-model
make -C src test-editor QEMU_CPUS=4 QEMU_ACCEL=tcg
# Host KVM check:
make -C src test-editor QEMU_CPUS=4 QEMU_ACCEL=kvm
```

GUI acceptance uses copied boot media and disposable files, exercising the same
menu, save/open, scrolling, file-caret and terminal-handoff checks as the sample.
Its report includes the native binary digest. No test fixtures enter the normal
system image.

Current verification: document tests and four-CPU TCG GUI acceptance pass
([report](../../../bin/tests/console/bf9bb8ed475f4b3aa4f9db5228ed5c63/result.json)).
Final source formatting changed debug information only; the tested and final
ELFs have identical allocatable sections. Native KVM performance is unmeasured.
