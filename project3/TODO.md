# Project 3 TODOs

This Pintos tree ships with `threads/`, `devices/`, `lib/`, and `filesys/`
already working — that's supporting code, not part of this assignment.

**Scope update:** it's been clarified that this assignment only covers
**User Programs** (`userprog/`). The Virtual Memory module (`vm/`) is **not**
required and is parked, unimplemented, at the bottom of this file for
reference only — do not build it as part of this assignment.

| Module | Directory | State |
|---|---|---|
| User Programs | [userprog/](userprog) | ✅ All in-scope requirements implemented, `make check` passes 75/75 |
| ~~Virtual Memory~~ | ~~[vm/](vm)~~ | **Out of scope — do not implement.** Left unimplemented below for reference only. |

---

## Module 1 — User Programs (`userprog/`)

### 1. System call dispatch — [syscall.c](userprog/syscall.c) ✅ Done
`syscall_handler()` was a placeholder that just printed `"system call!\n"`
and exited. It's now implemented:

- [x] Reads the syscall number and arguments off `f->esp`, validating every
      user-provided pointer/string/buffer first (`check_user_range()` /
      `check_user_string()`); invalid pointers terminate the process via
      `sys_exit(-1)` instead of crashing the kernel.
- [x] Dispatches all of `SYS_HALT`, `SYS_EXIT`, `SYS_EXEC`, `SYS_WAIT`,
      `SYS_CREATE`, `SYS_REMOVE`, `SYS_OPEN`, `SYS_FILESIZE`, `SYS_READ`,
      `SYS_WRITE`, `SYS_SEEK`, `SYS_TELL`, `SYS_CLOSE`. `SYS_MMAP`/
      `SYS_MUNMAP` currently just reject with `sys_exit(-1)` — real support
      is deferred to the VM module below.
- [x] Added per-process file descriptor tracking: `struct thread` now has
      an `fd_list`/`fd_next` (see [thread.h](threads/thread.h)), populated
      by `sys_open()` and walked by `lookup_fd()`. `process_exit()` calls
      the new `syscall_close_all_files()` so a dying process doesn't leak
      open files.
- [x] Added a single global `filesys_lock` in syscall.c guarding every
      `filesys_*()`/`file_*()` call, since the filesystem isn't reentrant.

Known limitations, to be resolved by later TODO items in this file:
- `sys_exit()`'s status isn't yet delivered to a waiting parent — that
  lands with TODO #2 below (`process_wait()`).
- `sys_exec()` doesn't yet block until the child finishes loading or learn
  whether loading failed — same TODO #2.
- End-to-end testing (`make check`) still fails: `threads/init.c`'s
  `run_task()` does `process_wait(process_execute(task))`, and since
  `process_wait()` is still a stub returning `-1` immediately, the kernel
  powers off before the test process ever gets a CPU tick (confirmed via
  `Thread: ... 0 user ticks` in test output). This is expected until TODO
  #2 is implemented, not a syscall.c bug — verified by a clean `make`
  rebuild of `userprog/` with zero errors/warnings.

### 2. Process exit status — [process.c](userprog/process.c) ✅ Done
`process_wait()` was a stub that returned `-1` immediately without waiting.
It's now implemented:

- [x] Added a reference-counted `struct process_status` (private to
      process.c), created in `process_execute()` and shared between parent
      and child: the parent holds it in a new `children` list on
      `struct thread`, the child holds it via a new `pstatus` pointer (see
      [thread.h](threads/thread.h)). Either side may exit first, so each
      side drops its own reference via `release_child_status()`, and
      whichever drops the last one frees it.
- [x] `process_wait()` looks the child up by tid in the caller's
      `children` list (returning `-1` immediately if not found — covers
      invalid tid, non-child tid, and "already waited" since a successful
      wait removes the entry), then blocks on a semaphore
      (`ps->dead`) until the child's `process_exit()` posts it.
- [x] `process_exit()` now posts that semaphore and copies the process's
      `exit_status` (a new field on `struct thread`, defaulting to `-1` —
      the "killed by the kernel" case) into the shared record. It also
      walks its own `children` list releasing any it never waited for, so
      parent-exits-first doesn't leak.
- [x] [syscall.c](userprog/syscall.c)'s `sys_exit()` now sets
      `thread_current()->exit_status` before calling `thread_exit()`, wiring
      the real exit code through to a waiting parent. The
      `"%s: exit(%d)\n"` message was already in place from TODO #1.

