# Project 2 — requirements

The assignment turns the Project 1 shell into a network service: a daemon
(`yashd`) that runs commands, and a thin client (`yash`) that acts as the
terminal.

---

## yashd — the server

| Requirement | Detail |
|---|---|
| Invocation | `$ yashd` — no arguments |
| Port | **3820**, hardcoded on both sides. Not configurable, not an argument. |
| Daemon | Must follow the standard daemonization guidelines covered in class |
| Concurrency | Multi-threaded with **pthreads** — one thread per accepted client |
| Shell logic | The thread function is mostly derived from Project 1 |
| Output | Command output goes **over the socket**, never to the daemon's stdout |
| Execution | Commands are still run with **fork + exec** |
| Logging | Every command executed is appended to `/tmp/yashd.log` |
| Socket reuse | Use the `reusePort` helper from the server example so rebinding works even after an unclean exit |

### Build order (professor's guidance)

> Start from a **Unix domain socket**, then transition to a **TCP socket**.

Both are `SOCK_STREAM`, so everything above the `accept` call is identical —
only the address family and the bind address differ. Building the Unix domain
version first removes networking from the picture while the hard parts (daemon
structure, threading, rewiring the shell's I/O) are being debugged, then the
transport is swapped underneath without touching the session logic.

Keep **both** behind a runtime toggle rather than deleting the Unix domain
path. It stays useful for local testing and it proves the session code has no
transport assumptions baked into it. See the transport section in
[DESIGN.md](DESIGN.md).

### The one-line summary of the port

> It's the yash shell, but `readline` becomes `readsocket`.

That is literally the change. Project 1's loop is:

```c
fgets(line, sizeof line, stdin);     /* read a line from the terminal */
...
fputs("# ", stdout);                 /* prompt to the terminal */
```

Project 2's loop is the same shape with both ends redirected at the socket,
plus a dispatch step because a line is now a typed protocol message rather than
always a command.

### Log format

Syslog-like, one line per command:

```
<date and time><blank>yashd[<Client IP>:<Port>]: <Command executed><newline>
```

```
Sep 18 10:02:55 yashd[127.0.0.1:7813]: ls -l
Sep 18 10:04:45 yashd[127.0.0.1:7815]: echo $PATH
```

The two entries above come from two different clients on the same host —
they are distinguished by the client's ephemeral **port**, not the IP.

---

## yash — the client

| Requirement | Detail |
|---|---|
| Invocation | `$ yash <IP_Address_of_Server>` — port is implicit (3820) |
| Role | Mimics a terminal: user types, responses come back |
| Ctrl-c | Stops the currently running command **on the server** |
| Ctrl-z | Suspends the currently running command **on the server** |
| Ctrl-d / `quit` | Terminates the client |

The client contains no shell logic. It does not parse, fork, or redirect
anything — it only moves bytes between the user's terminal and the socket, and
translates two keystrokes into control messages. It is **substantially simpler
than Project 1's shell** — no job table, no `fork`, no redirection, no parser.

The IP is the only argument because the port is hardcoded at 3820 in the
client binary.

---

## The protocol

Three client→server message types, one server→client message type. See
[PROTOCOL.md](PROTOCOL.md) for the full state machine; the wire format is:

### Client → server

```
CMD <command string>\n       run a command
CTL <c|z|d>\n                control character
<plain text>                 stdin for the running command, ended by EOF
```

Examples:

```
CMD ls -l\n
CMD ps -ef | more\n
CTL c\n
```

Plain text is used when the user is typing *into* a running command, e.g. the
body of `cat > file.txt`.

### Server → client

Plain ASCII text only — the output of a command, or the result of a control
message. It may contain any number of newlines.

---

## Implementation notes from the handout

- **The prompt comes from the server.** The client just prints whatever it
  receives. This removes the need for an end-of-output marker: the arrival of
  the prompt *is* the marker.
- **The prompt must be formatted as `"\n# "`.** The leading newline guarantees
  the prompt lands on a fresh line no matter how the previous command's output
  ended.
- **Use `reusePort`** (`SO_REUSEADDR`) so the daemon can bind 3820 immediately
  after a previous instance died without closing the socket.

---

## Carried over from Project 1

The shell semantics are unchanged and still graded:

- One command, or two joined by a single `|`
- `<`, `>`, `2>` redirection, space-separated tokens, 200-char lines
- `&` for background, `jobs` / `fg` / `bg` builtins
- Ctrl-c kills the foreground job, Ctrl-z stops it — but now driven by `CTL`
  messages instead of a terminal
- All children dead when the session ends

See [../Project 1/DESIGN.md](../Project%201/DESIGN.md) for how those pieces
work today.
