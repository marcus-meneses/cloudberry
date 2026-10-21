# Terminal application

The installed ordinary `terminal` app acquires keyboard, text, application,
filesystem and stream capabilities through the C system library. It is the
trusted initial app in normal builds. Its prompt includes the current directory;
Help columns use the reported text width.
`cd` changes that state and every child receives it as immutable startup metadata.
Relative paths are canonicalized against that directory. There is no filesystem
authority implied by cwd.

Built-ins are `cd`, `clear`, `help`, `exit`; `help` discovers installed applications
through `/@applications.list`. `echo`, `cat`, `ls`, `pwd`, `launch` and `true` are
ordinary installed executables. A shared event queue preserves type-ahead across idle/foreground transitions.
Line editing operates on UTF-8 character boundaries. Left/Right, Home/End,
Backspace/Delete and Insert are supported; insertion is the default and Insert
toggles overwrite. A reversed cell marks the input cursor: steady in insertion
mode, blinking every 500 ms in overwrite mode. Editing restores its visible phase. Long lines use a
horizontal viewport on the input row; their full contents still reach the parser.
Ctrl-D on empty foreground input closes stdin; otherwise it deletes at the cursor.
At an empty idle prompt it exits the terminal.
There is no shell expansion or command substitution. Single/double quotes,
empty quoted arguments and backslash escaping are supported.

```
echo "hello world" |> cat
pwd |> cat
echo first => /example.txt
echo second -> /example.txt
cat /example.txt
```

`=>` creates/truncates once before execution; `->` creates/appends. `|>` connects
stdout to the next application's stdin. Stderr stays attached to a separate
terminal output pipe. All destinations are resolved/authorized before the chain
starts. Redirection is allowed on the final command; combined intermediate
redirection and piping is rejected. On setup failure no application is launched.
A later launch failure closes parent endpoints and requests cooperative stop for
already launched children; forced termination remains unsupported.

Utilities report their own errors through stderr; the terminal does not reinterpret
ordinary application exit codes as OS error statuses. `exit` leaves the boot
console idle; automatic terminal/session restart is not implemented.

The terminal creates pipes and asks `/@applications` to bind their endpoints into
each child. Both ends of a pipeline run concurrently; only the kernel chooses
initial CPUs, by current live process-thread occupancy. Threads remain pinned
thereafter. The terminal makes no placement decision. It forwards keyboard text
to child stdin and renders stdout/stderr; output is never fed into input.

The single-threaded UI submits one keyboard read and one completion wait per
child, then waits for any pending reply or pipe readiness through the system
library. Foreground output, stderr, stdin backpressure and orphan completion all
wake it; idle prompts use the same mechanism without a polling deadline. The
terminal uses no 10 ms keyboard timeouts. Only overwrite-mode cursor blinking
arms a 500 ms wait deadline, using a lazily acquired clock capability; default
insertion mode waits indefinitely when no other work is ready.
Nonblocking pipe reads/writes drain available progress before waiting again.
A backpressured input line is retained for retry rather than silently discarded.

Keyboard capture remains in the keyboard provider. After collecting a batch the
terminal submits the next read before echoing the batch, preserving both local
type-ahead and a pending provider read across foreground/idle transitions. Slow
synchronous rendering or another terminal call still delays UI processing; this
is not a separate input/rendering thread and history retention remains bounded.

Up/Down recall session commands and restore the unfinished draft after the newest
entry. History lives in a dynamically allocated private-memory list until terminal
exit; empty commands and consecutive duplicates are omitted. It performs no disk
writes and has no separate entry-count ceiling beyond available memory.

Tab completes file/directory names through `/@filesystem`, relative to cwd or an
explicit absolute path. Quotes/backslashes are decoded using the parser's rules;
completed literal names are escaped safely. Directories gain a slash; a unique
file at line end gains a space. Ambiguous prefixes extend to their common prefix;
another Tab lists matching names in four columns and preserves the input.
Underscore-hidden entries require an explicit underscore prefix. Completion
may perform synchronous directory calls. Foreground stdin editing does not recall
shell history or perform path completion. Child output preserves the unfinished
input line while it is rendered through text. Background jobs and a window/session
model remain future work. Launches, command lines and each wait-any subscription list
fit one bounded transport frame. Oversized pipeline/wait combinations are rejected
before launching children.

Use `make -C src run QEMU_BOOT_WRITABLE=1` to exercise file writes in a disposable
snapshot of the boot medium. This does not persist files across VM exits.


A foreground interactive app acquires /@keyboard and claims focus directly. The
keyboard handler suspends other readers and reports focus-generation changes;
the terminal pauses its editor/cursor and receives no owned-app keys. On release
or owner death it resumes at a cursor barrier, avoiding replay of application
keystrokes into the shell. The terminal redraws its cwd prompt when the called
app returns and does not restore a stale origin over an app-cleared screen.
Stdin remains an ordinary byte stream. The terminal can supply edited lines to
stdin and consume stdout/stderr, but any launcher/producer/consumer can bind
those same streams independently. There are no app-name input exceptions.
