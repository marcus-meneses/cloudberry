# Process monitor

`monitor` is an ordinary installed application, discovered by the terminal through
its application catalog. It acquires `/@applications` with the read-only
`processes` operation; it receives no launch, stop or release authority.

Run `monitor` for a live table, `monitor -n 2` for two stdout snapshots, or
`monitor -b -d 2` for batch snapshots every two seconds. `-d` accepts positive whole
seconds; the default interval is one second. Batch output supports terminal file
redirection and pipelines. In live mode, Escape quits immediately and Up/Down
move one page when a previous/next page exists; Enter is unnecessary. The live
screen clears on exit and the terminal redraws its cwd prompt at the cleared
cursor position. Live mode acquires /@keyboard, claims exclusive input focus,
and reads structured events directly. The keyboard handler suspends other readers
until release or owner death. Stdin is not used to transport device events.
Batch stdout snapshots can run without a terminal or a stdin binding.

Rows include ordinary apps and privileged resource handlers. CPU% is the change
in scheduled process-thread execution over elapsed time: 100% means one logical
CPU, including execution inside that process's syscalls. The first or an invalid
sample shows `--`. CPU TIME is cumulative scheduled execution in seconds. CPU is
the thread's assigned logical CPU. USER KiB counts committed image, startup,
stack and allocation pages, excluding page tables, kernel stacks and resources
owned by providers. This is not total physical system memory or Linux RSS.
Kernel worker threads have no process record and are not separate rows.

The app stores rows dynamically, sorts by CPU use then PID, derives its grid
from `/@text`, and rewrites only changed rows. Text retains its exchangeable
`text -> graphics -> display` backend. The final screen row is reserved to avoid
scrolling. Telemetry is copied one record per call with a fixed PID ceiling for
each sweep; disappearing processes may be omitted, and the sweep is not atomic.
