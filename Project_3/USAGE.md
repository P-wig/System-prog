# Using Pintos with QEMU

Pintos boots a real x86 kernel, so it needs a PC emulator. This repo is
configured for **QEMU**, not Bochs: [vm/Make.vars](vm/Make.vars) sets
`SIMULATOR = --qemu`, and [utils/pintos](utils/pintos) defaults to QEMU even
though its own `--help` text still claims Bochs is the default.

Everything here assumes Linux (WSL Ubuntu or the course VM). None of it runs
from PowerShell or Git Bash.

## Setup, start to finish

The full sequence, verified on WSL2 Ubuntu 24.04. Each step is explained in
detail below.

```bash
# from PowerShell
wsl -d Ubuntu

# from here on, inside Ubuntu
apt update
apt install -y build-essential gcc-9 gcc-multilib gdb qemu-system-x86 binutils perl
update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 90

cd /mnt/c/Users/Isaac/Documents/System-prog/Project_3
chmod +x install.sh .check_reqs.sh \
  utils/pintos utils/runtest utils/backtrace \
  utils/pintos-gdb utils/pintos-mkdisk utils/pintos-set-cmdline

./install.sh              # prints env vars; reports symlink "failed" — expected

cat >> ~/.bashrc <<'EOF'

export PINTOS=/mnt/c/Users/Isaac/Documents/System-prog/Project_3
export PATH=$PATH:$PINTOS/utils
EOF

source ~/.bashrc
./install.sh              # "Installation successful."

cd $PINTOS/vm && make
```

Prepend `sudo` to the `apt` and `update-alternatives` lines if your WSL user is
not root.

## One-time setup

### 1. Install the toolchain

On Ubuntu 24.04 (including WSL2):

```bash
sudo apt update
sudo apt install -y build-essential gcc-9 gcc-multilib gdb qemu-system-x86 binutils perl
```

`gcc-multilib` is required because [Make.config](Make.config) compiles with
`-m32` and links with `-melf_i386` on x86-64 hosts.

> **Pin GCC to 9.** Ubuntu 24.04 defaults to GCC 13, but GCC 11+ is known to
> miscompile Pintos. Verify with `gcc -dumpversion`.
>
> ```bash
> sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 90
> sudo update-alternatives --remove gcc /usr/bin/gcc-9   # to undo
> ```

### 1b. Windows users: use WSL, not Git Bash

[.check_reqs.sh](.check_reqs.sh) accepts only `Darwin` or `Linux` from
`uname -s`. Git Bash reports `MINGW64_NT-...` and is rejected with
`Unsupported OS` — correctly, since it has no i386 ELF toolchain or QEMU.

Enter WSL first:

```powershell
wsl -d Ubuntu
```

Working out of `/mnt/c/...` is supported as long as symlink creation succeeds
there (`install.sh` needs it for `utils/qemu`). Builds are slower than a native
clone under `~`. The executable bit is often lost on that mount, so:

```bash
chmod +x install.sh .check_reqs.sh \
  utils/pintos utils/runtest utils/backtrace \
  utils/pintos-gdb utils/pintos-mkdisk utils/pintos-set-cmdline
```

If your WSL default user is `root`, the bashrc `install.sh` refers to is
`/root/.bashrc`.

`install.sh` creates `utils/qemu` as a symlink from inside WSL. Git for Windows
cannot read it (`open("utils/qemu"): Function not implemented`) and `git add`
will abort, so it is listed in [utils/.gitignore](utils/.gitignore).

### 2. Run the install script

```bash
cd /mnt/c/Users/Isaac/Documents/System-prog/Project_3
./install.sh
```

> **Keep the directory name free of spaces.** `install.sh` prints the env lines
> with `printf "export PINTOS=$SCRIPTPATH\n"` — unquoted. A path containing a
> space word-splits, printf reapplies its format, and you get a mangled value
> such as:
>
> ```
> export PINTOS=/mnt/c/Users/Isaac/Documents/System-prog
> 3
> ```
>
> Pasting that into `.bashrc` points `$PINTOS` at the wrong directory. This is
> why the project folder is `Project_3`, not `Project 3`.

