# Project 3 TODOs

This Pintos tree ships with `threads/`, `devices/`, `lib/`, and `filesys/`
already working — that's supporting code, not part of this assignment.
Everything you need to implement lives in two modules, built together from
the [vm/](vm) project directory (its [Make.vars](vm/Make.vars) pulls in
`tests/userprog`, `tests/vm`, and `tests/filesys/base`).

| Module | Directory | State |
|---|---|---|
| 1. User Programs | [userprog/](userprog) | Skeleton present, core logic stubbed out |
| 2. Virtual Memory | [vm/](vm) | Empty — no `.c` files exist yet |

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

### 3. Argument passing — [process.c](userprog/process.c)
`load()` passes the raw command line straight to `filesys_open()` — it never
splits off arguments:

```c
file = filesys_open(file_name);
```

and `setup_stack()` only zero-maps a page and points `esp` at `PHYS_BASE`;
nothing is pushed onto it:

```c
static bool
setup_stack(void **esp)
{
    ...
    success = install_page(((uint8_t *)PHYS_BASE) - PGSIZE, kpage, true);
    if (success) {
        *esp = PHYS_BASE;
    } else { ... }
    // hex_dump( *(int*)esp, *esp, 128, true ); // NOTE: uncomment this to check arg passing
    ...
}
```

TODO:
- [ ] Tokenize the command line (program name + arguments, e.g. with
      `strtok_r`) before calling `filesys_open()` so only the executable name
      is opened.
- [ ] Push the argument strings, a null sentinel, `argv[]` pointers, `argv`,
      `argc`, and a fake return address onto the stack in `setup_stack()`,
      following the x86 calling convention Pintos expects.
- [ ] Uncomment the `hex_dump` line (or similar) to verify the stack layout
      against the Pintos docs' example dump while developing.

### 4. Design document — [userprog_design_doc.txt](userprog/userprog_design_doc.txt)
- [ ] Fill in the blank "EE 461S Project 2: User Programs" template once the
      above is implemented.

---

## Module 2 — Virtual Memory (`vm/`)

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

```bash
cd $PINTOS/vm
make
make check          # runs tests/userprog, tests/vm, tests/filesys/base
```

All tests fail until the relevant TODOs above are addressed — see
[USAGE.md](USAGE.md) for the full build/test/debug workflow.