Verified: clean `make` rebuild of `userprog/` with zero errors/warnings.
Before this change, every test showed `0 user ticks` in its log (the kernel
powered off before the test process ever ran, because `process_wait()`
returned instantly). After this change, e.g. `wait-simple`'s log shows
`21 user ticks` and the child process actually executing — confirming
`process_wait()` now genuinely blocks on the child instead of returning
immediately.

Still expected to fail `make check`: every userprog test's `main()` reads
`argv[0]` (see [tests/main.c](tests/main.c)), and since argument passing
(TODO #3 below) isn't implemented yet, the initial stack has no `argc`/
`argv` on it, so tests now fault trying to read it (`Page fault ... rights
violation error reading page`). That's TODO #3's job, not a regression
here.

### 3. Argument passing — [process.c](userprog/process.c) ✅ Done
`load()` used to pass the raw command line straight to `filesys_open()`, and
`setup_stack()` only zero-mapped a page and pointed `esp` at `PHYS_BASE`
without pushing anything onto it.

- [x] `load()` now tokenizes the command line with `strtok_r()` into an
      `argv[]`/`argc` (capped at `MAX_ARGS` = 32), opens only `argv[0]` via
      `filesys_open()`, and passes `argv`/`argc` through to `setup_stack()`.
- [x] `setup_stack()` builds the full initial stack: argument strings
      (highest address = last arg, matching the Pintos docs' example),
      4-byte alignment padding, a null `argv[argc]` sentinel, the `argv[]`
      pointer array, `argv`, `argc`, and a fake return address — each push
      bounds-checked against the single stack page (fails cleanly instead
      of overflowing if the command line is too long).
- [x] `process_execute()` now also extracts just the program name (before
      the first space) to use as the new thread's name, since the whole
      command line was getting truncated into `thread_name()` and breaking
      the `"%s: exit(%d)\n"` message tests check for.
- [x] Along the way, found and fixed a gap from TODO #2: the exit message
      was only printed by `sys_exit()`, so processes killed by the kernel
      (bad pointer/bad jump tests) never printed `"name: exit(-1)"`. Moved
      that `printf()` into `process_exit()` itself (gated on
      `pagedir != NULL`) so it fires for both voluntary `exit()` calls and
      kernel-forced kills.

Verified: clean `make` rebuild with zero errors/warnings. `make check` went
from "every test fails instantly" (page fault reading `argv[0]` before this
TODO) to 95 passing, with 5 then-remaining failures — `exec-missing`,
`rox-simple`, `rox-child`, `rox-multichild`, `syn-read` — tracked and
resolved by TODO #5 below (they weren't argument-passing bugs).

### 4. Design document — [userprog_design_doc.txt](userprog/userprog_design_doc.txt)
- [ ] Fill in the blank "EE 461S Project 2: User Programs" template. This is
      the only remaining item for this module.

### 5. Exec/load synchronization and write-protected executables — [process.c](userprog/process.c), [syscall.c](userprog/syscall.c) ✅ Done
Found while re-reading [userprog/README.md](userprog/README.md) after the
scope was narrowed to userprog-only: two requirements from the spec weren't
implemented yet, and they were the root cause of the 5 remaining test
failures noted above (not VM-related, so still squarely in scope):

- **"the parent process cannot return from `exec` until it knows whether the
  child process successfully loaded its executable"** (`exec-missing`
  failure — `exec()` returned a positive pid instead of `-1` for a
  nonexistent program). Fixed by adding `load_success`/`loaded` (a
  semaphore) to `struct process_status`: `start_process()` now posts
  `loaded` right after `load()` returns (success or failure), and
  `process_execute()` blocks on it before returning, reporting `TID_ERROR`
  if the load failed instead of handing back a doomed pid.
- **"Denying Writes to Executables"** (Exercise 2.5 — `rox-simple`/
  `rox-child`/`rox-multichild` failures, since a running executable's file
  wasn't write-protected). Fixed: `load()` now calls `file_deny_write()` on
  success and keeps the executable open for the process's whole lifetime in
  a new `exec_file` field on `struct thread` (owned by process.c, same
  pattern as `pagedir`), instead of closing it immediately; `process_exit()`
  closes it (which also calls `file_allow_write()` internally — see
  [filesys/file.c](filesys/file.c)'s `file_close()`).
- **Un-synchronized filesystem access from `process_execute()`/`load()`**
  (root cause of the `syn-read` failure, and an explicitly called-out
  requirement: *"Don't forget that `process_execute()` also accesses
  files"*). `load()` previously called `filesys_open()`/`file_read()`/etc.
  with no locking at all, while every syscall handler already serialized on
  `syscall.c`'s private `filesys_lock`. Exposed two thin wrapper functions,
  `syscall_filesys_lock_acquire()`/`syscall_filesys_lock_release()`, from
  [syscall.h](userprog/syscall.h) (keeping the actual lock object private to
  syscall.c, same pattern as `syscall_close_all_files()`), and wrapped all of
  `load()`'s file access in it, plus `process_exit()`'s close of
  `exec_file`.

Verified: clean `make` rebuild, zero errors/warnings. `make check` now
reports **"All 75 tests passed."** — every test in
`tests/userprog/Rubric.functionality`, `tests/userprog/Rubric.robustness`,
and `tests/filesys/base/Rubric` (the only three rubrics with nonzero weight
in [tests/userprog/Grading](tests/userprog/Grading); `tests/userprog/no-vm`
is explicitly 0%, confirming it's not needed).

---

## Module 2 — Virtual Memory (`vm/`) — **OUT OF SCOPE, not required**

> **This entire module is not part of this assignment** (clarified
> 2026-11-13). It's left below, unimplemented, purely as reference in case
> scope changes again later — do not act on anything in this section unless
> told otherwise.

The [vm/](vm) directory currently contains only `Makefile`, `Make.vars`,
`README.md`, and the blank `vm_design_doc.txt` — **no implementation
exists yet**. You are expected to design and create it from scratch
(typically a supplemental page table, a frame table, and swap support — e.g.
`vm/page.c`, `vm/frame.c`, `vm/swap.c` plus headers, though exact naming is
up to you).

### 1. Register your new sources — [Makefile.build](Makefile.build)
```make
#vm_SRC = vm/file.c			# Some file.
```
TODO:
- [ ] Uncomment `vm_SRC` and list every `.c` file you add under `vm/`, or
      they will silently never be compiled (see the warning in
      [USAGE.md](USAGE.md)).

### 2. Page-fault handler — [exception.c](userprog/exception.c)
`page_fault()` currently just prints a diagnostic and kills the process:

```c
/* To implement virtual memory, delete the rest of the function
 * body, and replace it with code that brings in the page to
 * which fault_addr refers. */
printf("Page fault at %p: %s error %s page in %s context.\n", ...);
kill(f);
```

TODO:
- [ ] Replace the body: look up `fault_addr` in the faulting process's
      supplemental page table, and either load the page on demand (from the
      executable, swap, or a zero page) or terminate the process if the
      access is genuinely invalid.
- [ ] Distinguish stack growth (an access just below the current stack
      pointer) from an invalid access.

### 3. Supplemental page table
TODO:
- [ ] Design a per-process data structure (keyed on user virtual address)
      recording how to (re)populate each page: from a file (and at what
      offset), from swap, or zeroed.
- [ ] Hook it into process/thread creation and teardown
      ([process.c](userprog/process.c), `process_exit()`).

### 4. Frame table and eviction
TODO:
- [ ] Track which physical frames are in use and by whom (replace/augment
      `threads/palloc.c` usage for user pages).
- [ ] Implement an eviction policy to free a frame when none are available,
      writing the evicted page to swap (or back to its file) as needed.
- [ ] Add synchronization so concurrent faults/evictions can't race.

### 5. Swap support
TODO:
- [ ] Create a swap table (block device abstraction already exists in
      [devices/block.c](devices/block.c)) to track free/used swap slots.
- [ ] Implement swap-in (on page fault) and swap-out (on eviction).
- [ ] Remember to pass `--swap-size=4` (or similar) when running tests by
      hand — see [USAGE.md](USAGE.md).

### 6. Lazy loading / memory-mapped files
TODO:
- [ ] Convert `load_segment()` in [process.c](userprog/process.c) to load
      pages lazily via the supplemental page table instead of eagerly reading
      the whole segment at `load()` time.
- [ ] Implement `SYS_MMAP`/`SYS_MUNMAP` in [syscall.c](userprog/syscall.c)
      (shared with Module 1), backed by the same supplemental page table
      infrastructure.

### 7. Design document — [vm_design_doc.txt](vm/vm_design_doc.txt)
- [ ] Fill in the blank "EE 461S Project 3: Virtual Memory" template once the
      above is implemented.

---

## Verifying progress

Build and test from **`userprog/`** (not `vm/` — that module isn't part of
this assignment, see above):

```bash
cd $PINTOS/userprog
make
make check          # runs tests/userprog and tests/filesys/base
```

As of this writing, `make check` reports **"All 75 tests passed."** — see
[USAGE.md](USAGE.md) for the full build/test/debug workflow.