It does two things and nothing else:

1. **Prints** the two lines you must add to `~/.bashrc` yourself — it
   deliberately does not edit the file, so reinstalling is safe:

   ```bash
   export PINTOS=/absolute/path/to/Project_3
   export PATH=$PATH:$PINTOS/utils
   ```

2. **Symlinks** `qemu-system-i386` to `utils/qemu`. This is required:
   `run_qemu` in [utils/pintos](utils/pintos) invokes the bare command name
   `qemu`, which no longer exists as a real binary on modern distros.

> **`Symlinking qemu-system-i386 -> qemu ... failed!` on the first pass is
> expected.** The script verifies the symlink with `which qemu`, which can only
> succeed once `$PINTOS/utils` is on `PATH` — which has not happened yet.
> Confirm the link really exists with `ls -l utils/qemu`; if it points at
> `/usr/bin/qemu-system-i386`, the second pass will report success.

### 3. Add the environment variables

`install.sh` only *prints* these; it never edits your bashrc. Append them
yourself, adjusting the path if your checkout lives elsewhere:

```bash
cat >> ~/.bashrc <<'EOF'

export PINTOS=/mnt/c/Users/Isaac/Documents/System-prog/Project_3
export PATH=$PATH:$PINTOS/utils
EOF
```

The quotes around `'EOF'` are required. Without them the shell expands `$PATH`
and `$PINTOS` as the file is written, baking in today's values instead of
leaving the variable names to be resolved at each shell startup.

### 4. Re-source and re-run

```bash
source ~/.bashrc
./install.sh
```

Expected final line:

```
Installation successful.
```

The second pass skips the symlink block entirely, because `which qemu` now
succeeds on the first check.

### 5. Verify

```bash
echo $PINTOS              # your Project_3 path
which pintos qemu         # both resolve into $PINTOS/utils
gcc -dumpversion          # 9
```

If `./install.sh` fails, it is [.check_reqs.sh](.check_reqs.sh) rejecting you.
That script validates binutils (`ld`, `objcopy`), GCC ≥ 3.3, Perl ≥ 5.6.1, and
QEMU, and prints the exact `apt` command for whatever is missing.

## Building

Build from a project directory, never from the repo root:

```bash
cd $PINTOS/vm
make
```

Output lands in `vm/build/`, which is gitignored. `make clean` removes it.

> **Expect `vm/` to build an unchanged kernel at first.** Line 65 of
> [Makefile.build](Makefile.build) is `#vm_SRC = vm/file.c`, commented out, and
> `vm/` currently contains no `.c` files. As you add source files you must
> uncomment that line and list them, or they will never be compiled, and you
> will spend an afternoon wondering why your changes have no effect.

## Running tests

All commands run from a project directory (`vm`, `userprog`, `filesys`).

### Whole suite

```bash
make check
```

For Project 3 this runs `tests/userprog`, `tests/vm`, and `tests/filesys/base`,
as listed in `TEST_SUBDIRS` in [vm/Make.vars](vm/Make.vars). Everything fails
until you implement the VM subsystem — that is the correct starting state.

### A single test

```bash
cd $PINTOS/vm
make
ls build/tests/vm                 # see what exists
runtest pt-grow-stack
runtest pt-grow-stack pt-big-stk-obj   # several at once
```

[utils/runtest](utils/runtest) accepts either a bare test name or a path like
`build/tests/vm/pt-grow-stack`. It locates the matching `.ck` file, runs the
test, and prints PASS/FAIL. It must be run from a project directory or its
`build/`.

### Driving the emulator by hand

```bash
cd $PINTOS/vm/build
pintos --qemu -- run alarm-multiple
```

Useful flags from [utils/pintos](utils/pintos):

| Flag | Effect |
|---|---|
| `-v` | No VGA window; serial output to your terminal |
| `-T 60` | Kill the run after 60 seconds |
| `-k` | Kill on kernel panic instead of hanging |
| `--swap-size=4` | 4 MB swap disk — **required for VM tests** |
| `-p FILE -a NAME` | Copy a host file into the Pintos filesystem |
| `-f` | Format the filesystem before running |

