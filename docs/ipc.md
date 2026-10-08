# IPC boundary

GridFlux accepts a fixed command set. Received command text is parsed as data;
it is never passed to a shell, evaluated, or used as a function pointer.
Commands contain fewer than `GF_IPC_MSG_SIZE` bytes. The dispatcher receives an
explicit readable byte length, rejects embedded NULs and control characters,
and copies validated bytes into bounded local storage. Tokens and numeric
arguments are rejected on overflow or truncation.

Replies retain the existing native binary layout. The entire response is
initialized and record fields are copied individually, leaving structure padding
zeroed. Consumers validate the available payload length before allocating or
copying records, validate strings/booleans and monitor IDs, and set allocated
list capacity to the actual allocation. Release parsed lists with
`gf_free_workspace_list` / `gf_free_window_list`. A typed text response is decoded
with `gf_parse_command_response`; do not cast a character buffer to a structure.

## Windows

Named pipes reject remote clients. The first server instance must create the
name itself. Security-descriptor creation failures stop initialization, and
interactive clients receive individual read/write rights that exclude
`FILE_CREATE_PIPE_INSTANCE`. Clients request identification-only SQOS so a
pipe server cannot impersonate their token.

Both peers are checked using the process ID reported by the pipe, their Windows
session, and their executable's location/name. The server accepts only
`gridflux-cli.exe` and `gridflux-gui.exe` beside its executable. Clients accept
only `gridflux.exe` beside theirs. This accommodates UAC elevation to another
administrator account within the same interactive session.

The installation directory must be protected by Windows permissions. A writable
development directory does not provide the same executable-identity boundary.
An authorized user can intentionally script the trusted CLI's documented
commands; authenticating the executable does not prohibit that. Protection
against code already executing inside a trusted process is outside this boundary.

Overlapped operations have bounded waits. Each operation's event and buffer stay
alive through completion or cancellation. A pipe instance owns one outstanding
read, and partial/oversized replies fail instead of being decoded. GUI tasks
retain their window until the main-thread completion handler releases it; late
results do not access destroyed widgets during shutdown.

## Linux

The socket lives in an owned private runtime directory, or an owned `0700`
directory under `/tmp`. Paths must fit `sockaddr_un.sun_path`; unsafe fallback
display names and foreign/non-socket existing paths are rejected. A live socket
is not unlinked by a second server. Socket permissions are `0600`, and both
ends verify the kernel's `SO_PEERCRED` user ID. Processes running as the same
user are authorized under this model; it is not executable-based authentication.

Each connection carries one command. The client closes its write half after
sending it; the server reads to EOF before dispatch, avoiding interpretation of
a fragmented command prefix. Read/write operations share an absolute monotonic
deadline and sends suppress `SIGPIPE`. Update the server, GUI and CLI together:
older Linux clients that do not close the write half are incompatible with this
framing. Command meanings and arrangement behavior remain unchanged.

## Build and validation

MSVC builds explicitly enable stack cookies, Control Flow Guard, ASLR and DEP.
GCC/Clang builds enable the strong stack protector; Linux optimized builds also
enable fortified libc checks. MinGW links stack-check support statically and
enables ASLR/DEP. These mitigations complement input checks rather than making
the binary exploit-proof.

Run the adversarial tests under `GF_ENABLE_SANITIZERS` as described in
[`tests/README.md`](../tests/README.md). Keep security changes accompanied by
tests for rejected input and the legitimate GUI/CLI flow.

References: [Windows pipe access rights](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights),
[CreateFile SQOS](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea),
[Control Flow Guard](https://learn.microsoft.com/en-us/windows/win32/secbp/control-flow-guard),
[Linux Unix-domain sockets](https://man7.org/linux/man-pages/man7/unix.7.html),
[GCC instrumentation options](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html).
