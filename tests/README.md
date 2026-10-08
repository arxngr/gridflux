# Tests

Keep source filenames prefixed with `test_` and put tests beside their module group:

| Directory | Coverage |
| --- | --- |
| `core/` | Arrangement, workspaces, monitor state and reconnect behavior |
| `config/` | Rules, configuration persistence and user configuration paths |
| `ipc/` | Command parsing, binary replies and native Windows/Linux transports |
| `windows/` | Windows monitor, border, taskbar and maximize behavior |
| `gui/` | GUI lifecycle, asynchronous IPC and tray protocols |

`tests/CMakeLists.txt` registers platform-specific targets with CTest. Tests that
write configuration receive a private working directory under the build tree.
Native Windows tests use isolated named pipes/desktops; Linux IPC tests create
their own private temporary socket directory. They do not install GridFlux or
change the live window manager's configuration.

Run after configuring and building the project:

```sh
ctest --test-dir build --output-on-failure
# Visual Studio uses a configuration:
ctest --test-dir build-vs -C Release --output-on-failure
```

For a separate memory-checking build, configure with
`-DGF_ENABLE_SANITIZERS=ON`. MSVC enables AddressSanitizer; Linux GCC/Clang enables
AddressSanitizer and UndefinedBehaviorSanitizer. Keep dependencies available in
the runtime search path, including the MSVC sanitizer runtime on Windows. Linux
CI runs the IPC, configuration and core tests with sanitizer failures enabled.

`ipc-protocol` covers truncated and oversized frames, integer overflow, embedded
NUL/control bytes, unterminated strings, invalid boolean/monitor representations,
peer-provided counts and capacities, and 10,000 deterministic malformed inputs.
These tests supplement review; they are not a proof that no exploit exists.
