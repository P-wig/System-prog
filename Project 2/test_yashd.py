#!/usr/bin/env python3
"""Drives bin/yashd over the wire protocol and checks the responses."""
import os, socket, subprocess, sys, time

HOST, PORT = "127.0.0.1", 3820
PASS = FAIL = 0


def check(desc, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  ok   %s" % desc)
    else:
        FAIL += 1
        print("  FAIL %s %s" % (desc, detail))


class Client:
    def __init__(self):
        self.s = socket.create_connection((HOST, PORT), timeout=5)
        self.buf = ""
        self.until_prompt()

    def until_prompt(self, timeout=5):
        self.s.settimeout(timeout)
        end = time.time() + timeout
        while not self.buf.endswith("\n# ") and time.time() < end:
            try:
                data = self.s.recv(4096)
            except socket.timeout:
                break
            if not data:
                break
            self.buf += data.decode(errors="replace")
        out, self.buf = self.buf, ""
        return out[:-3] if out.endswith("\n# ") else out

    def send(self, msg):
        self.s.sendall(msg.encode())

    def cmd(self, line):
        self.send("CMD %s\n" % line)
        return self.until_prompt()

    def close(self):
        self.s.close()


print("starting yashd")
for stale in ("/tmp/yashd.log", "/tmp/yashd.pid"):
    if os.path.exists(stale):
        os.remove(stale)
subprocess.run(["./bin/yashd"], check=True)
time.sleep(0.4)

c = Client()

print("\nbasics")
check("simple command", c.cmd("echo hello") == "hello\n", repr(c.buf))
check("args passed through", c.cmd("echo a b c") == "a b c\n")
check("PATH is searched", c.cmd("which ls").strip() == "/usr/bin/ls")
check("stderr reaches the client", "No such file" in c.cmd("ls /nope_xyz"))
check("unknown command is silent", c.cmd("nosuchprogram_xyz") == "")
check("malformed line is silent", c.cmd("| ls") == "")
check("session survives a bad line", c.cmd("echo alive") == "alive\n")

print("\nredirection and pipes")
c.cmd("echo redirected > /tmp/yt_out.txt")
check("> creates the file", open("/tmp/yt_out.txt").read() == "redirected\n")
check("< reads the file", c.cmd("cat < /tmp/yt_out.txt") == "redirected\n")
check("pipe works", c.cmd("cat /tmp/yt_out.txt | wc -l").strip() == "1")
c.cmd("echo relative > yt_rel.txt")
check("relative path lands in /tmp", os.path.exists("/tmp/yt_rel.txt"))

print("\nplain text as stdin")
c.send("CMD cat > /tmp/yt_stdin.txt\n")
time.sleep(0.3)
c.send("typed line one\n")
c.send("typed line two\n")
time.sleep(0.2)
c.send("CTL d\n")
c.until_prompt()
check("plain text reached the command",
      open("/tmp/yt_stdin.txt").read() == "typed line one\ntyped line two\n",
      repr(open("/tmp/yt_stdin.txt").read()))

print("\njob control")
check("& prints nothing", c.cmd("sleep 5 &") == "")
check("jobs lists it", "[1] + Running\tsleep 5 &" in c.cmd("jobs"))
c.cmd("sleep 6 &")
out = c.cmd("jobs")
check("older job marked -", "[1] - Running\tsleep 5 &" in out, repr(out))
check("newest job marked +", "[2] + Running\tsleep 6 &" in out, repr(out))

c.cmd("sleep 0.3 &")
time.sleep(0.6)
check("Done line appears", "Done\tsleep 0.3 &" in c.cmd("echo tick"))

print("\nCTL c / CTL z")
c.send("CMD sleep 60\n")
time.sleep(0.3)
c.send("CTL c\n")
got = c.until_prompt()
check("CTL c returns to the prompt", got == "", repr(got))
check("interrupted job left the table", "sleep 60" not in c.cmd("jobs"))

c.send("CMD sleep 61\n")
time.sleep(0.3)
c.send("CTL z\n")
c.until_prompt()
out = c.cmd("jobs")
check("CTL z stops the job", "Stopped\tsleep 61" in out, repr(out))
out = c.cmd("bg")
check("bg resumes with &", "Running\tsleep 61 &" in out, repr(out))
c.send("CMD fg\n")
time.sleep(0.3)
c.send("CTL c\n")
got = c.until_prompt()
check("fg echoes the command", got.startswith("sleep 61"), repr(got))

print("\nper-session isolation")
d = Client()
d.cmd("sleep 7 &")
check("second client sees only its own job",
      d.cmd("jobs").count("Running") == 1, repr(d.cmd("jobs")))
check("first client unaffected", "sleep 7" not in c.cmd("jobs"))

print("\nlogging")
log = open("/tmp/yashd.log").read()
check("log records a command", "yashd[127.0.0.1:" in log and "echo hello" in log)
check("log uses syslog-like format",
      any(l.split()[3].startswith("yashd[") for l in log.splitlines() if l))
ports = set(l.split("yashd[")[1].split("]")[0] for l in log.splitlines() if "yashd[" in l)
check("two clients log distinct ports", len(ports) >= 2, repr(ports))
check("malformed lines are not logged", "| ls" not in log)

print("\ndisconnect cleanup")
d.cmd("sleep 313 &")
d.close()
time.sleep(0.5)
left = subprocess.run(["pgrep", "-f", "sleep 313"], capture_output=True)
check("children die with their client", left.stdout.strip() == b"")

c.send("CTL d\n")
time.sleep(0.3)
check("daemon still alive after a client leaves",
      subprocess.run(["pgrep", "-x", "yashd"], capture_output=True).stdout.strip() != b"")

subprocess.run(["pkill", "-x", "yashd"])
subprocess.run(["pkill", "-f", "sleep 6"])
print("\n%d passed, %d failed" % (PASS, FAIL))
sys.exit(1 if FAIL else 0)
