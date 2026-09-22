# yash — design review questions

Answers reference the implementation in `yash/src` and `yash/include`.

---

## Architecture

**Q: Walk me through what happens between pressing Enter and the command running.**

`main.c` reads the line with `fgets` and strips the newline. `parse_line` tokenizes it
into a `job_t`. `builtin_try` checks whether it's `jobs`, `fg`, or `bg` and handles it in
the shell process if so. Otherwise `exec_job` forks the children, wires the pipe and
redirections, puts them in a new process group, and either waits on them or records them
as a background job in `jobs.c`.

*Source: [main.c](Project%201/yash/src/main.c)*

**Q: Why split it into seven files instead of one?**

Each module owns exactly one failure mode, so a symptom maps to a file. Two boundaries
matter most: every `waitpid` call lives in `jobs.c`, and every signal disposition lives in
`sig.c`. Those are where job-control bugs concentrate, so there's exactly one file to
audit when something misbehaves. The split also keeps `parse.c` free of system calls,
which makes it testable in isolation.

*Source: [yash/src](Project%201/yash/src), [yash/include](Project%201/yash/include)*

**Q: Why does `job_t` hold two copies of the input line?**

`strtok_r` is destructive — it overwrites each separator with `\0`. The `argv` pointers
point into `buf` after that surgery, so `buf` is unusable for display. `cmdline` keeps a
pristine copy so `jobs` can print what the user actually typed. Without the second copy
you'd have to reconstruct the line from `argv`, which would lose the original spacing and
the redirection tokens entirely.

*Source: `job_t` in [yash.h](Project%201/yash/include/yash.h)*

**Q: Why fixed-size arrays instead of dynamic allocation in the parser?**

The assignment specified capped lines at 200 characters. Since operators are space-separated, that bounds
tokens at 128, which bounds `argv`. Parsing therefore needs no allocation at all, which
removes an entire class of leak and error-handling paths from the hot path.

*Source: `YASH_MAX_*` in [yash.h](Project%201/yash/include/yash.h)*

---

## Parsing

**Q: How do you know a token is a redirection target and not an argument?**

Position. The scan is a single left-to-right pass; when it sees `<`, `>`, or `2>` it
consumes the *next* token into the matching `cmd_t` field and continues, so that token
never reaches `argv`. The spec's guarantee that redirections follow all arguments is what
makes one pass sufficient — no backtracking, no state machine.

*Source: `parse_line` in [parse.c](Project%201/yash/src/parse.c)*

**Q: What makes a line invalid?**

Seven conditions: a redirection with no command to attach to, the same direction
redirected twice, a missing redirection target, an operator used as a target, a plain word
after a redirection, a second `|`, an `&` that isn't the final token, and `|` combined
with `&`. All return `PARSE_ERROR`, and `main` silently ignores the line.

*Source: the `PARSE_ERROR` returns in [parse.c](Project%201/yash/src/parse.c); ignored in [main.c](Project%201/yash/src/main.c)*

**Q: Why is `> ls` invalid but `ls > out.txt` valid?**

`redirect_slot` matches first, then the code checks `cur->argc == 0`. On `> ls` no command
word has been seen yet, so there is nothing to attach the redirection to. This matches the
FAQ's rule that input must begin with a command.

*Source: `redirect_slot` and the `cur->argc == 0` guard in [parse.c](Project%201/yash/src/parse.c)*

**Q: You strip the `&` from `cmdline` during parsing. Why?**

So job output can reattach it uniformly. A job backgrounded by `bg` was never typed with
`&`, but the FAQ requires `bg` to print the jobs-format line *with* a trailing `&`. By
storing the line without it and appending ` &` whenever `job->background` is set, both
cases produce identical output from one code path. Keeping the `&` verbatim instead would
print `sleep 5 & &` for jobs started with `&`.

*Source: the trailing-`&` trim in [parse.c](Project%201/yash/src/parse.c); reattached by `jobs_print_one` in [jobs.c](Project%201/yash/src/jobs.c)*

