# Project 4 — Concepts & Implementation Notes

This is a companion to [TODO.md](TODO.md): same sections, same bullet
order, same items. Where `TODO.md` tracks *what's done*, this file
explains *why it matters* (the underlying OS/filesystem concept) and
*how we actually built it* (the real functions, data structures, and
design decisions in this codebase).

It only covers items already checked off `[x]` in `TODO.md`. As we
finish more items, this file grows alongside it — think of `TODO.md`
as the checklist and this as the running lecture notes for that
checklist.

---

## 1. Indexed and Extensible Files — [filesys/inode.c](filesys/inode.c), [filesys/inode.h](filesys/inode.h)

### Replace `inode_disk`'s layout with an indexed scheme

**Concept.** The original Pintos inode stores exactly one `start`
sector plus a length — the whole file has to live in one contiguous
run of disk sectors, decided once at creation time. That's called a
*single extent*, and it's exactly how old filesystems like the
original FAT worked: simple, but it means a file can never grow
without either leaving a gap (fragmentation) or being copied
somewhere bigger. An **indexed** (or **multilevel-index**) inode fixes
this the way Unix/ext2-style filesystems do: instead of storing data
sectors directly, the inode stores *pointers* to sectors, and some of
those pointers can themselves point to blocks that are nothing but
more pointers. This removes the "must be contiguous" requirement
entirely — any free sector anywhere on disk can be part of the file.

**What we did.** We replaced the single `start` field with three
pointer groups in [`struct inode_disk`](filesys/inode.h):

```c
struct inode_disk {
    off_t          length;
    unsigned       magic;
    bool           is_dir;
    block_sector_t direct[123];      /* direct[i] *is* a data sector */
    block_sector_t indirect;         /* -> a block of 128 more pointers */
    block_sector_t double_indirect;  /* -> a block of 128 indirect blocks */
};
```

- **Direct** pointers (123 of them) point straight at data sectors —
  no extra disk read needed, fastest path, used for small files.
- The **indirect** pointer points at one extra sector that is itself
  just an array of 128 more sector numbers — one extra disk read buys
  128 more data sectors.
- The **doubly-indirect** pointer points at a sector full of 128
  *indirect-block* pointers, each of which unlocks another 128 data
  sectors — two extra disk reads buys up to 16,384 more data sectors.

