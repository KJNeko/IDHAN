# io_uring restriction and transport probes

Evidence for the security claims in
[`../2026-08-02-module-sandbox-io-uring-design.md`](../2026-08-02-module-sandbox-io-uring-design.md)
§6. These are standalone diagnostics, deliberately outside the CMake build — they answer questions
about kernel behaviour, not about IDHAN.

They should be ported into the test suite as part of implementing that spec (§12). Until then they
are the only thing standing behind §6, so they are kept rather than discarded.

```sh
cc -O1 -o ring_probe        ring_probe.c        -luring && ./ring_probe [path]
cc -O1 -o ring_probe2       ring_probe2.c       -luring && ./ring_probe2
cc -O1 -o scm_probe         scm_probe.c         -luring && ./scm_probe
cc -O1 -o inherit_probe     inherit_probe.c     -luring && ./inherit_probe
cc -O1 -o addfd_probe       addfd_probe.c       -luring && ./addfd_probe
cc -O1 -o memfd_slot_probe  memfd_slot_probe.c  -luring && ./memfd_slot_probe
cc -O1 -o sparse_probe      sparse_probe.c      -luring && ./sparse_probe
```

## What each one asks

Read in two groups. The first two established what a restricted ring can and cannot do. The rest
answer *how the ring reaches the worker and how its slots are driven* — a question the first draft
of the spec got wrong.

**`ring_probe.c`** — does a sealed ring leak its registered file's path, do the restrictions actually
deny anything, and does registering the ring fd let us close the real one?

**`ring_probe2.c`** — are register opcodes open by default on a ring that only restricts SQE opcodes,
and does naming one permitted `REGISTER_OP` close the rest?

**`scm_probe.c`** — can an io_uring descriptor cross a unix socket via `SCM_RIGHTS`? **This is the
disproof that forced the redesign.** It cannot.

**`inherit_probe.c`** — does a restricted ring survive `fork` + `exec`, can the parent repoint a
registered slot while the child is reading through it, and does a seccomp filter denying
`io_uring_register` in the child stop the child doing the same? This is the design as it now stands.

**`addfd_probe.c`** — can a seccomp supervisor inject a ring with `SECCOMP_IOCTL_NOTIF_ADDFD`? It
can. Kept because it is the fallback if fork inheritance ever becomes unworkable, and because it
documents that the `SCM_RIGHTS` ban is specific to that path rather than a blanket rule.

**`memfd_slot_probe.c`** — can a memfd back a registered slot? It can, which is what lets the
request-body call sites use the same ring path as a file on disk rather than needing a second one.

**`sparse_probe.c`** — can the file table be enabled empty and filled per call, and does a read
against an unfilled slot fail rather than returning something stale? Both yes, which is what makes
slot release a real release.

## Recorded output

Kernel 7.1.5-arch1-2, liburing 2.15, 2026-08-02.

```
target file: /etc/hostname

1. sealed ring, fdinfo:
  [leak] UserFiles:	1
  [leak]     0: /etc/hostname

2. restrictions:
  READ  + IOSQE_FIXED_FILE -> bytes read (expected)
  READ  without FIXED_FILE -> Permission denied
  OPENAT                   -> Permission denied
  REGISTER_RING_FDS        -> allowed

3. ring with REGISTER_RING_FDS permitted:
  registered ring fd, index 1
  closed real ring fd 4
  [after close] fdinfo unreadable: No such file or directory
  READ after closing the fd -> bytes read (still works)
```

```
slot 0 starts as /etc/hostname; intruder fd is /etc/os-release

=== REGISTER_OP restriction NOT SET ===
  FILES_UPDATE (swap slot 0) -> SUCCEEDED -- slot swapped!
  REGISTER_BUFFERS           -> allowed
  REGISTER_RING_FDS          -> allowed
  slot 0 now reads: "NAME="Arch Linux" PRETTY_NAME=""

=== REGISTER_OP restriction SET (RING_FDS only) ===
  FILES_UPDATE (swap slot 0) -> Permission denied
  REGISTER_BUFFERS           -> Permission denied
  REGISTER_RING_FDS          -> allowed
  slot 0 now reads: "kj-desktop "
```

```
plain file fd        -> sent
io_uring ring fd     -> Invalid argument
restricted ring fd   -> Invalid argument
memfd fd             -> sent
```

```
child: mapped the inherited ring
child: registered the ring and closed its descriptor
child: slot 0 read 0 -> "kj-desktop"
parent: FILES_UPDATE -> slot 0 repointed
child: slot 0 read 1 -> "NAME="Arch Linux""
child: own io_uring_setup -> ALLOWED (before lockdown)
child: locked down
child: own io_uring_setup -> DENIED (Operation not permitted) (after lockdown)
child: FILES_UPDATE -> DENIED (Operation not permitted) (after lockdown)
child: post-lockdown read -> "NAME="Arch Linux""
```

```
supervisor: ADDFD of the ring -> injected as fd 5
child: received fd 5
child: read 11 bytes through the ring: kj-desktop
```

```
register a memfd in the file table -> ok
read the memfd through the ring -> 29 bytes: bytes that arrived over HTTP
```

```
register 4 sparse slots -> ok
read an unfilled slot   -> Bad file descriptor
fill slot 2 post-enable -> ok
read slot 2             -> "kj-desktop"
release slot 2           -> ok
```

## The results that changed the design

**`scm_probe` invalidated the original transport.** The first draft had the server build a ring per
call and attach its descriptor to the CALL frame. The kernel refuses: an io_uring file cannot cross
`SCM_RIGHTS`, because a ring can hold registered files — including unix sockets — and that forms
reference cycles the unix garbage collector cannot break. `EINVAL`, for both a plain ring and a
restricted one, while regular files and memfds pass in the same program. There is no flag for it and
it is not going to change.

**`inherit_probe` is the design that replaced it,** and its last four lines are the load-bearing
part. Ring restrictions are a property of the *context*, which both processes share; seccomp is a
property of the *process*. So `FILES_UPDATE` can stay in the ring's allowlist — letting the server
repoint a slot per call — while the worker is denied `io_uring_register` by its own filter and
cannot do the same. Neither mechanism alone is sufficient. This is why §10's seccomp filter is part
of this spec rather than the follow-up: without it the shared `FILES_UPDATE` is exactly the hole
`ring_probe2` demonstrates.

**`ring_probe2` remains the reason `REGISTER_OP` is set at all.** A ring restricted only by `SQE_OP`
and `SQE_FLAGS_REQUIRED` still permits every register opcode. `IORING_SETUP_R_DISABLED` plus SQE
restrictions is **not** sufficient on its own.

**`ring_probe` step 3 is why `IORING_REGISTER_RING_FDS` is permitted** despite the above. It grants
no capability — it registers a ring the caller already holds — but it is what allows the real
descriptor to be closed, removing the `/proc/self/fdinfo` path leak step 1 demonstrates.