`make check` composes exactly these into `TESTCMD` in
[tests/Make.tests](tests/Make.tests#L54-L70); read that block when you need to
reproduce a failing test invocation by hand.

### Grade preview

```bash
make grade
cat build/grade        # if already up to date
```

This is scored against `tests/vm/Grading`. It is **not** authoritative —
synchronization bugs routinely pass locally and fail during real grading,
because QEMU's timing is non-deterministic.

## Debugging

GDB is not optional on this project. Use two terminals.

```bash
# terminal 1
cd $PINTOS/vm/build
pintos --qemu --gdb -- run pt-grow-stack

# terminal 2
cd $PINTOS/vm/build
pintos-gdb kernel.o
(gdb) target remote localhost:1234
```

`--gdb` passes `-s -S` to QEMU: open a gdbstub on port 1234 and halt the CPU
before the first instruction, so you get to attach before anything boots.

[utils/pintos-gdb](utils/pintos-gdb) is a wrapper that loads
[utils/gdb-macros](utils/gdb-macros), which gives you Pintos-aware commands:

| Command | Purpose |
|---|---|
| `debugpintos` | Standard setup; run this first after connecting |
| `btthread`, `btthreadlist` | Backtrace one thread, or every thread |
| `dumplist` | Walk a Pintos linked list |
| `hook-stop` | Auto-prints state at each stop |

To debug a single test under GDB, `runtest --gdb test-name` forwards the flag.

### Interpreting a kernel panic

A panic prints a backtrace of raw addresses. Translate them:

```bash
backtrace build/kernel.o 0xc0106eff 0xc01102fb
```

[utils/backtrace](utils/backtrace) maps those to file and line numbers. For a
user program fault, pass the user binary instead of `kernel.o`.

## A note on determinism

QEMU does not reproduce timing between runs, so a race in your page-eviction or
frame-allocation code may pass ten times and then fail during grading. If you
hit an intermittent failure you cannot pin down, Bochs is deterministic and
supports `--jitter` for reproducible timing perturbation:

```bash
sudo apt install bochs bochs-sdl bochsbios vgabios
make check SIMULATOR=--bochs
```

It is much slower, so use it to confirm a suspected race, not for routine
iteration. No edit to [vm/Make.vars](vm/Make.vars) is needed — the `SIMULATOR`
override on the command line is enough.

## Troubleshooting

| Symptom | Cause |
|---|---|
| `pintos: command not found` | `$PINTOS/utils` not on `PATH`; check `grep PINTOS ~/.bashrc`, then re-source |
| `Symlinking ... failed!` | `$PINTOS/utils` not yet on `PATH`. Harmless on the first pass; verify with `ls -l utils/qemu` |
| `Unsupported OS` | You are in Git Bash or PowerShell, not WSL |
| `No build directory found` | You ran `runtest` outside a project dir; `make` first |
| Code changes have no effect | Source file not listed in `vm_SRC` in [Makefile.build](Makefile.build) |
| Every VM test fails instantly | Missing `--swap-size=4` when invoking `pintos` by hand |
| Run hangs forever | Add `-T 60 -k` to time out and kill on panic |
| `Function not implemented` on `git add` | Windows Git hit the `utils/qemu` symlink; confirm it is ignored |

## Files git ignores

Covered by the root [.gitignore](../.gitignore) and the per-directory Pintos
ones: `build/`, `*.o`, `*.d`, `*.dsk`, `bochsrc.txt`, `bochsout.txt`, and the
compiled binaries in `examples/` and `utils/`.

The `utils/qemu` symlink is listed in [utils/.gitignore](utils/.gitignore).
This is not cosmetic: Git for Windows cannot read a WSL-created symlink and
aborts the whole staging operation with
`open("utils/qemu"): Function not implemented`.

The root [.gitattributes](../.gitattributes) forces `eol=lf` on all sources and
on the extensionless Perl utilities in `utils/`. Without it, a clone on a
machine with `core.autocrlf=true` rewrites them with CRLF and every script dies
with `/bin/bash^M: bad interpreter`.
