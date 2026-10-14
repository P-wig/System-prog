#!/usr/bin/env python3
"""Drives yash through a pty against a live yashd."""
import os, pty, select, signal, subprocess, sys, time

PASS = FAIL = 0


def check(desc, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  ok   %s" % desc)
    else:
        FAIL += 1
        print("  FAIL %s %s" % (desc, detail))


class Term:
    """A yash client attached to a pty we can type into."""

    def __init__(self, *args):
        self.dead = False
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.execv("./yashd/yash", ["yash"] + list(args))
        self.out = ""
        self.drain(0.6)

    def drain(self, seconds=0.5):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], 0.1)
            if not r:
                continue
            try:
                data = os.read(self.fd, 4096)
            except OSError:
                return
            if not data:
                return
            self.out += data.decode(errors="replace").replace("\r", "")

    def send(self, text, wait=0.5):
        before = len(self.out)
        os.write(self.fd, text.encode())
        self.drain(wait)
        return self.out[before:]

    def run(self, cmd, wait=0.5):
        """Sends a command, stripping the pty's local echo and the prompt."""
        out = self.send(cmd + "\n", wait)
        if out.startswith(cmd + "\n"):
            out = out[len(cmd) + 1:]
        if out.endswith("\n# "):
            out = out[:-3]
        return out

    def alive(self):
        if self.dead:
            return False
        try:
            pid, _ = os.waitpid(self.pid, os.WNOHANG)
        except ChildProcessError:
            self.dead = True
            return False
        if pid:
            self.dead = True
            return False
        return True

    def exited(self, timeout=3.0):
        """Exiting and being reapable race, so poll rather than check once."""
        end = time.time() + timeout
        while time.time() < end:
            if not self.alive():
                return True
            time.sleep(0.1)
        return False

    def kill(self):
        if not self.dead:
            try:
                os.kill(self.pid, signal.SIGKILL)
                os.waitpid(self.pid, 0)
            except OSError:
                pass
            self.dead = True
        try:
            os.close(self.fd)
        except OSError:
            pass


for stale in ("/tmp/yashd.log", "/tmp/yashd.pid"):
    if os.path.exists(stale):
        os.remove(stale)
subprocess.run(["pkill", "-x", "yashd"])
time.sleep(0.2)
print("starting yashd")
subprocess.run(["./yashd/yashd"], check=True)
time.sleep(0.4)

print("\nconnection and prompt")
t = Term("127.0.0.1")
check("prompt comes from the server", t.out.endswith("# "), repr(t.out))

print("\nrunning commands")
check("echo", t.run("echo hello") == "hello\n")
check("output then a fresh prompt", t.out.endswith("# "), repr(t.out[-20:]))
check("PATH search", t.run("which ls").strip() == "/usr/bin/ls")
check("stderr reaches the terminal", "No such file" in t.run("ls /nope_xyz"))
check("unknown command is silent", t.run("nosuchprogram_xyz") == "")
check("malformed line is silent", t.run("| ls") == "")
check("session survives a bad line", t.run("echo alive") == "alive\n")

t.run("echo piped > /tmp/yc_out.txt")
check("redirection", open("/tmp/yc_out.txt").read() == "piped\n")
check("pipe", t.run("cat /tmp/yc_out.txt | wc -l").strip() == "1")

print("\ntyped input becomes the command's stdin")
t.send("cat > /tmp/yc_stdin.txt\n", 0.4)
t.send("first line\n", 0.3)
t.send("second line\n", 0.3)
t.send("\x04", 0.6)                       # Ctrl-d: EOF for cat, not for us
check("plain text reached the command",
      open("/tmp/yc_stdin.txt").read() == "first line\nsecond line\n",
      repr(open("/tmp/yc_stdin.txt").read()))
check("client survives Ctrl-d mid-command", t.alive())
check("prompt returned after Ctrl-d", t.out.endswith("# "), repr(t.out[-20:]))

print("\nCtrl-c")
t.send("sleep 60\n", 0.4)
t.send("\x03", 0.8)                       # stops the command on the server
check("Ctrl-c returns a prompt", t.out.endswith("# "), repr(t.out[-20:]))
check("client survives Ctrl-c", t.alive())
check("the job is gone", "sleep 60" not in t.run("jobs"))
check("server-side process is gone",
      subprocess.run(["pgrep", "-f", "sleep 60"], capture_output=True).stdout.strip() == b"")

print("\nCtrl-z, bg, fg")
t.send("sleep 61\n", 0.4)
t.send("\x1a", 0.8)                       # suspends the command on the server
check("client survives Ctrl-z", t.alive())
out = t.run("jobs")
check("job shows as Stopped", "Stopped\tsleep 61" in out, repr(out))
out = t.run("bg")
check("bg resumes it with &", "Running\tsleep 61 &" in out, repr(out))
out = t.send("fg\n", 0.4)
check("fg echoes the command", "sleep 61" in out.replace("fg\n", "", 1), repr(out))
t.send("\x03", 0.6)
check("Ctrl-c killed the resumed job", "sleep 61" not in t.run("jobs"))

print("\nbackground jobs")
t.send("sleep 0.3 &\n", 0.2)
time.sleep(0.6)
out = t.run("echo tick")
check("Done line is delivered", "Done\tsleep 0.3 &" in out, repr(out))

print("\nCtrl-c at the prompt")
t.send("\x03", 0.6)
check("prompt reappears", t.out.endswith("# "), repr(t.out[-20:]))
check("client still alive", t.alive())
check("still usable", t.run("echo after-ctrl-c") == "after-ctrl-c\n")

print("\nterminating")
t.send("quit\n", 0.6)
check("quit exits the client", t.exited())
t.kill()

t2 = Term("127.0.0.1")
t2.send("\x04", 0.6)
check("Ctrl-d at the prompt exits the client", t2.exited())
t2.kill()

t3 = Term("127.0.0.1")
t3.run("sleep 314 &")
t3.send("quit\n", 0.6)
t3.exited()
time.sleep(0.5)
check("the client's jobs die with it",
      subprocess.run(["pgrep", "-f", "sleep 314"], capture_output=True).stdout.strip() == b"")
t3.kill()

print("\nerror handling")
bad = subprocess.run(["./yashd/yash", "999.999.999.999"], capture_output=True)
check("rejects a non-address", b"not an IPv4 address" in bad.stderr)
check("non-zero exit on bad address", bad.returncode != 0)
noargs = subprocess.run(["./yashd/yash"], capture_output=True)
check("usage message with no arguments", b"usage" in noargs.stderr)

print("\nlog")
log = open("/tmp/yashd.log").read()
check("commands were logged", "echo hello" in log and "yashd[127.0.0.1:" in log)
check("control messages were not logged", "CTL" not in log)

subprocess.run(["pkill", "-x", "yashd"])
subprocess.run(["pkill", "-f", "sleep 6"])
time.sleep(0.4)
refused = subprocess.run(["./yashd/yash", "127.0.0.1"], capture_output=True,
                         stdin=subprocess.DEVNULL, timeout=15)
check("reports a refused connection", refused.returncode != 0 and
      b"onnection refused" in refused.stderr, repr(refused.stderr))

print("\n%d passed, %d failed" % (PASS, FAIL))
sys.exit(1 if FAIL else 0)
