# C coding conventions

Use the repository's `.clang-format` with clang-format 18.

## Function names and scope

Use lowercase `snake_case` names. Choose a prefix from the function's interface:

| Scope | Naming | Example |
| --- | --- | --- |
| Private to one `.c` file | Plain descriptive name, declared `static` | `window_is_minimized` |
| Shared internally by the window-manager core | `wm_` | `wm_request_visibility` |
| Public window-manager API | `gf_wm_` | `gf_wm_window_move` |
| Other module interfaces | Existing `gf_` module namespace | `gf_taskbar_restore_all` |

Keep existing public API names, including utility names such as `gf_malloc`.
Header-shared inline helpers retain their interface prefix. Keep required entry
points such as `main` and `WinMain`. Avoid leading underscores in function names.

Keep function implementations in the owning module's `.c` file. Headers contain
declarations and types, with small intentional inline helpers where appropriate.

## Refactoring

Update declarations, definitions, callers, callback references, and test
substitutes together. Keep function parameters, return types, data structures,
configuration keys, IPC commands, and runtime behavior unchanged during a naming
refactor. Preserve platform callback field names and Windows property names.

Unit test source filenames start with `test_`. Run the relevant existing CTest
tests and formatting checks after changes. Group them by module under `tests/`,
as described in [tests/README.md](tests/README.md).

Treat IPC data as untrusted. Pass explicit payload lengths, check arithmetic and
allocation bounds before copying, initialize replies, reject truncation, and
decode wire structures with checked `memcpy`. Keep implementations in `.c`
files. See [docs/ipc.md](docs/ipc.md) for the authentication boundary and sanitizer
validation workflow.

Generate Windows installers with `scripts/binary_builder.bat`.
