# Future Modules: Cooperate and Git

Date: 2026-05-14  
Status: Deferred — document only, no implementation planned yet.

---

## Cooperate

A shared-file collaboration mode. One file is shared between peers on the LAN,
but only one person can edit it at a time.

**Model: pessimistic locking with a designated host.**

- The file has a Host — the peer who owns/created the shared session.
- Other peers can request edit access. The host grants or denies it.
- While one peer holds the edit lock, all others see the file as read-only.
- Lock transfer: TBD — whether the host can transfer host role if they go
  offline is an open question. Simplest first version: lock is released when
  the holder disconnects; host must re-grant to someone else.

**Open questions:**
- What happens when the host goes offline permanently? Does a new host get
  elected, or does the session end?
- Is the lock per-file or per-session (a session can contain multiple files)?

---

## Git

A Git-based collaborative workflow over LAN, with no dependency on GitHub or
any external hosting service.

**Key insight:** Git is fully decentralised. `git push` and `git pull` work
over any transport, including raw LAN addresses. No GitHub required — data
never leaves the local network.

**Planned approach:**
- Use the system `git` binary (via `QProcess`) rather than implementing a
  custom VCS. This gives full Git compatibility at a fraction of the
  implementation cost.
- Peers share a repository over the LAN directly (peer-to-peer, no server).
- Workflow inside YukiSend:
  1. A peer pushes a branch via YukiSend.
  2. All members of the group chat see a "merge request" message.
  3. Members discuss and vote/approve in the chat.
  4. When approved, the designated host merges the branch to main.

**Why not build a custom VCS?**
- Diff algorithms, merge strategies, and conflict resolution are the hard parts
  of Git — reimplementing them correctly is a large separate project.
- Standard Git compatibility means users can use existing tools (editors, CI,
  etc.) alongside YukiSend without friction.
- A custom local VCS is a valid independent project but should not block
  YukiSend development.

**Open questions:**
- How are bare repos shared? Does each peer expose a git-daemon, or does
  YukiSend transfer pack files over its own TCP protocol?
- Conflict resolution UI: what does the merge conflict screen look like?

---

## Implementation order

1. **Send** — chat UI, file transfer with accept/reject, message history,
   group chat. (current focus)
2. **Cooperate** — pessimistic-lock shared editing.
3. **Git** — LAN-based branch/merge workflow using system git.
4. **Settings** — per-peer storage strategy, theme, display name, etc.