A sector number of `0` means "not allocated yet" (sector 0 is
reserved for the free map, so it's safe to use as a sentinel). This
had to fit in exactly 512 bytes (`BLOCK_SECTOR_SIZE`), which is why
the direct count is 123 rather than a rounder number — see the next
item for why.

### Support files as large as the 8 MB partition

**Concept.** The project's own `README.md` specifically requires that
at least one doubly-indirect block be used, so the index has to
support addressing roughly the whole partition, not just "big enough
for the test suite."

**What we did.** We sized the three pointer groups so the struct is
*exactly* `BLOCK_SECTOR_SIZE` (512 bytes) — `inode_create()` already
asserts this (`ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE)`), so
getting the count wrong would panic at boot, not silently misbehave.
The arithmetic: `off_t length` (4) + `unsigned magic` (4) + `bool
is_dir` (1, padded to 4 for alignment) + `123 × 4` direct pointers
(492) + `indirect` (4) + `double_indirect` (4) = 512 bytes exactly.
Capacity: 123 direct + 128 indirect + 128×128 doubly-indirect = 16,635
sectors ≈ 8.1 MB, just over the partition size. (We originally had 124
direct slots before the `is_dir` flag was added in Section 2 — adding
that flag meant giving up exactly one direct slot to keep the struct
at 512 bytes, a negligible capacity loss.)

### Rewrite `byte_to_sector()`

**Concept.** Every read/write needs to turn "byte offset N within
this file" into "which physical disk sector holds that byte." With a
single extent that's one line of arithmetic (`start + N /
BLOCK_SECTOR_SIZE`); with a multilevel index it means figuring out
*which zone* (direct, indirect, or doubly-indirect) the offset falls
into, and walking through the right number of indirection levels.

**What we did.** The new `byte_to_sector()` computes a sector
*index* (`pos / BLOCK_SECTOR_SIZE`), then subtracts off each zone's
capacity in turn until it lands in the right one:
1. If the index is under 123, it's a direct lookup —
   `inode->data.direct[index]`, no extra disk read.
2. Otherwise subtract 123; if what's left is under 128, read the one
   indirect block and index into it.
3. Otherwise subtract 128 more; split the remaining index into
   `first_level / 128` and `second_level % 128`, read the
   doubly-indirect block to find the right indirect block, then read
   *that* block to find the actual data sector.

A small helper, `index_block_sector()`, does the "read this indirect
block off disk and return entry N" work, shared by both the indirect
and doubly-indirect cases rather than duplicated.

### Implement real file growth

**Concept.** The original code's `inode_write_at()` had a comment
admitting growth "is not yet implemented" — writing past the current
end of a file just failed silently. Real growth means: when a write
extends past the current length, allocate whatever new sectors are
needed, make sure any gap between the old end-of-file and the new
write position reads back as zeros (not garbage), and update the
recorded length. There's a design choice here between **sparse**
allocation (only allocate a sector once something is actually written
into it, leaving "holes" in the index) and **eager-zero** allocation
(allocate and zero every sector up to the new length immediately).

**What we did.** We chose **eager-zero allocation** and implemented
it with two reusable helpers:
- `inode_allocate(disk_inode, sector_cnt)` walks sector indices
  `0..sector_cnt-1` using the exact same direct/indirect/doubly-indirect
  math as `byte_to_sector()`, allocating (and zero-filling) only the
  sectors that aren't already there. Because it skips anything
  already allocated, the *same* function works both for creating a
  brand-new file (`inode_create()` can now legitimately start a file
  at length 0) and for growing an existing one.
- `inode_write_at()` now checks `offset + size > inode->data.length`
  at the top; if so, it calls `inode_allocate()` to cover the new end
  position, then updates `length` and writes the inode's own sector
  back to disk immediately (so growth survives a crash right after).

Because every sector is zeroed the moment it's first allocated —
whether that's filling a seek-past-EOF gap or extending into the
write itself — there's no separate "zero the gap" loop needed
anywhere; it falls out of allocation always zero-filling.

### Remove the file-growth caveat comment

**Concept.** Stale comments that describe *old*, no-longer-true
limitations are worse than no comments — they actively mislead the
next reader.

**What we did.** Once growth worked, we replaced the "growth is not
yet implemented" comment in `inode_write_at()` (and two matching ones
in [filesys/file.c](filesys/file.c)'s `file_write()`/`file_write_at()`)
with ones describing what actually happens now: a write past EOF
grows the file first, only coming up short if the disk fills up
during that growth.

### EOF / seek semantics

**Concept.** Two independent correctness rules: (1) reading starting
at or past the current end-of-file must return 0 bytes, never
garbage; (2) merely *seeking* past the end of a file must **not**
itself grow the file — only a subsequent *write* should.

**What we did.** Both already fell out of the existing code once
growth was correct, so this item was mostly verification rather than
new code:
- `inode_read_at()`'s loop computes `inode_left = inode_length(inode)
  - offset`; once that's ≤ 0, `chunk_size` becomes ≤ 0 and the loop
  breaks immediately, returning 0 bytes read.
- `file_seek()` in [filesys/file.c](filesys/file.c) only ever does
  `file->pos = new_pos` — it never touches `inode_write_at()`, so a
  seek alone can never trigger growth.

We verified the trickiest part — that a write *after* seeking far
past EOF zero-fills the entire gap correctly, including across the
direct→indirect sector-index boundary — by actually booting the
kernel under QEMU and running Pintos's own `grow-sparse` test (seeks
to byte 76,542, writes one byte, then reads the whole 76,543-byte file
back and checks every other byte is zero). It passed.

---

## 2. Subdirectories / Path Resolution — [filesys/directory.c](filesys/directory.c), [filesys/filesys.c](filesys/filesys.c)

### Mark directory inodes as directories (`is_dir` flag)

**Concept.** On disk, a directory *is* a file — it's just a file
whose bytes happen to be an array of `struct dir_entry` records
instead of arbitrary data. But code still needs a reliable way to ask
"is this inode a file or a directory?" (e.g. so path resolution can
refuse to descend *into* a regular file as if it were a directory).
That requires one bit of metadata living with the inode itself.

**What we did.** Added `bool is_dir` to `struct inode_disk` (see
above for how we made room for it within the 512-byte limit) and
threaded a new `is_dir` parameter through `inode_create()`, so the
flag is set once, permanently, at creation time — there's no "promote
a file to a directory" operation, matching how real filesystems work.
A new accessor, `inode_is_dir()`, mirrors the existing
`inode_length()` pattern so other modules never reach into
`inode->data` directly. All three existing callers of
`inode_create()` were updated: [directory.c](filesys/directory.c)'s
`dir_create()` passes `true`; [filesys.c](filesys/filesys.c)'s
`filesys_create()` and [free-map.c](filesys/free-map.c)'s
free-map-file creation both pass `false`.

### Write `.` and `..` entries when a directory is created

**Concept.** In Unix-style filesystems, every directory contains two
special self-referencing entries: `.` (a name for the directory
itself) and `..` (a name for its parent). These aren't magic syntax —
they're ordinary directory entries sitting on disk like any other
filename, which is what lets path resolution treat `.`/`..` as plain
path components instead of needing special-case code (see the next
item).

**What we did.** `dir_create()`'s signature grew a `parent_sector`
parameter. After creating the directory's inode, it immediately opens
the (still-empty) new directory and calls the *existing* `dir_add()`
twice — once for `"."` pointing at its own sector, once for `".."`
pointing at `parent_sector`. No new entry-writing logic was needed;
`.`/`..` are just regular entries written through the regular
mechanism, which also means they correctly trigger file growth if the
directory needs its first data sector allocated (which it always
does, starting from length 0).

The root directory is special-cased exactly once, at format time:
`do_format()` calls `dir_create(ROOT_DIR_SECTOR, ROOT_DIR_SECTOR, 16)`
— passing the root's own sector as its "parent." This single line
directly implements the well-known FAQ rule that *the root's `..` is
the root itself*, and it means path resolution never needs a special
case for it later — walking `..` from the root just naturally lands
back on the root via its own on-disk entry.

We verified this by booting the kernel and running the built-in `ls`
kernel action right after formatting: it printed exactly `.` then
`..` and nothing else, confirming both entries exist and point where
expected — and reran the full regression suite to confirm zero
existing tests broke (readdir() returning `.`/`..` to callers that
don't expect them is a known, deliberately deferred concern — see
Section 3's `sys_readdir()` item in `TODO.md`).

### Path-resolution routine

**Concept.** A path like `/a/b/c` has to be broken into components
and walked one directory at a time: start somewhere (root, if
absolute; the calling process's current directory, if relative),
look up the first component, descend into it (it must be a
directory), look up the next component, and so on — stopping one
component short of the end, because callers need to treat the final
component differently (`create` wants it to *not* exist yet, `open`
wants it to exist, etc.). Repeated slashes (`a//b`) should collapse,
and `.`/`..` need to behave like Unix.

**What we did.** Wrote `filesys_resolve_path(path, name_out)` in
[filesys.c](filesys/filesys.c). The key design decision: because
every directory already has real on-disk `.`/`..` entries (previous
item), the resolver **never special-cases those names at all** — it
just does an ordinary `dir_lookup()` for every component, letting `.`
and `..` resolve correctly as a side effect of the directory
structure itself. Mechanically:
- Splits on `/` using `strtok_r()` (from [lib/string.h](lib/string.h)),
  which collapses repeated delimiters for free.
- Walks components with one-token lookahead: if there's a next
  token, the current one must resolve to an existing *directory*
  (checked via `inode_is_dir()`) and we descend into it; if there's
  no next token, the current one is the final component — its name
  is copied out and returned *without* checking whether it exists,
  leaving that decision to the caller.
- The "nothing but slashes" edge case (`"/"`, `"///"`) has no final
  component to split off, so the function returns the root itself
  paired with `name = "."` — letting the caller's own
  `dir_lookup(result, ".", ...)` find the root's inode via its
  self-entry, with zero special-casing needed in any caller.
- Relative paths currently fall back to the root too, since
  `struct thread` has no `cwd` field yet (that's a later item) — and
  with no `mkdir` wired up at the time this was written, root was the
  *only* directory that existed anyway, so this was correct, not a
  shortcut.

We verified this with a temporary boot-time test exercising five
cases (root, a relative name, an absolute name, a missing
intermediate directory, and an intermediate component that's a file
rather than a directory) — all five matched expectations exactly —
then removed the test scaffolding and confirmed the full regression
suite was unaffected (the function was inert until wired in next).

### Route `filesys_create()`/`filesys_open()`/`filesys_remove()` through the resolver

**Concept.** Writing the resolver doesn't help anyone until something
calls it. This item is the "flip the switch" step — the three
original flat-namespace entry points stop assuming "look up this
literal name in the root" and start accepting real paths.

**What we did.** Each function now calls
`filesys_resolve_path(name, final_name)` to get `(containing_dir,
last_component)` instead of unconditionally `dir_open_root()`-ing,
then operates on `final_name` within that directory exactly as
before. Simple in principle — but wiring it up immediately surfaced
**two real regressions**, both fixed as part of this same step:

1. **Silent name truncation.** The resolver copied the final
   component into a fixed `name[NAME_MAX + 1]` buffer with
   `strlcpy()`, which truncates rather than failing on an over-long
   name. Pintos's own `create-long` test (a 511-character filename
   that must be rejected) started *succeeding* instead, because
   `dir_add()`'s own length check never saw the original, over-long
   string — only the already-truncated one. **Fix:** the resolver
   now rejects (returns `NULL`) outright if the final component is
   too long, before ever truncating it.
2. **Root directory became removable.** Before this step, `"/"` was
   just a literal name that could never match anything in a flat
   lookup, so `remove("/")` always failed harmlessly. Once the
   resolver gave `"/"` real meaning (resolving to `(root, ".")`),
   `dir_remove(root, ".")` happily deleted it — breaking Pintos's
   `dir-rm-root` test. **Fix:** added a narrow guard in
   `filesys_remove()` that looks up the resolved name first and
   refuses removal if it points at `ROOT_DIR_SECTOR`, explicitly
   scoped as a minimal patch — the *full* removal semantics (empty
   directories only, policy for removing an open/cwd directory) are
   their own upcoming `TODO.md` item.

We caught both regressions by actually running the full 121-test
suite after the change (not just reasoning about it), fixed both,
and reran the suite to confirm the exact original failure count was
restored — a good demonstration of why "rebuild and test after every
step" matters even for a mechanically simple-looking change.

### `filesys_mkdir()`

**Concept.** Creating a directory is like creating a file (allocate
an inode, link it into its parent) plus the extra step of giving the
new directory its own `.`/`..` entries — which means it's really just
composing pieces that already exist: the resolver (to find the
parent), `dir_create()` (to make the new directory *and* write its
`.`/`..`), and `dir_add()` (to link it into the parent).

**What we did.** `filesys_mkdir()` in [filesys.c](filesys/filesys.c)
resolves the path, allocates a fresh inode sector, calls
`dir_create(inode_sector, parent_sector, 0)` — note the `0` entry
count: since growth works and `.`/`..` are just grown-into entries, a
new directory can start truly empty, exactly like a regular file
would — and links it into the parent via `dir_add()`.

The one subtlety worth calling out: naive cleanup-on-failure (mirror
`filesys_create()`'s "release the one sector I allocated") is *wrong*
here in a case that isn't rare at all — if `dir_create()` succeeds
(writing the new directory's inode *and* the data sector backing its
`.`/`..`) but the final `dir_add()` fails because the name already
exists in the parent, releasing only the inode's header sector would
leak the data sector too, and this is the routine, every-time
behavior of tests like `dir-over-file`/`dir-under-file`, not a rare
disk-full edge case. So cleanup branches on whether `dir_create()`
itself actually succeeded: if it did, we reopen the orphaned inode
and `inode_remove()` + `inode_close()` it (reclaiming every sector it
owns); if it didn't (e.g. disk full mid-allocation), we only release
the one inode sector, accepting the same partial-allocation tradeoff
already documented for `inode_allocate()` in Section 1.

We verified this with a temporary boot-time test covering 8 cases,
including the first-ever test in this project of resolving through a
*non-root* directory: creating `/newdir`, then `/newdir/sub` nested
inside it, then confirming `/newdir/sub`'s `..` entry resolves to the
same sector number as `/newdir` itself (not the root) — proving
`dir_create()`'s parent-sector wiring and the resolver's recursive
descent correctly compose beyond a single level. All 8 checks passed;
the scaffolding was then fully removed and the regression suite
confirmed unaffected.

### Per-process current working directory

**Concept.** Every Unix process has a current working directory —
an already-*open* reference to a directory, not a string path that
gets re-resolved on every use. That distinction matters: if a path
were re-resolved each time, renaming or removing an intermediate
directory could change (or break) what "cwd" means mid-process. Two
more rules complete the picture: `exec()`'d children inherit their
parent's cwd (by sharing the same open directory, not by re-looking
up a path), and the very first process in the system has to get
*something* sensible as a starting point — conventionally the root.

**What we did.** Added `struct dir *cwd` to `struct thread` in
[threads/thread.h](threads/thread.h), right next to the existing
`pstatus`/`fd_list` fields, with no explicit initializer needed:
`thread_create()` allocates every thread's page via
`palloc_get_page(PAL_ZERO)`, so `cwd` starts `NULL` the same way
`pstatus`/`exec_file` already do.

The inheritance logic lives in [process.c](userprog/process.c)'s
`process_execute()` — the single function used for *both* starting
the very first user process (called from `run_task()`) and spawning
every `exec()`'d child (`sys_exec()` is a thin wrapper over it). Right
before creating the new thread, it captures and reopens **its own
caller's** cwd:

```c
args->cwd = thread_current()->cwd != NULL
            ? dir_reopen(thread_current()->cwd)
            : dir_open_root();
```

This single line implements both rules at once: for a normal `exec()`
call, `thread_current()` is the parent process, so this reopens the
parent's actual cwd (sharing its inode, not re-resolving a path) — the
Unix inheritance rule. For the very first process, `thread_current()`
is the initial kernel thread, which never had a cwd of its own (still
`NULL`), so this falls back to `dir_open_root()` — which *is* "the
first process's cwd is the root," arising naturally from the same
code path rather than needing a separate special case. The reopened
directory is bundled through `struct start_process_args` (the same
mechanism already used to pass the command line and exit-status
record across the `thread_create()` boundary) and assigned to
`thread_current()->cwd` inside `start_process()`, right next to where
`pstatus` is already assigned there. `process_exit()` closes it,
mirroring exactly how it already closes `exec_file`.

One thing worth flagging: this is also what finally makes
`filesys_resolve_path()`'s `resolve_start_dir()` helper do real work —
it now checks `thread_current()->cwd` for relative paths instead of
always falling back to root (guarded by `#ifdef USERPROG`, so a
hypothetical filesys-without-userprog build still falls back safely).
Every one of these new accesses to shared filesystem state
(`dir_reopen()`, `dir_open_root()`, `dir_close()`) is wrapped in the
same `syscall_filesys_lock` every other `filesys_*()`/`dir_*()` call
site already uses — that coarse lock is explicitly scheduled for
replacement in Section 5, but until then, it's still the law of the
land and new code has to respect it too.

We verified the mechanism itself with a temporary boot-time test
(there's no `chdir()` syscall yet to drive this from user code, so we
simulated one by assigning `thread_current()->cwd` directly): `mkdir`
a directory, "chdir" into it, create a file with a *relative* name,
and confirm it landed inside that directory rather than the root
(and that switching back to `cwd = NULL` correctly falls back to
root again) — all 7 checks passed. Then, since this touches the
lifecycle of *every* process in the system, we ran the complete
121-test regression suite rather than just the filesys subset:
identical 35-failure baseline, and — most importantly — **zero
failures anywhere in `tests/userprog`**, meaning every test that
spawns, waits on, or concurrently runs multiple processes
(`multi-recurse`, `exec-multiple`, `wait-twice`, `syn-rw`) exercised
the new reopen/close logic on every single process lifecycle without
incident.

### Directory-removal semantics

**Concept.** Removing a *file* is simple: Unix (and the original
Pintos code) lets you unlink a file even while it's open elsewhere —
existing openers keep reading/writing it normally, and the disk space
is only actually freed once the last one closes it. Removing a
*directory* needs stricter rules, because a directory can have
*structure* (things inside it) and *context* (someone currently
"standing" in it as their cwd) that a plain file doesn't:
1. A non-empty directory can't be removed — you'd otherwise orphan
   whatever was inside it, with no path left to ever reach those
   inodes again.
2. The root is the one directory that must never be removable at
   all — the whole filesystem hangs off it.
3. There's a genuine, assignment-acknowledged *design choice* for
   whether removing a directory that's currently open (as an fd, or
   as some process's cwd) should be allowed or refused outright — real
   filesystems differ on this (POSIX generally refuses; Windows also
   generally refuses; a few systems allow it and leave the handle
   working until closed, the way Unix treats open *files*).

**What we did.** We chose to **refuse removing a directory that's
currently in use** (open as a file descriptor, or some process's
cwd) — the "busy" branch of the assignment's two allowed designs —
specifically because it requires **zero new bookkeeping**: every
live reference to a directory (an open fd, *or* a cwd — both reopen
the same inode via `inode_open()`/`inode_reopen()`) already shows up
in the existing `inode->open_cnt` counter. We just needed to expose
it with a new accessor, `inode_open_cnt()`, mirroring `inode_length()`
/`inode_is_dir()`.

All three rules now live together in `dir_remove()`
([directory.c](filesys/directory.c)), the one shared primitive both
`filesys_remove()` and any future `rmdir`-style syscall will call
through:
1. **Root check** — short-circuits immediately if the looked-up
   entry's sector is `ROOT_DIR_SECTOR`, before even opening the
   inode. (This subsumed and replaced Step 4's narrower, explicitly
   "minimal, targeted" root guard that had lived in
   `filesys_remove()` as a stopgap — it's now centralized in one
   place instead of split across two layers.)
2. **Empty check** — a new helper, `dir_is_empty()`, reads through
   the directory's entries the same way `dir_readdir()` already does,
   and returns false the moment it finds anything besides `.`/`..`.
3. **Busy check** — `inode_open_cnt(inode) > 1` (the `> 1` rather
   than `> 0` accounts for `dir_remove()`'s own transient
   `inode_open()` call needed to inspect the entry in the first
   place).

Crucially, both the empty and busy checks are gated behind
`inode_is_dir(inode)` — so ordinary **files** are completely
unaffected and keep their original "removable while open" behavior.
We specifically re-verified this wasn't broken: Pintos's own
`syn-remove` test (which removes a file while another process still
has it open, and checks that process keeps working) still passes.

Since the officially graded tests for this behavior
(`dir-rm-cwd`, `dir-rm-parent`, `dir-rm-root`) call `mkdir()`/
`chdir()`/`readdir()`/`inumber()` as real **syscalls** — and those
syscalls aren't wired up yet (that's Section 3, still ahead of us) —
those tests can't run end-to-end yet; they'll fail at their very
first `mkdir()` call regardless of how correct our `dir_remove()`
logic is. So we verified the logic itself directly, the same way we
verified `filesys_mkdir()` in the previous item: a temporary
boot-time test driving `filesys_mkdir()`/`filesys_create()`/
`filesys_remove()` and `thread_current()->cwd` by hand, covering all
of: root protection, non-empty rejection, busy-while-open rejection,
busy-while-cwd rejection, success once no longer busy/non-empty, and
the file-removal-while-open regression check above. All checks
passed; the scaffolding was removed and the full regression suite
reconfirmed the exact same 35-failure baseline, with `dir-rm-root`
and `syn-remove` both still explicitly passing.

