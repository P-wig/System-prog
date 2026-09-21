#!/usr/bin/env bash
#
# Test harness for yash. Run from anywhere:  bash test_yash.sh
# Interactive job-control cases need a pty and are driven through python3.

YASH_DIR="$(cd "$(dirname "$0")/yash" && pwd)"
YASH="$YASH_DIR/yash"
WORK="$(mktemp -d)"
PASS=0
FAIL=0

cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

# --- helpers ---------------------------------------------------------------

# Feeds each argument as one input line, returns stdout with prompts stripped.
run_yash() {
    printf '%s\n' "$@" | "$YASH" 2>/dev/null | sed -E 's/^(# )+//' | sed '/^[[:space:]]*$/d'
}

check() {
    local desc="$1" expected="$2" actual="$3"
    if [ "$expected" = "$actual" ]; then
        PASS=$((PASS + 1))
        printf '  ok   %s\n' "$desc"
    else
        FAIL=$((FAIL + 1))
        printf '  FAIL %s\n' "$desc"
        printf '         expected: %s\n' "$(printf '%s' "$expected" | tr '\n' '|')"
        printf '         actual:   %s\n' "$(printf '%s' "$actual" | tr '\n' '|')"
    fi
}

check_match() {
    local desc="$1" pattern="$2" actual="$3"
    if printf '%s' "$actual" | grep -Eq "$pattern"; then
        PASS=$((PASS + 1))
        printf '  ok   %s\n' "$desc"
    else
        FAIL=$((FAIL + 1))
        printf '  FAIL %s\n' "$desc"
        printf '         pattern:  %s\n' "$pattern"
        printf '         actual:   %s\n' "$(printf '%s' "$actual" | tr '\n' '|')"
    fi
}

check_nomatch() {
    local desc="$1" pattern="$2" actual="$3"
    if printf '%s' "$actual" | grep -Eq "$pattern"; then
        FAIL=$((FAIL + 1))
        printf '  FAIL %s\n' "$desc"
        printf '         should not match: %s\n' "$pattern"
        printf '         actual:           %s\n' "$(printf '%s' "$actual" | tr '\n' '|')"
    else
        PASS=$((PASS + 1))
        printf '  ok   %s\n' "$desc"
    fi
}

section() { printf '\n%s\n' "$1"; }

# --- build -----------------------------------------------------------------

section "build"
if ! make -C "$YASH_DIR" clean >/dev/null 2>&1 || ! make -C "$YASH_DIR" >"$WORK/build.log" 2>&1; then
    printf '  FAIL make failed\n'
    cat "$WORK/build.log"
    exit 1
fi
if grep -qiE 'warning|error' "$WORK/build.log"; then
    FAIL=$((FAIL + 1))
    printf '  FAIL build produced diagnostics\n'
    grep -iE 'warning|error' "$WORK/build.log"
else
    PASS=$((PASS + 1))
    printf '  ok   builds clean\n'
fi
[ -x "$YASH" ] && { PASS=$((PASS + 1)); printf '  ok   executable named yash\n'; } \
               || { FAIL=$((FAIL + 1)); printf '  FAIL no yash executable\n'; }

cd "$WORK" || exit 1
printf 'FROM_IN1\n' > in1.txt
printf 'FROM_IN2\n' > in2.txt

# --- basics ----------------------------------------------------------------

section "basics"
check "simple command" "hello" "$(run_yash 'echo hello')"
check "args are passed through" "a b c" "$(run_yash 'echo a b c')"
check "PATH is searched" "/usr/bin/ls" "$(run_yash 'which ls')"
YASH_ENV_PROBE=inherited; export YASH_ENV_PROBE
check "environment inherited" "inherited" "$(run_yash 'printenv YASH_ENV_PROBE')"
check "quotes are literal, not stripped" '"hello world"' "$(run_yash 'echo "hello world"')"
check_match "prompt is '# '" '^# $' "$(printf '' | "$YASH" | tr -d '\n')"

# --- file redirection ------------------------------------------------------

