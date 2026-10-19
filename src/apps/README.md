# Built-in applications

`installed.json` binds image paths, immutable manifest bytes, OS installation
identities, roles and stack settings in a build-generated catalog outside app
bundles. Keyboard, display, graphics, text, filesystem and application providers
are registered alongside ordinary apps. Callers cannot override those bindings.

| Application | Role |
| --- | --- |
| terminal | Ordinary initial app; keyboard/text frontend, cwd, application discovery, stdio routing and pipelines |
| launch | Ordinary launcher forwarding its own stdio bindings, waiting and returning the child result |
| true | Returns zero; accepts standard stream bindings without using them |
| echo | Writes arguments and newline to stdout |
| cat | Copies stdin or named files to stdout |
| ls | Lists the supplied directory or startup cwd, in four output columns |
| pwd | Writes startup cwd to stdout |

Only cd/clear/help/exit are terminal built-ins. Providers interpret their own
configuration; neither bootstrap nor the terminal implements external utilities.
The trusted build-generated `/system/handlers/applications.resources/initial.bin` launches terminal in normal
builds. Disposable tests replace it; no test app enters the candidate catalog.
The C startup helper preserves argv/cwd; standard handles are obtained from the
system library, not copied from parent numeric tokens. Ordinary apps receive
kernel-selected initial placement and remain single-threaded/pinned thereafter.

See [terminal behavior and limits](terminal/README.md) and [stream contracts](../sdk/streams.md).

Guest filenames follow the [filesystem namespace](../maps/filesystem.md): flat
`/system/apps/<appname>` plus manifest/resources sidecars, with root-session
private data under `/system/root/_apps/<appname>.data`. `ls -a` includes hidden
underscore-prefixed entries; default listings omit them.
