# Project 4 TODOs

This builds on the same codebase as Project 3 (`project3/`), not a fresh
checkout. Project 3's `userprog` work (syscalls, argument passing,
wait/exec, deny-write-to-executables) stays as-is and must keep passing —
see [TODO.md](TODO.md) and [REVIEW.md](REVIEW.md) for that history.

**Scope confirmed:** this project is **threads + filesys + userprog only,
no `vm/`**. [filesys/Make.vars](filesys/Make.vars) defaults to exactly that
("no-vm") track — `vm` and `-DVM` are present only as commented-out lines
for an optional 10% extra-credit path, and none of the raw requirements
(indexed files, path-aware syscalls, new directory syscalls, finer
synchronization) touch paging/swap/mmap. Don't implement `vm/` unless that
decision changes.

| Area | Directory | State |
|---|---|---|
| Indexed/extensible files | [filesys/inode.c](filesys/inode.c) | Single-extent only — needs redesign |
| Subdirectories | [filesys/directory.c](filesys/directory.c), [filesys/filesys.c](filesys/filesys.c) | Flat root-only namespace — no path resolution at all |
| New directory syscalls | [userprog/syscall.c](userprog/syscall.c) | Numbers/prototypes already exist; unimplemented (fall through to `sys_exit(-1)`) |
| Per-process cwd | [threads/thread.h](threads/thread.h) | Field doesn't exist yet |
| Synchronization | [userprog/syscall.c](userprog/syscall.c)'s `filesys_lock` | One global lock for the whole filesystem — too coarse for this project |
| `extract`/`append` | [filesys/fsutil.c](filesys/fsutil.c) | Currently **skips** directory entries entirely |

---

## 1. Indexed and Extensible Files — [filesys/inode.c](filesys/inode.c), [filesys/inode.h](filesys/inode.h)

`struct inode_disk` is a single extent today:

```c
struct inode_disk {
    block_sector_t start;       /* First data sector. */
    off_t          length;      /* File size in bytes. */
    unsigned       magic;       /* Magic number. */
    uint32_t       unused[125]; /* Not used. */
};
```

`inode_create()` allocates that one contiguous run up front
(`free_map_allocate(sectors, &disk_inode->start)`), and `inode_write_at()`
still has the old limiting comment: *"Normally a write at end of file
would extend the inode, but growth is not yet implemented."* `byte_to_sector()`
also assumes the single-extent layout (`inode->data.start + pos /
BLOCK_SECTOR_SIZE`).

