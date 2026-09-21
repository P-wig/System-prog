# Using yash

## Build and run

`yash` uses Linux process APIs, so it must be built and run inside WSL, not PowerShell.

```powershell
wsl -d Ubuntu
```

```bash
cd "/mnt/c/Users/Isaac/Documents/System-prog/Project 1/yash"
make
./yash
```

You should see the prompt:

```
#
```

`make clean` removes `build/` and the `yash` binary.

> Run it from a real terminal, not by piping input into it. Job control needs a
> controlling terminal, so `fg`, `bg`, Ctrl-z and Ctrl-c only behave correctly
> when `yash` owns a tty.

## Leaving the shell

Press **Ctrl-d** at the prompt. Any jobs still running or stopped are killed on
the way out. There is no `exit` command.

## Command syntax

Every token must be separated by spaces, including the operators:

```
# echo hello > out.txt          correct
# echo hello>out.txt            not parsed, ignored
```

A line may contain one command, or two commands joined by a single `|`.
Redirections come after the command's arguments. `&` must be the very last
token.

## File redirection

```bash
# echo hello > out.txt          # creates or truncates out.txt
# cat < out.txt                 # reads stdin from out.txt
# cat < in.txt > copy.txt       # both at once
# ls nosuchfile 2> err.txt      # stderr to a file
```

Reading from a file that does not exist fails that command only; the shell keeps
running.

## Piping

```bash
# ls | wc -l
# cat in.txt | grep hello
# cat in.txt > saved.txt | cat < other.txt
```

Only one `|` is allowed. In the third example the left command's stdout goes to
the file rather than the pipe, so the right command sees only `other.txt` — the
redirection wins over the pipe.

## Background jobs and job control

```bash
# sleep 30 &                    # runs in the background, prints nothing
# jobs                          # list the job table
[1] + Running   sleep 30 &

# sleep 60                      # then press Ctrl-z
# jobs
[1] - Running   sleep 30 &
[2] + Stopped   sleep 60

# bg                            # resume the most recent stopped job
[2] + Running   sleep 60 &

# fg                            # bring the most recent job forward
sleep 60
```

`&` and `|` cannot be combined. `fg` and `bg` take no arguments; they always act
on the most recent job, which is the one marked `+`.

When a background job finishes, the `Done` line appears the next time you press
Enter:

```
# sleep 2 &
#
[1] + Done      sleep 2 &
```

## Signals

| Key | Effect |
|---|---|
| Ctrl-c | Kills the foreground job. The shell is unaffected. |
| Ctrl-z | Stops the foreground job and returns you to the prompt. |
| Ctrl-d | Exits the shell. |

Nothing is printed when a job is stopped or interrupted. Your terminal may still
echo `^C` or `^Z`, which is expected.

## Manual test checklist

Paste these in order and confirm each result.

```bash
# echo hello                                  -> hello
# echo "hello world"                          -> "hello world"   (quotes kept)
# which ls                                    -> /usr/bin/ls
# nosuchprogram                               -> nothing, new prompt
# < ls                                        -> nothing, new prompt
# ls | cat | cat                              -> nothing, new prompt
# echo written > t.txt
# cat < t.txt                                 -> written
# cat < missing.txt                           -> error text, shell alive
# ls | wc -l                                  -> a number
# sleep 30                                    -> press Ctrl-z
# jobs                                        -> [1] + Stopped   sleep 30
# bg                                          -> [1] + Running   sleep 30 &
# jobs                                        -> [1] + Running   sleep 30 &
# fg                                          -> sleep 30, then press Ctrl-c
# jobs                                        -> nothing
# sleep 3 &
#                                             -> [1] + Done      sleep 3 &
# sleep 100 &
#                                             -> press Ctrl-d
```

After the last step, run `pgrep -a sleep` in your normal shell. It should print
nothing: every child dies when `yash` exits.

## Automated tests

```bash
cd "/mnt/c/Users/Isaac/Documents/System-prog/Project 1"
bash test_yash.sh
```

This runs 70 checks covering parsing, redirection, pipes, invalid input, the job
table, every example case from the rubric, and — through a pseudo-terminal —
Ctrl-c, Ctrl-z, `fg`, `bg`, and a stress scenario that shuffles several jobs
between the foreground, the background, and stopped.