**Q: Why `strtok_r` instead of `strtok`?**

`strtok` keeps its scan position in a hidden global. `strtok_r` takes a `save` pointer
instead, so nested or repeated tokenizing can't corrupt each other's state. It costs
nothing and removes a footgun.

*Source: `tokenize` in [parse.c](Project%201/yash/src/parse.c)*

---

## File redirection

**Q: How does `>` actually replace stdout?**

File descriptors are just indices into the process's descriptor table. `dup2(fd, 1)` makes
index 1 refer to the same open file as `fd`, closing whatever 1 previously was. The
program then writes to descriptor 1 as always, unaware anything changed — which is why
redirection works on any program without cooperation from it.

*Source: `open_onto` in [redirect.c](Project%201/yash/src/redirect.c)*

**Q: What flags does `>` use, and why each one?**

`O_WRONLY | O_CREAT | O_TRUNC`. `O_CREAT` satisfies "create the file if it doesn't exist";
`O_TRUNC` discards old contents so a re-run doesn't leave stale tail data; `O_WRONLY`
because the command only writes.

*Source: `out_flags` in [redirect.c](Project%201/yash/src/redirect.c)*

**Q: How do you make a redirected input fail when the file is missing?**

Nothing special — `open(file, O_RDONLY)` already returns `-1` with `errno == ENOENT`.
`redirect_apply` returns `-1` and the child calls `_exit(1)` without ever reaching
`execvp`.

*Source: `redirect_apply` in [redirect.c](Project%201/yash/src/redirect.c); the `_exit(1)` in [exec.c](Project%201/yash/src/exec.c)*

**Q: Why not check the file exists first with `stat` or `access`?**

That's a time-of-check/time-of-use race: the file could be deleted between the check and
the `open`. Letting `open` fail is both simpler and atomic. This is a standard security
pattern, not just a style preference.

*Source: header comment on `redirect_apply` in [redirect.h](Project%201/yash/include/redirect.h)*

**Q: Why does redirection happen in the child rather than the parent?**

Two reasons. The shell's own stdin/stdout must not be disturbed — redirecting in the
parent would permanently break the shell. And a failed `open` must kill only that command;
in the child it's a local `_exit`, in the parent it would be a shell-wide problem.

*Source: the `redirect_apply` call inside the fork branch of [exec.c](Project%201/yash/src/exec.c)*

**Q: Why does the mode argument matter if the file already exists?**

It doesn't — `open` ignores the mode unless `O_CREAT` actually creates the file. It
matters only on first creation, where omitting it leaves the permission bits as whatever
garbage was on the stack, producing files you can't read back.

*Source: `out_mode` in [redirect.c](Project%201/yash/src/redirect.c)*

**Q: Can a command have both `<` and `>`?**

Yes. They're independent fields on `cmd_t` and `redirect_apply` applies them in sequence
onto descriptors 0 and 1, which never conflict. `2>&1` is explicitly out of scope and is
treated as an unrecognized token.

*Source: `cmd_t` in [yash.h](Project%201/yash/include/yash.h); applied in [redirect.c](Project%201/yash/src/redirect.c)*

---

## Piping

**Q: Describe the pipe setup.**

`pipe(fds)` fills `fds[0]` with the read end and `fds[1]` with the write end. The left
child does `dup2(fds[1], STDOUT_FILENO)`, the right child does `dup2(fds[0], STDIN_FILENO)`,
and then both children close both raw descriptors. The parent closes both too, after
forking.

*Source: `exec_job` in [exec.c](Project%201/yash/src/exec.c)*

**Q: Why close the pipe descriptors in three separate places?**

A pipe reader sees EOF only when *every* copy of the write end is closed. `fork` duplicates
the descriptor table, so after two forks the write end exists in the parent and in both
children. Miss any one of those closes and the reader blocks forever — `ls | wc` would
hang with no error message. This is the single most common pipe bug.

*Source: the two `close(fds[...])` pairs in [exec.c](Project%201/yash/src/exec.c)*