TODO:
- [x] Replace `inode_disk`'s layout with an indexed scheme — direct,
      indirect, and doubly-indirect block pointers are the standard choice
      (any scheme is acceptable as long as you justify it in the design doc
      and it doesn't suffer external fragmentation). It must fit in exactly
      `BLOCK_SECTOR_SIZE` (512 bytes) — `inode_create()` already asserts
      this (`ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE)`), so the
      pointer counts need to be sized to match.
- [x] Support files as large as the 8 MB partition allows (minus metadata)
      — per [filesys/README.md](filesys/README.md), this requires at least
      one doubly-indirect block.
- [x] Rewrite `byte_to_sector()` to walk the new index structure instead of
      a single `start + offset`.
- [x] Implement real file growth: `inode_create()` should be able to start
      a file at length 0, and `inode_write_at()` must allocate new sectors
      on demand when a write extends past the current length — zero-filling
      any gap between the old EOF and the write position. Sparse
      (don't-allocate-until-written) or eager-zero allocation are both
      acceptable; pick one and say so in the design doc.
- [x] Remove the file-growth caveat comment in `inode_write_at()` once this
      works.
- [x] A read starting past EOF must return 0 bytes (already true as long as
      `inode_length()` is accurate); a seek past EOF must not itself extend
      the file (only a later write does).

---

## 2. Subdirectories / Path Resolution — [filesys/directory.c](filesys/directory.c), [filesys/filesys.c](filesys/filesys.c), [threads/thread.h](threads/thread.h)

Right now there is no hierarchy at all. `filesys_create()`/`filesys_open()`/
`filesys_remove()` in [filesys/filesys.c](filesys/filesys.c) **always**
call `dir_open_root()` and look the name up there — a name containing `/`
or a relative path has no meaning today. `struct dir_entry` and
`struct inode_disk` have no way to say "this entry/inode is a directory."
There are no `.`/`..` entries anywhere.

TODO:
- [x] Mark directory inodes as directories — add an `is_dir`-style flag
      somewhere reachable from an inode (e.g. in `struct inode_disk`, or
      tracked alongside it), so code can tell a file inode from a directory
      inode.
- [x] When a directory is created, write `.` (self) and `..` (parent)
      entries into it pointing at the right inode sectors — `dir_create()`/
      `dir_add()` in [filesys/directory.c](filesys/directory.c) don't do
      this today.
- [x] Write a path-resolution routine (new code, most naturally in
      [filesys/filesys.c](filesys/filesys.c)) that:
      - Treats a leading `/` as absolute (start from the root directory)
        and anything else as relative to the **calling process's cwd**.
      - Splits on `/`, treating `a//b` the same as `a/b` (collapse repeated
        slashes).
      - Handles `.` and `..` specially, including the FAQ's rule that the
        root's `..` is the root itself (so `/../x` == `/x`).
      - Returns the containing directory and final component name
        separately, since `mkdir("/a/b/c")` needs `/a/b` to already exist
        and `/a/b/c` to not.
- [x] Route `filesys_create()`, `filesys_open()`, and `filesys_remove()`
      through that resolver instead of `dir_open_root()`.
- [x] Add a `filesys_mkdir()`-equivalent (or extend `dir_create()`'s
      caller) that resolves the parent, allocates a new inode sector,
      calls `dir_create()`, writes `.`/`..`, and `dir_add()`s it into the
      parent — failing if the parent path doesn't fully exist or the name
      already exists.
- [x] Add a per-process current working directory: a `struct dir *cwd`
      (or equivalent) field on `struct thread` in
      [threads/thread.h](threads/thread.h), next to the `pstatus`/`fd_list`
      fields already added for Project 3. Initialize the first process's
      cwd to the root in `process_execute()`/`load()`
      ([userprog/process.c](userprog/process.c)); `exec()` must have the
      child inherit the parent's cwd (reopen the same directory, not
      re-resolve a path) rather than resetting to root. Close it in
      `process_exit()`.
- [x] Update `dir_remove()`/`filesys_remove()` semantics: a directory can
      only be removed if it's empty (nothing besides `.`/`..`), and the
      root can never be removed. Decide — and document in the design
      doc — whether removing a directory that's open or is some process's
      cwd is allowed; if you allow it, opening/creating inside a removed
      directory must then fail. `dir-rm-cwd`, `dir-rm-parent`, and
      `dir-rm-root` specifically probe this behavior.
---

## 3. New Directory Syscalls — [userprog/syscall.c](userprog/syscall.c)

The syscall numbers and user-facing prototypes already exist from Project
3's setup — [lib/syscall-nr.h](lib/syscall-nr.h) has `SYS_CHDIR`,
`SYS_MKDIR`, `SYS_READDIR`, `SYS_ISDIR`, `SYS_INUMBER`, and
[lib/user/syscall.h](lib/user/syscall.h) already declares `chdir()`,
`mkdir()`, `readdir()`, `isdir()`, `inumber()` (plus `READDIR_MAX_LEN`).
`syscall_handler()` has all five directory syscalls wired.

TODO:
- [x] Add `sys_chdir(const char *dir)`: resolve `dir` (absolute or
      relative) to a directory, swap it in for the calling thread's cwd on
      success. Returns `false` without changing the cwd on failure.
- [x] Add `sys_mkdir(const char *dir)`: thin wrapper over the
      `filesys_mkdir()`-equivalent from item 2.
- [x] Add `sys_readdir(int fd, char *name)`: only valid for an fd that's a
      directory; must skip `.`/`..` (the provided `dir_readdir()` doesn't
      filter these out on its own — check and filter if needed); writes
      into a buffer sized `READDIR_MAX_LEN + 1`.
- [x] Add `sys_isdir(int fd)` and `sys_inumber(int fd)`: `inumber()` can be
      implemented directly with the already-existing
      `inode_get_inumber()`/`struct inode *` reachable from either a
      `struct file` or `struct dir`.
- [x] Extend `struct fd_entry` (currently just `{ int fd; struct file
      *file; struct list_elem elem; }`) so a single fd table can represent
      either an open file or an open directory — e.g. add a `bool is_dir`
      and a `struct dir *dir` member (or a tagged union). `sys_open()` must
      check the new directory flag from item 2 and populate the right
      variant.
- [x] Update `sys_close()` to close whichever variant the fd actually is —
      the spec calls out that `close` is the *only* existing call required
      to accept a directory fd, implying `read`/`write`/`seek`/`tell` on a
      directory fd should fail gracefully instead.
- [x] Update `sys_read()`/`sys_write()` to return `-1` (not crash) when
      called on a directory fd.

---

## 4. Existing File Syscalls Must Accept Paths — [userprog/syscall.c](userprog/syscall.c)

`sys_create()`, `sys_open()`, and `sys_remove()` currently just forward
their string argument straight to `filesys_create()`/`filesys_open()`/
`filesys_remove()`. Once item 2's path resolver lives inside those
`filesys_*()` functions, these call sites don't need much rewriting — but
double check:

TODO:
- [ ] `sys_open()` must succeed for a path that names a directory (storing
      it via the new `fd_entry` directory variant from item 3) — `create`
      is explicitly **not** how directories get made (that's `mkdir`), so
      `sys_create()` on a path that resolves to an existing directory (or
      whose last component is meant to be a directory) should still behave
      like today: fail or create a regular file, never silently make a
      directory.
- [ ] `sys_remove()` must now also succeed for an **empty** directory
      (besides the root), in addition to regular files.

---

## 5. Finer-Grained Synchronization — [userprog/syscall.c](userprog/syscall.c), [filesys/](filesys)

Project 3 added exactly one `static struct lock filesys_lock` (private to
[userprog/syscall.c](userprog/syscall.c), exposed via
`syscall_filesys_lock_acquire()`/`syscall_filesys_lock_release()` in
[userprog/syscall.h](userprog/syscall.h)) and wrapped *every* filesystem
call in it — correct at the time, since the unmodified `filesys/` code
wasn't reentrant at all. This project explicitly requires removing that:
*"Your submission must adopt a finer-grained synchronization strategy that
does not require external synchronization... operations on independent
entities should be independent."*

TODO:
- [ ] Remove (or significantly narrow) the single `filesys_lock`
      chokepoint; push synchronization down into `filesys/` itself instead
      of guarding it from the syscall layer.
- [ ] Add synchronization sized to the actual shared resource, e.g.: a
      lock per open inode (guarding concurrent growth/length updates to
      *that* inode — ties directly into item 1), a lock per directory
      (guarding concurrent `dir_add()`/`dir_remove()`/`readdir()` on *that*
      directory), and something narrow around free-map bitmap allocation
      (which is inherently one shared resource, so it may legitimately keep
      a single lock — just not one that also blocks unrelated files from
      being read/written at the same time).
- [ ] Verify two processes operating on two different files can actually
      proceed concurrently without blocking on each other — that's the
      explicit goal, not merely "doesn't crash under concurrency."
      `syn-rw` (and its `-persistence` check) specifically test this.

---

## 6. `extract`/`append` Must Handle Paths — [filesys/fsutil.c](filesys/fsutil.c)

The README says this "should not require any significant extra effort,"
but there's a real bug waiting here: `fsutil_extract()` currently has this
branch, which **silently skips every directory entry** in the tar archive:

```c
} else if (type == USTAR_DIRECTORY) {
    printf("ignoring directory %s\n", file_name);
}
```

The Project 4 test harness's `tar` helper program builds nested directory
trees (used by `dir-mk-tree`, `dir-vine`, etc.) and relies on `extract` to
recreate that structure in the Pintos filesystem before the regular files
inside it can be created — if directories are still being ignored, every
nested-path file create will fail because its parent directory was never
made.

TODO:
- [ ] Replace that `printf("ignoring directory...")` branch with an actual
      call to the new mkdir-equivalent from item 2, so directory entries in
      an extracted archive really get created.
- [ ] Confirm `fsutil_extract()`'s `filesys_create()`/`filesys_open()` calls
      and `fsutil_append()`'s `filesys_open()` call correctly resolve
      multi-component paths once item 2 lands (they should, for free, since
      they already go through the generic `filesys_*()` entry points rather
      than calling `dir_*()` directly) — but verify with a nested-path
      extract/append by hand, not just by reading the diff.

---

## 7. Design Document — [filesys/filesys_design_doc.txt](filesys/filesys_design_doc.txt)
- [ ] Fill in the blank "EE 461S Project 4: File Systems" template once the
      above is implemented — in particular, justify the inode indexing
      scheme chosen in item 1, and the directory-removal-while-open
      decision from item 2.

---

## Verifying progress

Build and test from **`filesys/`** (this project's directories are
`threads devices lib lib/kernel userprog filesys`, per
[filesys/Make.vars](filesys/Make.vars); `vm` stays commented out):

```bash
cd $PINTOS/filesys
make
make check          # runs tests/userprog, tests/filesys/base, tests/filesys/extended
```

Per [tests/filesys/Grading.no-vm](tests/filesys/Grading.no-vm), scoring
breaks down as:

| Weight | Source |
|---|---|
| 30% | `tests/filesys/extended/Rubric.functionality` |
| 15% | `tests/filesys/extended/Rubric.robustness` |
| 20% | `tests/filesys/extended/Rubric.persistence` |
| 20% | `tests/filesys/base/Rubric` |
| 10% | `tests/userprog/Rubric.functionality` |
| 5%  | `tests/userprog/Rubric.robustness` |

**Note on "66 tests":** counting distinct named test targets directly from
each suite's `Make.tests`, I get 62 in `tests/userprog`, 13 in
`tests/filesys/base`, and 23 "regular" + 22 "-persistence" (`syn-rw` has no
persistence variant) in `tests/filesys/extended` — none of those groupings
sums to exactly 66 on their own. If your assignment materials say 66, treat
that as the number to confirm once `make check` actually runs end to end
(it will print `N of M tests failed` / `All M tests passed`), rather than
something to force-fit in advance — the discrepancy is worth a quick
sanity check with whoever gave you that number, but isn't a blocker to
starting implementation.