section "file redirection"
rm -f out.txt
run_yash 'echo redirected > out.txt' >/dev/null
check "> creates the file" "redirected" "$(cat out.txt 2>/dev/null)"
check "< reads the file" "FROM_IN1" "$(run_yash 'cat < in1.txt')"
rm -f both.txt
run_yash 'cat < in1.txt > both.txt' >/dev/null
check "< and > on one command" "FROM_IN1" "$(cat both.txt 2>/dev/null)"
printf 'old content here\n' > trunc.txt
run_yash 'echo new > trunc.txt' >/dev/null
check "> truncates existing file" "new" "$(cat trunc.txt 2>/dev/null)"
check "< on missing file produces no stdout" "" "$(run_yash 'cat < nothing_here.txt')"
rm -f perm.txt
run_yash 'echo p > perm.txt' >/dev/null
check_match "created file is readable" '^-rw' "$(ls -l perm.txt 2>/dev/null)"
check "2> captures stderr" "" "$(run_yash 'ls nothing_here 2> err.txt')"
check_match "stderr file has content" '.' "$(cat err.txt 2>/dev/null)"

# --- piping ----------------------------------------------------------------

section "piping"
check "pipe passes stdout to stdin" "FROM_IN1" "$(run_yash 'cat in1.txt | cat')"
check "pipe with counting" "3" "$(run_yash 'printf a\nb\nc\n | wc -l' 2>/dev/null || run_yash 'seq 3 | wc -l')"
rm -f out1.txt
OUT="$(run_yash 'cat in1.txt > out1.txt | cat < in2.txt')"
check "redirection overrides pipe (stdout side)" "FROM_IN2" "$OUT"
check "redirection overrides pipe (file side)" "FROM_IN1" "$(cat out1.txt 2>/dev/null)"
rm -f o1.txt o2.txt
run_yash 'cat < in1.txt > o1.txt | cat < in2.txt > o2.txt' >/dev/null
check "redirection on both ends of pipe" "FROM_IN1 FROM_IN2" \
      "$(cat o1.txt 2>/dev/null) $(cat o2.txt 2>/dev/null)"

# --- invalid input ---------------------------------------------------------

section "invalid input (must be silently ignored)"
check "unknown program" "" "$(run_yash 'nosuchprogram_xyz')"
check "line starting with <" "" "$(run_yash '< ls')"
check "line starting with >" "" "$(run_yash '> ls')"
check "line starting with |" "" "$(run_yash '| ls')"
check "line starting with &" "" "$(run_yash '& ls')"
check "two pipes" "" "$(run_yash 'ls | cat | cat')"
check "| and & together" "" "$(run_yash 'ls | cat &')"
check "& not last token" "" "$(run_yash 'ls & cat')"
check "duplicate >" "" "$(run_yash 'cat in1.txt > a.txt > b.txt')"
check "duplicate <" "" "$(run_yash 'cat < in1.txt < in2.txt')"
check "redirection with no target" "" "$(run_yash 'cat >')"
check "empty line" "" "$(run_yash '')"
check "line over 200 chars" "" "$(run_yash "echo $(head -c 250 < /dev/zero | tr '\0' 'x')")"
check "shell survives a bad line" "still alive" "$(run_yash '| ls' 'echo still alive')"

# --- job control (non-interactive) -----------------------------------------

section "job control"
check "& prints nothing" "" "$(run_yash 'sleep 0.3 &')"
check_match "jobs lists a background job" '\[1\] \+ Running[[:space:]]+sleep 5 &' \
      "$(run_yash 'sleep 5 &' 'jobs')"
check_match "multiple jobs get + on the newest" \
      '\[1\] - Running[[:space:]]+sleep 5 &' \
      "$(run_yash 'sleep 5 &' 'sleep 6 &' 'jobs')"
check_match "newest job marked +" '\[2\] \+ Running[[:space:]]+sleep 6 &' \
      "$(run_yash 'sleep 5 &' 'sleep 6 &' 'jobs')"
check_match "finished job reports Done" '\[1\] \+ Done[[:space:]]+sleep 0.2 &' \
      "$(run_yash 'sleep 0.2 &' 'sleep 1' '')"
check_nomatch "Done job leaves the table" 'Running|Stopped' \
      "$(run_yash 'sleep 0.2 &' 'sleep 1' 'jobs')"