**Q: What happens with `cat in.txt > out.txt | cat < other.txt`?**

The left command's stdout goes to the file, not the pipe, so the pipe carries nothing and
the right command sees only `other.txt`. This falls out of ordering: `redirect_apply` runs
*after* the pipe `dup2`, so the file redirection overwrites descriptor 1 and the pipe's
write end ends up fully closed in that child. The FAQ calls out this exact case.

*Source: the dup2-then-`redirect_apply` order in [exec.c](Project%201/yash/src/exec.c); test "left > file starves the pipe" in [test_yash.sh](Project%201/test_yash.sh)*

**Q: How are both pipeline members stopped simultaneously?**

They share a process group. The first child's pid becomes the pgid and the second child
joins it, so the terminal delivers SIGTSTP to the group, not to an individual process.
One signal, both processes.

*Source: the `pgid` assignment in [exec.c](Project%201/yash/src/exec.c)*

**Q: Why is `setpgid` called in both the parent and the child?**

It's a race. After `fork`, either process may run first. If only the child called it, the
parent might `tcsetpgrp` or signal the group before the child had set it. If only the
parent called it, the child might `exec` and receive a signal first. Calling it in both
makes the outcome identical regardless of scheduling — the second call is a harmless no-op.

*Source: the two `setpgid` calls in [exec.c](Project%201/yash/src/exec.c)*

**Q: Why is only one pipe supported?**

The spec caps it, so `YASH_MAX_CMDS` is 2 and a second `|` is a parse error. Generalizing
would mean an array of commands and a loop carrying the previous read end forward — the
structure is already shaped for that, but it's out of scope.

*Source: `YASH_MAX_CMDS` in [yash.h](Project%201/yash/include/yash.h); enforced in [parse.c](Project%201/yash/src/parse.c)*

---

## Signals

**Q: How does Ctrl-c kill the job but not the shell?**

The terminal sends SIGINT to the *foreground process group*. Before waiting, the shell
calls `tcsetpgrp(0, job->pgid)`, which makes the job that group; the shell is now in a
background group and doesn't receive the signal at all. There's no forwarding logic —
the kernel does the routing.

*Source: `sig_give_terminal` in [sig.c](Project%201/yash/src/sig.c); called from [exec.c](Project%201/yash/src/exec.c)*

**Q: So what does the shell's SIGINT handler do?**

Almost nothing. It exists only for signals typed at the prompt, when no job is running and
the shell *is* the foreground group. It's an empty function.

*Source: `noop_handler` in [sig.c](Project%201/yash/src/sig.c)*

**Q: Why an empty handler instead of `SIG_IGN`?**

Because of `EINTR`. The handler is installed without `SA_RESTART`, so a signal arriving
during `fgets` causes it to return `NULL` with the error flag set; `main` detects that,
clears the flag, prints a newline, and reprompts. `SIG_IGN` would not interrupt the read
at all, leaving the cursor parked after the echoed `^C`.

*Source: `set_handler` and its `sa_flags = 0` in [sig.c](Project%201/yash/src/sig.c); the reprompt in [main.c](Project%201/yash/src/main.c)*

**Q: How does `main` tell Ctrl-d from Ctrl-c, since `fgets` returns `NULL` for both?**

`feof(stdin)`. True means genuine end-of-file, so the shell exits. False means the read was
interrupted, so it clears the error and continues.

*Source: the `feof` branch in [main.c](Project%201/yash/src/main.c)*

**Q: Why must the child reset signal dispositions before `exec`?**

`exec` resets *handlers* to default automatically, but `SIG_IGN` survives it. The shell
ignores SIGTTIN and SIGTTOU, so without `sig_reset_child` a child would inherit those
ignored — and more importantly, this makes the reset explicit and total rather than relying
on subtle exec semantics.

*Source: `sig_reset_child` in [sig.c](Project%201/yash/src/sig.c)*

**Q: Why is SIGTTOU ignored?**

