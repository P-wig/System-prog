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

### 1. System call dispatch — [syscall.c](userprog/syscall.c)
`syscall_handler()` is a placeholder:

```c
static void
syscall_handler(struct intr_frame *f UNUSED)
{
    /* Remove these when implementing syscalls */
    printf("system call!\n");

    thread_exit();
}
```

TODO:
- [ ] Read the syscall number and arguments off the interrupt frame's stack
      (`f->esp`), validating every user-provided pointer before dereferencing
      it (bad pointers must terminate the offending process, not crash the
      kernel).
- [ ] Dispatch on all syscall numbers defined in
      [syscall-nr.h](lib/syscall-nr.h):
      `SYS_HALT`, `SYS_EXIT`, `SYS_EXEC`, `SYS_WAIT`, `SYS_CREATE`,
      `SYS_REMOVE`, `SYS_OPEN`, `SYS_FILESIZE`, `SYS_READ`, `SYS_WRITE`,
      `SYS_SEEK`, `SYS_TELL`, `SYS_CLOSE` (the `SYS_MMAP`/`SYS_MUNMAP` pair is
      project 3/4 territory, shared with the VM module below).
- [ ] Add per-process file descriptor tracking (e.g. a list/array on
      `struct thread`) so `open`/`read`/`write`/`close` etc. can map fds to
      `struct file *`.
- [ ] Synchronize filesystem access if multiple processes can call syscalls
      concurrently (a single global filesystem lock is the usual approach).

### 2. Process exit status — [process.c](userprog/process.c)
`process_wait()` is a stub:

```c
/* This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait(tid_t child_tid UNUSED)
{
    return -1;
}
```

TODO:
- [ ] Track parent/child relationships and exit status so a parent can
      retrieve a child's real exit code exactly once.
- [ ] Make `process_wait()` block until the specified child thread exits,
      then return its exit status (or `-1` for bad/duplicate/non-child tids).
- [ ] Wire the exit status through to `thread_exit()`/`process_exit()` and
      print the `"%s: exit(%d)\n"` message tests expect.

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
