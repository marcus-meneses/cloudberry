# JavaScript graphics samples

The image builder installs these files under `/system/script/samples`.
Run the graphical text editor from the Cloudberry terminal:

```
js /system/script/samples/editor.js
js /system/script/samples/editor.js /home/alice/Documents/notes.txt
```

Use File/Edit/Search/Help menus with the mouse or F10 and arrow keys. Enter
activates a menu item or dialog button; Tab changes buttons; Esc cancels.
Ctrl+A selects a dialog's input field for replacement.

- Ctrl+N / Ctrl+O: new / open, with an unsaved-changes dialog.
- Ctrl+S / F4: save / save as.
- Ctrl+F / F3: find / find next, wrapping through the document.
- Ctrl+Z / Ctrl+Y: undo / redo.
- Ctrl+C / Ctrl+X / Ctrl+V: copy current line / cut current line / paste.
- Ctrl+Q: quit, asking about unsaved changes.
- Arrows and Home/End move the file caret; Insert toggles INS/OVR.
- Wheel and Page Up/Down scroll only the viewport. The file row/column stay fixed;
  the caret disappears off-screen and reappears at its original file position.
  Typing edits at that file position and brings it back into view.
- Click the document to place the caret; wheel scrolls. The mouse handler supplies
  the arrow cursor independently of this application.
- Open/Save As browse sorted folders/files. Enter opens a selected folder/file;
  Backspace or Alt+Up goes up; Page Up/Down scroll. Ctrl+L edits the path,
  Ctrl+H toggles underscore-hidden entries, and Tab/Shift+Tab changes focus.
  Toolbar buttons go up, return to the working directory or create a folder.
  Save As asks before replacing an existing file.

This is an application sample, not a desktop/window manager. It uses the ordinary
`js` application's manifest and filesystem permissions. Graphics, keyboard and
mouse handles are acquired only when the script requests them. The keyboard
handle owns focus during editing; pending reads are abandoned, resources closed
and focus returned on exit. The mouse is optional when its handler is unavailable.
There is no idle polling/render loop: input completion wakes the script. UI cells
are retained in editor-view.mjs; unchanged document rows are skipped, native glyph
batches defer presentation until the frame is complete, and graphics retains
sparse damage. Pointer motion does not trigger document rendering.
editor-files.mjs supplies separately tested navigation state.

`editor-model.mjs` provides the document model. UTF-8 is validated before replacing
the current document; malformed files show an error and leave it intact. Uniform
LF/CRLF and a leading UTF-8 BOM are retained; mixed line endings normalize to the
chosen convention. Editing counts Unicode scalars; visible glyphs use the current
Ubuntu Mono TrueType face (queried 8×17 cells at 16 pixels), whose unsupported characters render as replacements. Tabs
loaded from files occupy one cell; the Tab key inserts four spaces. Clipboard
contents are local to this editor. No selection-based cut/copy, proportional-font
layout or OS clipboard is claimed.

Saving uses the existing recreate/write file operation, not atomic replacement;
confirmed replacement of an existing Save As path overwrites it. A write failure keeps the document
marked unsaved but cannot guarantee rollback of an already modified target file.
The ordinary kernel resource and interpreter allocation limits apply; the sample
does not introduce a file-size or line-count ceiling. Undo snapshots consume app
memory. It needs at least a 480x340 display.

Focused checks:

```
make -C src test-js-editor-model
make -C src test-js-editor QEMU_CPUS=4 QEMU_ACCEL=tcg
# Host-native acceptance:
make -C src test-js-editor QEMU_CPUS=4 QEMU_ACCEL=kvm
```

The GUI test uses the real sample in an isolated disposable image, drives keyboard
and mouse menus/dialogs, saves/reopens text, checks discard/cancel behavior and
verifies terminal input after exit. Test reports/media follow normal retention.

The earlier bitmap-font acceptance checkpoint predates this TrueType/cursor update.
Current results are recorded by the focused GUI runner, with sample/binary digests.
Native KVM acceptance remains a separate host check.

The current editor uses real graphics surfaces per visible line. Document lines
are children of an editing-area container; header/status/menu rows are separate
main-canvas children. Vertical scroll reuses row bitmaps by changing their position
and repaints the recycled row/caret. Surface release precedes graphics close;
terminal presentation restores its own cached tree after the editor exits.

Before recomputing a frame the editor hides the pointer through the mouse handle,
then restores it in a finally block after presentation. Hidden input still updates
mouse coordinates; showing uses the existing arrow bitmap at its latest position.
This also runs when rendering fails; owner-exit cleanup covers abrupt termination.