A process in a background group that calls `tcsetpgrp` is sent SIGTTOU, whose default
action stops it. The shell must call `tcsetpgrp` to hand the terminal back after a job
finishes — at which point it *is* in a background group. Without ignoring SIGTTOU, the
shell would stop itself.

*Source: `sig_init_shell` in [sig.c](Project%201/yash/src/sig.c)*

**Q: You have no SIGCHLD handler. Isn't that required?**

The requirement is to handle child state changes, not to use a handler. `jobs_reap` polls
with `waitpid(-1, &st, WNOHANG | WUNTRACED | WCONTINUED)` at the top of the read loop.
That's deliberate: `printf` is not async-signal-safe, so printing `Done` lines from a
handler would be undefined behavior. Polling also places those lines exactly where the
spec wants them — after the user's newline, before the next prompt.

*Source: `jobs_reap` in [jobs.c](Project%201/yash/src/jobs.c); rationale comment in [sig.c](Project%201/yash/src/sig.c)*

**Q: What's the risk of the polling approach?**

Zombies live slightly longer — until the next prompt rather than the instant they die.
For an interactive shell that window is imperceptible and nothing is leaked, since the
loop always runs before prompting.

*Source: the `jobs_reap()` call at the top of the loop in [main.c](Project%201/yash/src/main.c)*

---

## Job control

**Q: What is a job, precisely?**

Everything typed on one line, which is one or two commands sharing a process group. The
pgid is the identity: it's what `kill` targets, what `tcsetpgrp` hands the terminal to,
and what `waitpid(-pgid, ...)` waits on.

*Source: `job_t` in [yash.h](Project%201/yash/include/yash.h)*

**Q: Why is a foreground job put in the job table at all?**

Ctrl-z can turn it into a stopped job that `fg` and `bg` must find later. If only
background jobs were tracked, a stopped foreground job would be orphaned and unreachable.
Foreground jobs that exit normally are removed immediately after the wait.

*Source: the `jobs_add` / `jobs_remove` pair in [exec.c](Project%201/yash/src/exec.c)*

**Q: Explain `rebase_pointers`.**

`parse_line` fills a `job_t` on `main`'s stack, and its `argv`, `infile`, and `outfile`
pointers all point into that struct's `buf`. `jobs_add` copies the struct to the heap, but
a plain copy leaves every one of those pointers aimed at the old stack buffer, which is
reused on the next loop iteration. `rebase_pointers` rewrites each as
`new_buf + (old_pointer - old_buf)`. Skip it and you get dangling pointers that appear to
work — the stack data is often still intact — and then corrupt at the worst possible time.

*Source: `rebase_pointers` in [jobs.c](Project%201/yash/src/jobs.c)*

**Q: You have two waiting functions. Why?**

They answer different questions.

| | `jobs_wait_fg` | `jobs_reap` |
|---|---|---|
| Blocks | yes | no (`WNOHANG`) |
| Target | `-pgid`, one job | `-1`, any child |
| Purpose | foreground jobs and `fg` | background jobs, `Done` lines |

*Source: both in [jobs.c](Project%201/yash/src/jobs.c)*

**Q: What do `WUNTRACED` and `WCONTINUED` do?**

By default `waitpid` reports only termination. `WUNTRACED` adds stopped children, which is
what makes Ctrl-z observable. `WCONTINUED` adds resumed children, so `jobs_reap` notices a
job restarted by `bg`.

*Source: the `waitpid` flags in [jobs.c](Project%201/yash/src/jobs.c)*

**Q: Why does `jobs_wait_fg` return immediately on `WIFSTOPPED` without decrementing
`nlive`?**

The process is stopped, not dead — it still exists and will be waited on later. And since
pipeline members share a pgid, one member stopping means both stopped, so there's nothing
left to wait for. Returning hands the terminal back and reprompts.

*Source: `jobs_wait_fg` in [jobs.c](Project%201/yash/src/jobs.c)*

**Q: Why retry on `EINTR`?**