check_match "next jid is 1 + highest" '\[4\] \+ Running[[:space:]]+sleep 7 &' \
      "$(run_yash 'sleep 0.2 &' 'sleep 5 &' 'sleep 6 &' 'sleep 1' 'sleep 7 &' 'jobs')"
check "fg with no jobs is silent" "" "$(run_yash 'fg')"
check "bg with no jobs is silent" "" "$(run_yash 'bg')"

section "children dead on exit"
printf 'sleep 313 &\nsleep 314 &\n' | "$YASH" >/dev/null 2>&1
sleep 0.3
LEFTOVER="$(pgrep -f 'sleep 31[34]' | wc -l)"
check "no surviving children" "0" "$LEFTOVER"
pkill -f 'sleep 31[34]' 2>/dev/null

# --- interactive signal / job control via pty ------------------------------

section "interactive (pty)"
if command -v python3 >/dev/null 2>&1; then
    PTY_OUT="$(python3 - "$YASH" <<'PYEOF'
import os, pty, select, sys, time

yash = sys.argv[1]
pid, fd = pty.fork()
if pid == 0:
    os.environ["PATH"] = os.environ.get("PATH", "/usr/bin:/bin")
    os.execv(yash, [yash])

out = []

def drain(seconds):
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                data = os.read(fd, 4096)
            except OSError:
                return
            if not data:
                return
            out.append(data.decode(errors="replace"))

def send(s, wait=0.6):
    os.write(fd, s.encode())
    drain(wait)

drain(0.4)
send("sleep 30\n", 0.5)
send("\x1a", 0.6)          # Ctrl-z stops the foreground job
send("jobs\n", 0.5)
send("bg\n", 0.5)
send("jobs\n", 0.5)
send("fg\n", 0.5)
send("\x03", 0.6)          # Ctrl-c kills the foreground job
send("jobs\n", 0.5)
send("echo shell_alive\n", 0.5)
send("\x04", 0.5)          # Ctrl-d exits
os.close(fd)
os.waitpid(pid, 0)
sys.stdout.write("".join(out).replace("\r", ""))
PYEOF
)"
    check_match "Ctrl-z stops the job" '\[1\] \+ Stopped[[:space:]]+sleep 30' "$PTY_OUT"
    check_match "bg resumes and prints jobs format with &" \
          '\[1\] \+ Running[[:space:]]+sleep 30 &' "$PTY_OUT"
    check_match "fg echoes the command name" '^sleep 30$' "$PTY_OUT"
    check_match "shell survives Ctrl-c" 'shell_alive' "$PTY_OUT"
    ALIVE="$(pgrep -f 'sleep 30' | wc -l)"
    check "Ctrl-c killed the foreground job" "0" "$ALIVE"
    pkill -f 'sleep 30' 2>/dev/null

    # Rubric scenario: many long-running jobs shuffled between fg, bg and
    # stopped, with new jobs created in the middle and some killed by Ctrl-c.
    PTY2="$(python3 - "$YASH" <<'PYEOF'
import os, pty, select, sys, time

yash = sys.argv[1]
pid, fd = pty.fork()
if pid == 0:
    os.execv(yash, [yash])

out = []

def drain(seconds):
    end = time.time() + seconds
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try:
                data = os.read(fd, 4096)
            except OSError:
                return
            if not data:
                return
            out.append(data.decode(errors="replace"))

def send(s, wait=0.5):
    os.write(fd, s.encode())
    drain(wait)

