# yashd — design review questions

Answers reference the implementation in `yashd/server`, `yashd/client`, and
`yashd/shared`. Project 1's equivalent is [../Project 1/REVIEW.md](../Project%201/REVIEW.md).

---

## Architecture

**Q: Walk me through what happens between a client pressing Enter and the command running.**

The client's `pselect` wakes on stdin, reads the line, and — because it is at a prompt —
sends `CMD <line>\n`. The daemon's session thread is blocked in `session_readsocket`,
which assembles bytes until it has a complete line. `dispatch` sees the `CMD ` prefix,
`parse_line` turns the rest into a `job_t`, `log_command` records it, and then
`builtin_try` or `exec_job` runs it. The child's stdout is the socket, so its output goes
straight back to the client without passing through the daemon. When the job finishes,
the loop sends `"\n# "` and blocks on the socket again.

*Source: `session_main` in [session.c](yashd/server/src/session.c#L286)*

**Q: Why attach a session to each connection, when Project 1 kept everything in globals?**

Because the daemon serves many clients from one process. Project 1's
`static job_t *job_list` was safe precisely because there was exactly one shell; with a
thread per client, that static becomes shared mutable state with no lock. Two clients
running `jobs` would see each other's processes, and two concurrent `jobs_add` calls
would corrupt the linked list.

The fix is not a mutex — it is deleting the sharing. Each connection gets a `session_t`
holding the socket, the peer identity, the job list, the foreground job, and the input
buffer. Every function in `jobs.c`, `exec.c` and `builtins.c` takes it as the first
parameter, so there is no way to reach another client's state even by accident.

*Source: `session_t` in [session.h](yashd/server/include/session.h#L25)*

**Q: What state is still shared between threads, and how is it protected?**

Two things. The listening socket, which only the accept loop touches — session threads
never see it. And the log file, which every thread writes to, guarded by a mutex with a
single `write` per entry.

That is the whole shared surface. Keeping it that small is why the rest of the code
needs no locking at all.

*Source: `log_mutex` in [log.c](yashd/server/src/log.c#L28)*

**Q: Why threads instead of forking a process per client?**

Forking would actually have let the globals stay — each child would get its own copy of
the job table for free, and the port would have been nearly mechanical. The assignment
specifies pthreads, but the trade is real: threads are cheaper and share the log file
naturally, while processes would isolate failures so one crashed session couldn't take
the daemon down.

The cost of threads is exactly the `session_t` refactor, plus the `waitpid` problem
below.

*Source: `pthread_create` in [server.c](yashd/server/src/server.c#L12)*

**Q: Why `pthread_detach`?**

Nothing ever joins a session thread, and an unjoined thread's stack is never reclaimed.
In a process meant to run indefinitely, that leaks one thread's worth of memory per
connection. Detaching tells the library to clean up at exit instead.

*Source: [server.c](yashd/server/src/server.c#L12)*

---

## Becoming a daemon

**Q: What actually makes `yashd` a daemon rather than a background process?**

`setsid()`. It puts the process in a new session with no controlling terminal, which is
the defining property — no tty can send it SIGINT, SIGHUP or SIGTSTP, and it is not in
anyone's job table. Backgrounding with `&` leaves the controlling terminal attached, so
closing the terminal would kill it.

*Source: `daemon_init` in [daemon.c](yashd/server/src/daemon.c#L45)*

**Q: Why fork twice?**

The first fork is so `setsid` can succeed — it fails if the caller is already a process
group leader, which a shell-launched process is. The second fork leaves the daemon as a
non-session-leader, and only a session leader can acquire a controlling terminal by
opening a tty. So the second fork makes it permanently impossible to pick one up by
accident.

*Source: [daemon.c](yashd/server/src/daemon.c#L45)*

**Q: Why `chdir("/tmp")` rather than `/`?**

The textbook answer is `/`, so the daemon doesn't pin a filesystem someone may want to
unmount. But a client's `echo hi > out.txt` resolves relative to the daemon's working
directory, and a non-root daemon cannot write to `/`. `/tmp` satisfies the real
requirement — don't hold the invoking user's directory open — while leaving file
redirection usable.

The visible consequence is that `pwd` reports `/tmp` and relative paths land there.

*Source: [daemon.c](yashd/server/src/daemon.c#L45)*

**Q: Why reopen descriptors 0, 1 and 2 on `/dev/null` after closing everything?**

Because descriptor numbers are reused. If fd 1 is left closed, the first socket the
daemon opens becomes fd 1 — and then any stray `printf` anywhere in the ported shell
code writes directly into some client's output stream. Pointing them at `/dev/null`
makes that failure silent instead of corrupting.

*Source: [daemon.c](yashd/server/src/daemon.c#L45)*

**Q: Why is the pid-file lock taken after both forks instead of at startup?**

POSIX record locks are not inherited across `fork`. Locking before forking would leave
the surviving daemon holding no lock at all, and the guarantee would silently evaporate.

*Source: `pidfile_lock` in [daemon.c](yashd/server/src/daemon.c#L18)*

**Q: Why does the daemon ignore `SIGPIPE`?**

Writing to a socket whose peer has disconnected raises SIGPIPE, whose default action
terminates the process. In a single-client program that is merely rude; here it would
kill every other client's session too. Ignoring it turns the condition into an `EPIPE`
return from `write`, which the session handles by marking itself finished.

*Source: [main.c](yashd/server/src/main.c#L15), `session_write` in [session.c](yashd/server/src/session.c#L49)*

---

## Sockets and transport

**Q: What does `reusePort` actually do, and why is it needed?**

It sets `SO_REUSEADDR`. When a socket closes, its address stays in `TIME_WAIT` for up to
two minutes so that stray packets from the old connection can't be delivered to a new
one. During that window `bind` on port 3820 fails with `EADDRINUSE`. `SO_REUSEADDR`
waives the check for a listening socket, which is what lets the daemon restart
immediately after being killed.

*Source: `reusePort` in [transport.c](yashd/server/src/transport.c#L19)*

**Q: Why build the Unix domain socket first and keep it afterwards?**

Both are `SOCK_STREAM`, so everything above `accept` is identical. Starting local removes
networking from the picture while the genuinely hard parts — daemonizing, threading,
rewiring the shell's I/O — are being debugged. Keeping it afterwards costs one enum and
proves the session layer has no transport assumptions baked in.

*Source: `transport_listen` in [transport.c](yashd/server/src/transport.c#L76)*

**Q: Where does the transport leak upward, if anywhere?**

One place: peer identity. The log format requires `<IP>:<Port>`, and an `AF_UNIX` client
has neither — `getpeername` returns an empty `sun_path` for an unnamed client socket.
`peer_from_unix` substitutes `local:<pid>` obtained from `SO_PEERCRED` so that
`log_command` stays a single code path. Everything else above `transport.c` works on a
plain connected descriptor.

*Source: `peer_from_unix` in [transport.c](yashd/server/src/transport.c#L82)*

**Q: Why is the port hardcoded instead of configurable?**

The protocol is meant to interoperate with other students' implementations, so the port
is part of the contract rather than a deployment choice. It lives in `protocol.h`, which
both binaries include, so the two sides cannot disagree.

*Source: [protocol.h](yashd/shared/include/protocol.h)*

---

## The protocol

**Q: Why does the client need a protocol at all? Why not send raw keystrokes?**

Because three different kinds of input share one channel: commands, control characters,
and text destined for a running command's stdin. Raw bytes could not distinguish them —
typing the word `quit` into `cat` must not end the session. The `CMD`/`CTL` prefixes make
the distinction explicit, and anything unprefixed is stdin by definition.

*Source: `dispatch` in [session.c](yashd/server/src/session.c#L232)*

**Q: Why does the prompt come from the server rather than the client printing its own?**

It is the framing mechanism. A stream socket has no message boundaries, so the client
needs some way to know the server has finished producing output. Rather than inventing
an end-of-output marker, the protocol reuses the prompt: when `"\n# "` arrives, the
server is idle. The client therefore prints received bytes verbatim and parses nothing.

*Source: `session_prompt` in [session.c](yashd/server/src/session.c#L90)*

**Q: Why `"\n# "` and not `"# "`?**

The leading newline guarantees the prompt starts on a fresh line regardless of how the
previous command's output ended. Without it, `printf hello` — output with no trailing
newline — would render as `hello# `.

*Source: `YASH_PROMPT` in [protocol.h](yashd/shared/include/protocol.h)*

**Q: Why are Ctrl-c and Ctrl-z sent as messages instead of as the raw control bytes?**

Because there is no terminal on the server to interpret them. In Project 1 the tty
recognized `0x03` and generated SIGINT for the foreground process group. A socket has no
line discipline doing that, so the intent has to be carried explicitly and re-created
with `kill` on the far side.

*Source: `handle_ctl` in [session.c](yashd/server/src/session.c#L162)*

**Q: Why must the server reply with a prompt even after a `CTL` message that does nothing?**

Because the client's only signal that the server is ready is the prompt. If `CTL c` at an
idle prompt produced no reply, the client would sit with no prompt on screen and look
hung. The session loop reprompts after every dispatched message, which is also what makes
Ctrl-c at the prompt behave like bash.

*Source: [session.c](yashd/server/src/session.c#L286)*

**Q: How does the client know whether what you typed is a command or stdin?**

One bit of state. It is set when received bytes end with `"\n# "`, and cleared the moment
a `CMD` is sent. At a prompt, a typed line is a command; otherwise it is stdin for the
running command.

Tracking only the receive half is a subtle bug that still passes a casual test — the
first command really was typed at a prompt. But the server sends no prompt while a
command runs, so a stale flag turns every subsequent line into another `CMD`, and
`cat > file.txt` receives its input as commands.

*Source: `send_line` and `at_prompt` in [client.c](yashd/client/src/client.c#L142)*

**Q: Ctrl-d means two different things. How is that resolved?**

By the same flag. At the prompt there is no job to send EOF to, so `CTL d` ends the
session; while a command runs it closes the write end of the pipe feeding that command's
stdin. The server disambiguates by whether `fg_job` is set, so client and server agree
without an extra message type.

*Source: `CTL_EOF` branch in [session.c](yashd/server/src/session.c#L162)*

---

## Reading from a socket

**Q: Why can't `session_readsocket` just be a wrapper around one `read`?**

Because a stream socket has no message boundaries. One `read` may return half a line, a
whole line, several lines, or a line split across two packets. `fgets` hid all of that in
Project 1. So `buf_fill` appends whatever is available into `s->inbuf` and
`buf_take_line` pops a single `\n`-terminated line, leaving the remainder buffered for
the next call.

*Source: [session.c](yashd/server/src/session.c#L101-L160)*

**Q: What breaks if you buffer but discard the leftover?**

A client that sends `CMD ls\nCMD pwd\n` in one packet silently loses the second command.
The `memmove` in `buf_take_line` is what prevents it. Reading one byte at a time would
avoid the problem differently — it cannot over-read — at the cost of a syscall per
character.

*Source: `buf_take_line` in [session.c](yashd/server/src/session.c#L101)*

**Q: Why are there two dispatchers?**

`dispatch` runs at the prompt and treats `CMD` as a command. `dispatch_running` runs from
inside `session_pump` while a job is in the foreground, where only `CTL` is still a
message and everything else — including a stray `CMD` — is that job's stdin.

Routing `CMD` to `handle_cmd` from the pump would call `exec_job` recursively and fork a
second job on top of the running one. Treating it as stdin is both safer and what the
protocol specifies.

*Source: `dispatch_running` in [session.c](yashd/server/src/session.c#L247)*

---

## Redirection over a socket

**Q: The daemon is an independent server process. How can `dup2` possibly redirect output to the client?**

Because a connected socket is just a file descriptor, and `fork` copies the descriptor
table. The child inherits the socket, `dup2(s->cfd, STDOUT_FILENO)` makes descriptor 1
refer to it, and `exec` preserves descriptors. The command then calls `write(1, ...)` as
it always does, unaware that descriptor 1 is a TCP connection rather than a terminal.

The daemon never copies those bytes — there is no relay loop. The kernel delivers them.

*Source: the `dup2` calls in [exec.c](yashd/server/src/exec.c#L31)*

**Q: Three things want to own descriptor 1. How is the conflict resolved?**

By ordering — last writer wins. The socket is attached first, the pipe `dup2` overrides
it on the piped side, and `redirect_apply` runs last so an explicit `> file` beats both.
That is why `ls > out.txt` sends nothing to the client, and why
`cat a > out | cat < b` starves the pipe.

*Source: [exec.c](yashd/server/src/exec.c#L31)*

**Q: Where does `> out.txt` actually create the file?**

On the server, in the daemon's working directory — `/tmp/out.txt`. The shell runs on the
server, so its filesystem is the server's. Only the keyboard and screen are remote. The
same applies to `<`: it reads a server-side file, and the client's local files are
invisible to it.

*Source: `redirect_apply` in [redirect.c](yashd/server/src/redirect.c)*

**Q: Why does the child close its own copy of the socket after the dups?**

Tidiness and correctness around EOF. Descriptors 1 and 2 still refer to the connection,
so closing the extra one changes nothing for the command, but it removes a redundant
reference. The connection stays open as long as any descriptor refers to it, which is why
a long-running background job keeps the client's socket alive.

*Source: [exec.c](yashd/server/src/exec.c#L31)*

**Q: Where does a command's stdin come from, now that there is no terminal?**

A pipe created per job. The parent keeps the write end as `job->infd` and forwards
plain-text messages into it; the child gets the read end on descriptor 0. `CTL d` closes
the write end, which is the EOF that lets `cat > file.txt` finish.

It is stored on the job rather than the session so that a job stopped with `CTL z` and
resumed with `fg` still has somewhere to receive input.

*Source: `handle_text` in [session.c](yashd/server/src/session.c#L194)*

**Q: Why do background jobs get their stdin closed immediately?**

Otherwise they would compete with the foreground job for whatever the user types next.
Closing the write end at fork makes them read EOF, which matches the way a real shell
denies terminal input to background processes.

*Source: [exec.c](yashd/server/src/exec.c#L31)*

---

## Job control without a terminal

**Q: Project 1 never forwarded a signal. Why does yashd have to?**

Project 1 relied on a fact that no longer holds: the terminal delivers SIGINT and SIGTSTP
to the foreground process group, so the shell only had to decide which group that was via
`tcsetpgrp`. A daemon has no controlling terminal, so nothing performs that routing. The
keystroke happens on the client's machine and the process is on the server, so the
daemon must re-create the signal itself with `kill(-pgid, SIGINT)`.

*Source: `handle_ctl` in [session.c](yashd/server/src/session.c#L162)*

**Q: Why can't `jobs_wait_fg` just block in `waitpid` like Project 1 did?**

Because the interrupt arrives on the socket. A thread blocked in `waitpid` is not reading
the socket, so a `CTL c` would sit unread in the kernel buffer until the job ended on its
own — making every foreground job uninterruptible. The loop therefore alternates
`session_pump`, which waits up to 50 ms on the socket, with a non-blocking poll of the
job.

*Source: `jobs_wait_fg` in [jobs.c](yashd/server/src/jobs.c#L213)*

**Q: What was the alternative, and why was it rejected?**

A second thread per session that owns the socket and signals jobs directly. Cleaner
control flow, but then two threads touch `session_t` and the job table needs a mutex
after all. The pump keeps the "one thread per client" structure the assignment asks for
and keeps the session lock-free; the cost is a 50 ms poll interval.

*Source: `session_pump` in [session.c](yashd/server/src/session.c#L255)*

**Q: Why is `waitpid(-1, ...)` wrong here when it was correct in Project 1?**

Child processes belong to the process, not to the thread that forked them. "Wait for any
child" therefore lets one session reap another session's child. The status is then
delivered to a thread that doesn't recognize the pid and is discarded, so the owning
client's job stays `Running` forever and never prints `Done`. Both wait paths use the
job's own process group instead.

*Source: `poll_job` in [jobs.c](yashd/server/src/jobs.c#L151)*

**Q: Process groups no longer receive terminal signals. Why keep `setpgid` at all?**

So that one `kill` reaches a whole pipeline. `kill(-pgid, SIGTSTP)` stops both halves of
`cat file | grep x` simultaneously; without a shared group the daemon would have to track
and signal each pid separately, and the two could stop at different moments.

*Source: the `setpgid` calls in [exec.c](yashd/server/src/exec.c#L31)*

**Q: What does `fg` mean when there is no terminal to hand over?**

It drops to its two remaining jobs: `SIGCONT` the process group, and mark the job as this
session's foreground so `CTL c` and `CTL z` are routed to it. Then it enters the same
pump-and-poll wait. The `tcsetpgrp` calls Project 1 made are simply deleted.

*Source: `do_fg` in [builtins.c](yashd/server/src/builtins.c)*

**Q: What happens to a running job if the client disappears mid-command?**

`session_pump` sees `read` return 0, the session marks itself finished and `SIGKILL`s the
foreground group, then switches to a blocking wait — nothing can interrupt the job any
more, so polling would just spin. On teardown `jobs_kill_all` kills and reaps every
remaining group, so no child outlives its client.

*Source: [jobs.c](yashd/server/src/jobs.c#L213), `jobs_kill_all` in [jobs.c](yashd/server/src/jobs.c#L249)*

**Q: Why `SIGKILL` on teardown rather than `SIGTERM`?**

It cannot be caught or ignored, and it takes effect on a stopped process without a
preceding `SIGCONT`. A `CTL z`'d job would survive a polite signal.

*Source: `jobs_kill_all` in [jobs.c](yashd/server/src/jobs.c#L249)*

**Q: Why does the child reset signal dispositions before `exec`?**

`exec` resets handlers automatically, but `SIG_IGN` survives it. The daemon ignores
`SIGPIPE` and `SIGHUP`, so without an explicit reset every command would inherit them
ignored — and a command with `SIGPIPE` ignored misbehaves inside a pipeline, continuing
to write after its reader is gone instead of dying.

*Source: `reset_child_signals` in [exec.c](yashd/server/src/exec.c#L16)*

---

## Logging

**Q: Why format the whole entry into a buffer and emit it with one `write`?**

Because several threads share the file descriptor. A sequence of `printf`-style calls
would let two threads interleave fragments of different entries, producing corrupted
lines. One `write` per entry makes each one atomic in practice; the mutex removes any
remaining doubt.

*Source: `log_command` in [log.c](yashd/server/src/log.c#L28)*

**Q: `O_APPEND` already makes writes atomic. Why the mutex as well?**

`O_APPEND` guarantees the offset update and the write happen atomically relative to other
writers, which covers the common case. The mutex costs nothing at this volume and makes
the guarantee independent of buffer sizes and filesystem behavior.

*Source: [log.c](yashd/server/src/log.c#L28)*

**Q: Why `localtime_r`?**

`localtime` returns a pointer to a shared static `struct tm`. Two threads logging
simultaneously would overwrite each other's time fields and produce wrong timestamps.
The `_r` variant writes into a caller-supplied struct on the stack.

*Source: [log.c](yashd/server/src/log.c#L28)*

**Q: Two clients on the same machine have the same IP. How are their log entries told apart?**

By the ephemeral port, which the kernel assigns per connection and which `accept`
reports. That is why the required format includes it rather than just the address.

*Source: `peer_from_tcp` in [transport.c](yashd/server/src/transport.c#L94)*

**Q: What is deliberately not logged?**

Control messages, plain-text stdin, and malformed command lines. The requirement is
commands executed; a `CTL c` is not a command, and a line that fails to parse was never
executed. Logging happens after `parse_line` succeeds and before the job runs.

*Source: `handle_cmd` in [session.c](yashd/server/src/session.c#L208)*

---

## The client

**Q: Why does the client need `select` at all? It only has one user.**

It has two input sources that can both block indefinitely: the keyboard and the socket.
Reading the keyboard first would freeze output from a long-running command; reading the
socket first would ignore typing. `select` waits on both and services whichever is ready.

*Source: `client_run` in [client.c](yashd/client/src/client.c#L165)*

**Q: Why `pselect` rather than `select`?**

To close a race. A signal arriving between the flag check and the `select` call would set
the flag and then leave the client blocked until unrelated input arrived — Ctrl-c would
appear to do nothing until the next keystroke. `pselect` takes a signal mask and unblocks
the signals atomically for the duration of the wait, so the handler can only run while
the client is inside the call.

*Source: the `sigprocmask`/`pselect` pair in [client.c](yashd/client/src/client.c#L165)*

**Q: Why do the signal handlers only set a flag?**

Writing to a socket from inside a handler is not async-signal-safe practice — `write` is
on the safe list, but the surrounding bookkeeping is not, and it is the kind of shortcut
that produces rare, unreproducible corruption. Setting a `volatile sig_atomic_t` and
acting on it in the main loop is both safe and easier to reason about.

*Source: `on_signal` in [client.c](yashd/client/src/client.c#L72)*

**Q: Why must the client catch SIGTSTP rather than letting it do the default thing?**

The default action suspends the client. But Ctrl-z is meant for the command running on
the server, so the client has to stay alive, translate the keystroke into `CTL z`, and
keep servicing the socket.

*Source: `client_install_handlers` in [client.c](yashd/client/src/client.c#L80)*

**Q: Why does the client treat EOF on stdin differently depending on `isatty`?**

On a terminal, `read` returning 0 means the user pressed Ctrl-d; it is not sticky, and the
next read blocks for new input as normal. On a pipe or file, EOF is permanent, so leaving
stdin in the `select` set would spin the loop re-reading EOF forever. The client drops
stdin from the set only in the non-tty case.

*Source: the `isatty` branch in [client.c](yashd/client/src/client.c#L165)*

**Q: How does the client detect the prompt if it never parses server output?**

It keeps a rolling three-byte tail of everything received and compares it against
`"\n# "`. That is deliberately not parsing — it imposes no structure on the stream and
survives the prompt arriving split across two reads.

*Source: `push_tail` in [client.c](yashd/client/src/client.c#L131)*

---

## Trade-offs and limits

**Q: What can't this shell do that Project 1 could?**

Anything needing a real terminal: `vim`, `top`, and password prompts all misbehave,
because there is no tty for them to put into raw mode or query for a window size. It also
has no access to the client's filesystem or working directory — relative paths resolve
under `/tmp` on the server.

**Q: What is the cost of the 50 ms poll interval?**

A foreground job's reaction to `CTL c` is delayed by up to 50 ms, and each session wakes
twenty times a second while a job runs. Negligible for this workload, but it is a real
difference from Project 1, where the kernel delivered the signal instantly and the shell
slept until something happened.

**Q: What would you change with more time?**

Replace the poll with `signalfd` or a self-pipe so the foreground wait can block on both
the socket and child status in a single `select`, removing the interval entirely. Second,
bound the number of concurrent sessions — right now every connection spawns a thread with
no cap.