A signal arriving during the blocking `waitpid` makes it return `-1` with `errno == EINTR`.
That isn't a failure — no child changed state — so treating it as one would abandon a job
that's still running.

*Source: the `errno == EINTR` branch in [jobs.c](Project%201/yash/src/jobs.c)*

**Q: How are job numbers assigned, and why that rule?**

`tail->jid + 1`. The list is append-ordered, so the tail always holds the highest number.
That reproduces bash's rule of "1 + highest current number": with jobs 1, 2, 4 present a
new job becomes 5, but once 4 and 5 finish the next one is 3 again. zsh fills the gap
immediately, and the FAQ says to follow bash.

*Source: `jobs_add` in [jobs.c](Project%201/yash/src/jobs.c); test "next jid is 1 + highest" in [test_yash.sh](Project%201/test_yash.sh)*

**Q: How is the `+` job defined?**

The most recently created job still Running or Stopped. `jobs_current` is the single
definition, used by both `jobs` for the marker and `fg` for its target, so the marker can
never disagree with what `fg` will actually do.

*Source: `jobs_current` in [jobs.c](Project%201/yash/src/jobs.c)*

**Q: Why can't `bg` reuse `jobs_current`?**

`bg` resumes stopped jobs only. If the most recent job is already running in the
background, `bg` must skip it and take the most recent *stopped* one, so it needs its own
search.

*Source: `jobs_recent_stopped` in [jobs.c](Project%201/yash/src/jobs.c); used by `do_bg` in [builtins.c](Project%201/yash/src/builtins.c)*

**Q: Why must `jobs`, `fg`, and `bg` be builtins?**

They operate on the shell's own state — the job table and terminal ownership. A forked
child would mutate a copy and exit, discarding the change. `builtin_try` is therefore
called before `exec_job`.

*Source: `builtin_try` in [builtins.c](Project%201/yash/src/builtins.c); dispatch order in [main.c](Project%201/yash/src/main.c)*

**Q: Why `kill(-pgid, ...)` with a negative pid?**

A negative pid means "every process in group `pgid`". That resumes or kills both halves of
a pipeline together rather than leaving one half stranded.

*Source: the `kill` calls in [builtins.c](Project%201/yash/src/builtins.c) and `jobs_kill_all` in [jobs.c](Project%201/yash/src/jobs.c)*

**Q: How is the 20-job maximum enforced?**

`jobs_add` counts live jobs and returns `NULL` at the cap, and `exec_job` reports it.
The table is a linked list, so 20 is a policy limit rather than a structural one.

*Source: `YASH_MAX_JOBS` in [yash.h](Project%201/yash/include/yash.h); checked in `jobs_add` in [jobs.c](Project%201/yash/src/jobs.c)*

**Q: Why is `Done` printed from `jobs_reap` rather than when the child dies?**

The shell doesn't know the child died until it waits. Polling at the top of the loop is
what makes the message appear after the user's newline, as specified, instead of
interrupting a half-typed line.

*Source: the `JOB_DONE` sweep in `jobs_reap` in [jobs.c](Project%201/yash/src/jobs.c)*

---

## Misc

**Q: How do children inherit the environment?**

`fork` copies it, and `execvp` passes the existing `environ` through. It's automatic —
using `execve` with an explicit envp would be the way to *break* it.

*Source: the `execvp` call in [exec.c](Project%201/yash/src/exec.c)*

**Q: Where does the PATH search happen?**

Inside `execvp`. The `p` suffix means it searches PATH when the filename contains no `/`;
the `v` means arguments are passed as a NULL-terminated vector. Using `execv` would require
resolving PATH by hand.

*Source: the `execvp` call in [exec.c](Project%201/yash/src/exec.c)*

**Q: Why `_exit` instead of `exit` in a failed child?**

`exit` flushes stdio buffers. The child inherited a copy of the parent's buffers at `fork`,
so flushing could re-emit output the parent has already queued, duplicating it.
`_exit` terminates without touching them.