**Design note for the formal design doc:** this decision — refusing
removal of a busy directory, rather than allowing it and blocking
further opens/creates inside it — is exactly the kind of choice
Section 7's `filesys_design_doc.txt` asks to be justified in writing
once that document gets filled in.

---

## 3. New Directory Syscalls — [userprog/syscall.c](userprog/syscall.c)

### `sys_chdir()`

**Concept.** `chdir(path)` changes *where relative paths start*. Because
a process's cwd is an already-open directory handle (not a path string —
see Section 2's cwd item), changing directory means: (1) resolve the path
to a directory, (2) open it, (3) swap that handle in for the old one and
close the old one. The order matters: if resolution fails (missing path, a
regular file, a missing intermediate directory) the process must be left
**exactly as it was** — the old cwd is only closed once the new one is
safely in hand. This is why `chdir` is also what finally lets user code
*move around* the hierarchy that `mkdir` builds.

**What we did.** Two pieces:

1. **`filesys_open_dir(name)`** in [filesys.c](filesys/filesys.c) — a new
   general helper that returns a `struct dir *` for a path, or `NULL`. It
   reuses `filesys_resolve_path()` rather than re-implementing path
   walking: resolve to `(parent_dir, final_name)`, `dir_lookup()` the final
   name inside the parent, then reject it with `inode_is_dir()` if it names
   a regular file, and finally `dir_open()` the inode (which takes
   ownership of it, so there's no leak on either exit path). Nothing about
   it is chdir-specific, so Section 3's `sys_open()` directory support can
   reuse it.

   Because the resolver turns `"/"` into `(root, ".")` and every directory
   has real `.`/`..` entries, edge cases need no code: `chdir("/")`,
   `chdir("//")`, `chdir("/..")`, and `chdir(".")` all just work through
   the same lookup.

2. **`sys_chdir()`** in [syscall.c](userprog/syscall.c) — validates the
   user string, then under `filesys_lock`:

   ```c
   new_cwd = filesys_open_dir(dir);
   if (new_cwd != NULL) {
       old_cwd = cur->cwd;
       cur->cwd = new_cwd;
       dir_close(old_cwd);
   }
   ```

   The swap only happens on success, giving the "return `false` without
   changing cwd" guarantee for free. `dir_close(old_cwd)` is what drops the
   old directory's `open_cnt` — which matters for Section 2's removal rule:
   a directory is "busy" while any process has it as cwd, so *leaving* it
   with `chdir` is what makes it removable again. A new `SYS_CHDIR` case in
   `syscall_handler()` forwards the argument and stores the boolean in
   `eax`.

**Verification.** `mkdir` isn't a syscall yet, so the official `dir-*` tests
can't run end to end. Instead we temporarily repurposed a test to boot the
kernel in QEMU and check, with only `chdir`/`create`: `/`, `/..`, `.`, `//`
succeed; a missing path, `""`, an existing *file*, and `file/x` all fail;
and `chdir("/")` still works after those failures. All 10 checks passed
(the swap is exercised on every success, so a double-close or leak would
have panicked). The test was then reverted. Actually *moving into* a
subdirectory is only testable once `sys_mkdir()` lands, which is the next
item.

---

## Still to come

Sections 1 (Indexed and Extensible Files) and 2 (Subdirectories /
Path Resolution) are both fully complete — every `TODO.md` item in
either section is checked off. In Section 3, `sys_chdir()` is done. What's
left is the rest of Section 3's directory syscalls, Section 4's path-aware
existing syscalls, Section 5's finer-grained locking, Section 6's
`extract`/`append` fix, and Section 7's design document. This file will
grow to cover each one, in the same order, as we complete them.
