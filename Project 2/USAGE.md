# Using yashd and yash

Project 2 is two programs: `yashd`, a daemon that runs the shell, and `yash`, a
client that acts as the terminal. Both use Linux process and socket APIs, so
they must be built and run inside WSL, not PowerShell.

```powershell
wsl -d Ubuntu
```

## Build

```bash
cd "/mnt/c/Users/Isaac/Documents/System-prog/Project 2"
make
```

Both binaries land in `bin/`:

| | |
|---|---|
| `bin/yashd` | the daemon |
| `bin/yash` | the client |

`make server` and `make client` build one side alone. `make clean` removes
`build/` and `bin/`.

> The binaries go in `bin/` because a file named `yash` at the project root
> would collide with the `yash/` source directory.

## Two-terminal walkthrough

You need two WSL terminals: one to run the daemon and watch its log, one for
the client.

### Terminal 1 — start the daemon

```bash
cd "/mnt/c/Users/Isaac/Documents/System-prog/Project 2"

pkill -x yashd                      # clear any previous instance
rm -f /tmp/yashd.log /tmp/yashd.pid

./bin/yashd                         # detaches and returns immediately
pgrep -x yashd                      # should print one pid
```

`yashd` takes no arguments and daemonizes, so you get your prompt straight
back. That is correct, not a hang — it is now running detached with no
controlling terminal.

Watch the log live:

```bash
tail -f /tmp/yashd.log
```

### Terminal 2 — connect a client

```bash
cd "/mnt/c/Users/Isaac/Documents/System-prog/Project 2"
./bin/yash 127.0.0.1
```

The IP is the only argument; the port is hardcoded at 3820. You should see:

```
#
```

That prompt arrived over the socket. The client never prints one of its own.

As you run commands, each one appears in Terminal 1's log tagged with your
client's IP and ephemeral port:

```
Oct 12 09:14:02 yashd[127.0.0.1:52118]: echo hello
```

## Leaving the client

Press **Ctrl-d** at the prompt, or type `quit`. Any jobs that client started
are killed on the way out — there is no `nohup`-style survival.

The daemon keeps running. Stop it with:

```bash
pkill -x yashd
```

## Command syntax

Identical to Project 1. Every token must be separated by spaces, including the
operators:

```
# echo hello > out.txt          correct
# echo hello>out.txt            not parsed, ignored
```

One command per line, or two joined by a single `|`. Redirections follow the
command's arguments. `&` must be the last token, and cannot be combined with
`|`.

## File redirection

```bash
# echo written > out.txt        # creates or truncates
# cat < out.txt                 # reads stdin from the file
# cat < in.txt > copy.txt       # both at once
# ls nosuchfile 2> err.txt      # stderr to a file
```

**Relative paths resolve under `/tmp`, not your shell's directory.** The daemon
`chdir`s away from whoever launched it, so `> out.txt` writes `/tmp/out.txt`.
Check with `pwd`.

## Piping

```bash
# ls | wc -l
# cat in.txt | grep hello
# cat in.txt > saved.txt | cat < other.txt
```

Only one `|` per line. In the third example the left command's stdout goes to
the file rather than the pipe, so the redirection wins.

## Background jobs and job control

```bash
# sleep 30 &                    # prints nothing
# jobs
[1] + Running   sleep 30 &

# sleep 60                      # then press Ctrl-z
# jobs
[1] - Running   sleep 30 &
[2] + Stopped   sleep 60

# bg
[2] + Running   sleep 60 &

# fg
sleep 60
```

`fg` and `bg` take no arguments and always act on the most recent job, the one
marked `+`. A finished background job reports the next time you press Enter:

```
# sleep 2 &
#
[1] + Done      sleep 2 &
```

Each client has its own job table. Two clients running `sleep` never see each
other's jobs.

## Signals

| Key | Sent as | Effect |
|---|---|---|
| Ctrl-c | `CTL c` | Kills the foreground job **on the server**. The client is unaffected. |
| Ctrl-z | `CTL z` | Stops the foreground job and returns you to the prompt. |
| Ctrl-d at the prompt | `CTL d` | Ends the session and exits the client. |
| Ctrl-d while a command runs | `CTL d` | EOF on that command's stdin. The client stays connected. |

Ctrl-d is deliberately overloaded. Typing into a running command and then
pressing it is how you finish `cat > file.txt`:

```bash
# cat > typed.txt
first line
second line
                                # press Ctrl-d
# cat < typed.txt
first line
second line
```

Nothing is printed when a job is stopped or interrupted. Your terminal may
still echo `^C` or `^Z`, which is expected.

## Watching the log

```bash
tail -f /tmp/yashd.log
grep yashd /tmp/yashd.log
```

Entries are syslog-like, one per command executed:

```
Sep 18 10:02:55 yashd[127.0.0.1:7813]: ls -l
Sep 18 10:04:45 yashd[127.0.0.1:7815]: echo $PATH
```

Two clients on the same host are distinguished by the **port**, not the IP.
Control messages and malformed lines are not logged.

## Unix domain mode

For local testing, both sides take `-u` and use `/tmp/yashd.sock` instead of
TCP:

```bash
./bin/yashd -u
./bin/yash -u
```

Everything above the socket is identical. Only the log line differs: an
`AF_UNIX` client has no address, so its pid stands in for the port.

```
Oct 11 02:07:38 yashd[local:309]: echo unix-domain-works
```

## Talking to the server directly

The protocol is three message types, so `nc` is a perfectly good client:

```bash
nc 127.0.0.1 3820
```

Then type raw messages — the keywords are case-sensitive with exactly one
space after:

```
CMD echo straight from the wire
CMD ls -l
CTL d
```

This is useful for checking that the server behaves correctly independently of
the client.

## Manual test checklist

With a client connected, run these in order and confirm each result.

```bash
# echo hello                              -> hello
# which ls                                -> /usr/bin/ls
# pwd                                     -> /tmp
# nosuchprogram                           -> nothing, new prompt
# | ls                                    -> nothing, new prompt
# ls /nope                                -> error text, session alive
# echo written > t.txt
# cat < t.txt                             -> written
# ls | wc -l                              -> a number
# cat > typed.txt                         -> type 2 lines, then Ctrl-d
# cat < typed.txt                         -> the 2 lines
# sleep 30                                -> press Ctrl-z
# jobs                                    -> [1] + Stopped   sleep 30
# bg                                      -> [1] + Running   sleep 30 &
# fg                                      -> sleep 30, then press Ctrl-c
# jobs                                    -> nothing
# sleep 3 &
#                                         -> [1] + Done      sleep 3 &
# sleep 100 &
#                                         -> press Ctrl-d
```

After the last step, run `pgrep -a sleep` in a normal shell. It should print
nothing: every child dies with its client.

To confirm sessions are isolated, connect a second client and run `sleep 99 &`
in it. Each client's `jobs` shows only its own.

## Automated tests

```bash
cd "/mnt/c/Users/Isaac/Documents/System-prog/Project 2"
python3 test_yashd.py      # 30 checks, drives the daemon over a raw socket
python3 test_yash.py       # 35 checks, drives the client through a pty
```

`test_yashd.py` speaks the protocol directly, covering commands, redirection,
pipes, plain-text stdin, job control, per-session isolation, the log format,
and cleanup on disconnect.

`test_yash.py` runs the real client inside a pseudo-terminal so Ctrl-c, Ctrl-z
and Ctrl-d are delivered as actual keystrokes, then checks termination, error
handling, and that jobs die with the client.

Each script starts and stops its own daemon, so stop any running instance
first:

```bash
pkill -x yashd
```

## Troubleshooting

| Symptom | Cause |
|---|---|
| `make (e=2): The system cannot find the file specified` | You are in PowerShell, not WSL. Windows found a `make` on your PATH but there is no `gcc` for it to call. Run `wsl -d Ubuntu` first. |
| `Connection refused` | The daemon is not running. Check `pgrep -x yashd`. |
| Starting a second `yashd` seems to work but `pgrep -xc yashd` still prints 1 | Correct. The `lockf` on `/tmp/yashd.pid` rejected it. The exit code is 0 because the first `fork`'s parent exits before the lock is attempted. |
| `bind: Address already in use` | A previous daemon still holds the port. `pkill -x yashd`, and for `-u` mode `rm -f /tmp/yashd.sock`. |
| `pwd` prints `/tmp` | Expected. A daemon detaches from the launching directory. |
| `vim`, `top` or a password prompt misbehaves | Expected. There is no controlling terminal; anything needing a real tty will not work. |
| The client prints nothing and hangs | The server owes a prompt. Every completed message should be followed by `"\n# "`. |