*Source: the `_exit(1)` and `_exit(127)` calls in [exec.c](Project%201/yash/src/exec.c)*

**Q: Why does `execvp` print nothing on failure?**

The FAQ requires an unknown program to produce no output beyond a newline, unlike bash's
"command not found". The `perror` call was removed for exactly this case.

*Source: the comment above `execvp` in [exec.c](Project%201/yash/src/exec.c); test "unknown program" in [test_yash.sh](Project%201/test_yash.sh)*

**Q: How are children guaranteed dead on exit?**

`jobs_kill_all` sends SIGKILL to each job's process group at the end of `main`. SIGKILL is
used because it cannot be caught or ignored and it takes effect on stopped processes
without a preceding SIGCONT — SIGHUP or SIGTERM could be handled or slept through.

*Source: `jobs_kill_all` in [jobs.c](Project%201/yash/src/jobs.c); called at the end of [main.c](Project%201/yash/src/main.c)*

**Q: Why `fflush` after printing the prompt?**

The prompt has no newline, and stdout to a terminal is line-buffered, so it would sit in
the buffer while the shell blocked in `fgets` — the user would stare at a blank screen.

*Source: the `fputs` / `fflush` pair in [main.c](Project%201/yash/src/main.c)*

---

## Curveballs

**Q: What breaks if you remove `fflush(stdout)` after the prompt?**

The prompt appears only after the next newline is written, so it looks like the shell
hangs on startup and prompts lag one command behind.

*Source: [main.c](Project%201/yash/src/main.c)*

**Q: What if you forgot `setpgid` entirely?**

Children stay in the shell's process group. Ctrl-c would then hit the shell and every job
at once, and `tcsetpgrp` would have nothing distinct to hand the terminal to. Job control
collapses completely.

*Source: [exec.c](Project%201/yash/src/exec.c), [sig.c](Project%201/yash/src/sig.c)*

**Q: What if the parent forgot to close the pipe's write end?**

The read end never sees EOF because a copy of the write end is still open in the shell.
`ls | wc` hangs forever with no error.

*Source: the post-fork `close(fds[...])` pair in [exec.c](Project%201/yash/src/exec.c)*

**Q: Where could a buffer overflow hide, and why doesn't it?**

`strcpy(job->cmdline, line)` is unbounded, but `parse_line` rejects any line longer than
`YASH_MAX_LINE` before reaching it, and the destination is sized `YASH_MAX_LINE + 1`. The
`argv` write is guarded by `cur->argc == YASH_MAX_ARGS`, and the token array by the
`n == max` check in `tokenize`.

*Source: the length guards in [parse.c](Project%201/yash/src/parse.c); buffer sizes in [yash.h](Project%201/yash/include/yash.h)*

**Q: Is there a memory leak?**

No. Jobs are the only heap allocation; each is freed in `jobs_remove` when it finishes or
is removed, and `jobs_kill_all` frees whatever remains at exit.

*Source: `malloc` in `jobs_add`, `free` in `jobs_remove` and `jobs_kill_all`, all in [jobs.c](Project%201/yash/src/jobs.c)*

**Q: What would it take to support unlimited pipes?**

Replace `cmds[2]` with a growable array, then loop: keep the previous read end, create a
new pipe for each subsequent command, dup the carried-over read end onto stdin and the new
write end onto stdout, and close the carried end after each fork. The pgid and job-table
logic wouldn't change at all, since they already treat a job as N processes rather than
exactly two.

*Source: `YASH_MAX_CMDS` in [yash.h](Project%201/yash/include/yash.h); the fork loop in [exec.c](Project%201/yash/src/exec.c)*

**Q: What's the weakest part of the design?**

`jobs_reap` runs only at the prompt, so job state is stale while a foreground job is
running. It doesn't affect correctness here — the foreground wait tracks its own job — but
a shell with more features would want state updated more eagerly.

*Source: `jobs_reap` in [jobs.c](Project%201/yash/src/jobs.c); its single call site in [main.c](Project%201/yash/src/main.c)*
