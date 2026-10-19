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
Line editing currently appends UTF-8 text and
supports backspace/Enter; Ctrl-D on an empty foreground input closes stdin.
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

For this first single-threaded UI, foreground multiplexing uses nonblocking pipe
reads and keyboard waits of up to 10 ms, checking child status between waits. A backpressured input line is retained for retry
rather than silently discarded.
At an idle prompt with no orphaned child to reap, keyboard reads block until
notification instead of issuing clock/read/timeout calls every 10 ms. Orphan
cleanup retains bounded waits so completion is still collected without input.
Kernel pipe blocking and application completion notifications themselves are
wake-driven. A general wait-set/async UI multiplexing facility, command history,
full cursor editing, background jobs and a window/session model remain future
work. Launches and command lines fit one bounded transport frame.

Use `make -C src run QEMU_BOOT_WRITABLE=1` to exercise file writes in a disposable
snapshot of the boot medium. This does not persist files across VM exits.