drain(0.4)
send("sleep 111 &\n")
send("sleep 222 &\n")
send("sleep 333 &\n")
send("jobs\n")
send("fg\n")               # pulls sleep 333 forward
send("\x1a")               # stop it
send("echo MARK_A\n")
send("jobs\n")
send("sleep 444 &\n")      # new job while others are parked
send("jobs\n")
send("fg\n")               # pulls sleep 444 forward
send("\x03")               # kill it
send("echo MARK_B\n")
send("jobs\n")
send("bg\n")               # resume the stopped sleep 333
send("echo MARK_C\n")
send("jobs\n")
send("\x04", 0.5)
os.close(fd)
os.waitpid(pid, 0)
sys.stdout.write("".join(out).replace("\r", ""))
PYEOF
)"
    AFTER_A="$(printf '%s' "$PTY2" | sed -n '/MARK_A/,/MARK_B/p')"
    AFTER_B="$(printf '%s' "$PTY2" | sed -n '/MARK_B/,/MARK_C/p')"
    AFTER_C="$(printf '%s' "$PTY2" | sed -n '/MARK_C/,$p')"

    check_match "three background jobs are listed" \
          '\[3\] \+ Running[[:space:]]+sleep 333 &' "$PTY2"
    check_match "fg then Ctrl-z leaves the job stopped" \
          '\[3\] \+ Stopped[[:space:]]+sleep 333' "$AFTER_A"
    check_match "new job created mid-session gets jid 4" \
          '\[4\] \+ Running[[:space:]]+sleep 444 &' "$PTY2"
    check_nomatch "job killed by Ctrl-c left the table" 'sleep 444' "$AFTER_B"
    check_match "surviving jobs still listed after a kill" \
          '\[1\] - Running[[:space:]]+sleep 111 &' "$AFTER_B"
    check_match "bg resumes the stopped job" \
          '\[3\] \+ Running[[:space:]]+sleep 333 &' "$AFTER_C"
    STRAY="$(pgrep -f 'sleep (111|222|333|444)' | wc -l)"
    check "no jobs survive the shell" "0" "$STRAY"
    pkill -f 'sleep 111' 2>/dev/null
    pkill -f 'sleep 222' 2>/dev/null
    pkill -f 'sleep 333' 2>/dev/null
    pkill -f 'sleep 444' 2>/dev/null
else
    printf '  skip python3 not found, interactive cases not run\n'
fi

# --- rubric example cases --------------------------------------------------

section "rubric examples"
check_match "basic command: date" '[0-9]{4}' "$(run_yash 'date')"
check_match "command with arguments: ls -a -l" '^total|\.\.' "$(run_yash 'ls -a -l')"
rm -f foo.txt
run_yash 'ls > foo.txt' >/dev/null
check_match "output: ls > foo.txt creates the file" '.' "$(cat foo.txt 2>/dev/null)"
check_match "input: wc < foo.txt" '[0-9]+' "$(run_yash 'wc < foo.txt')"
rm -f bar.txt
check "error: cat fake_file 2> bar.txt writes no stdout" "" \
      "$(run_yash 'cat fake_file 2> bar.txt')"
check_match "error file was created with content" '.' "$(cat bar.txt 2>/dev/null)"
check "invalid: cat < fake-file.bar" "" "$(run_yash 'cat < fake-file.bar')"
rm -f your_file
run_yash 'cat < in1.txt > your_file' >/dev/null
check "multiple redirections: cat < my-file > your_file" "FROM_IN1" \
      "$(cat your_file 2>/dev/null)"
check_match "basic pipe: ls | wc" '[0-9]+[[:space:]]+[0-9]+[[:space:]]+[0-9]+' \
      "$(run_yash 'ls | wc')"
check_match "many arguments: ls -a -l -t -r . | wc -c -L" '[0-9]+' \
      "$(run_yash 'ls -a -l -t -r . | wc -c -L')"
rm -f err1.txt err2.txt pout.txt
run_yash 'cat < in1.txt 2> err1.txt | cat > pout.txt 2> err2.txt' >/dev/null
check "pipe with redirections on both ends" "FROM_IN1" "$(cat pout.txt 2>/dev/null)"
check "stderr files created on both ends" "0 0" \
      "$(wc -c < err1.txt 2>/dev/null | tr -d ' ') $(wc -c < err2.txt 2>/dev/null | tr -d ' ')"
rm -f lout.txt rout.txt
run_yash 'cat < in1.txt > lout.txt | cat > rout.txt 2> err2.txt' >/dev/null
check "left > file starves the pipe" "FROM_IN1 " \
      "$(cat lout.txt 2>/dev/null) $(cat rout.txt 2>/dev/null)"

# --- summary ---------------------------------------------------------------

printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
