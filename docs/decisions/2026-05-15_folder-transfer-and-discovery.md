# 2026-05-15 — Folder transfer protocol and UDP discovery fix

## Context

Two problems were found during testing:

1. **Asymmetric discovery**: machine A could see machine B in the peer list,
   but B could not see A. Both ran identical builds.
2. **Folder transfer bugs**: folders sent without the root directory name,
   empty directories and 0-byte files were skipped entirely, and the progress
   bar stuck at 0 % for folders containing subdirectories.

---

## Discovery: subnet-directed broadcast

### Problem

`QUdpSocket::writeDatagram(..., QHostAddress::Broadcast, port)` sends to
`255.255.255.255`. On Windows, the Windows Firewall and the networking stack
process this packet differently per interface — one direction works, the other
is silently dropped.

### Decision

Enumerate all active, non-loopback IPv4 interfaces using
`QNetworkInterface::allInterfaces()` and send one datagram per interface to
its specific subnet broadcast address (`entry.broadcast()`). Fall back to
`255.255.255.255` only when no interfaces are found.

This is implemented in `Discovery.cpp` via `subnetBroadcasts()` and
`Private::sendDatagram()`.

### Consequences

Discovery is now symmetric on Windows. No OS-level firewall rule change is
required. The extra datagrams per announce are negligible (typically 1–2
interfaces on a laptop).

---

## Folder transfer: per-entry TCP connections

### Protocol

Each file and directory inside the folder is sent as a separate TCP connection
to port 48320. The JSON header includes a `relPath` field that encodes the full
relative path including the root folder name (e.g. `Photos/2024/cat.jpg`). The
receiver recreates the directory tree using `QDir::mkpath`.

### Directory entries and 0-byte files

The original code rejected any transfer with `fileSize <= 0`, which silently
dropped all directories and empty files.

**Directory entries** (`isDir: true` in header): the sender writes the header
and immediately disconnects — no payload bytes follow. The receiver calls
`mkpath(savePath)` and emits `transferFinished` without opening a file.

**0-byte files**: the sender writes the header and disconnects. The receiver
opens a `QFile` in `WriteOnly` mode (creating the file on disk), closes it
immediately, and emits `transferFinished`.

### relPath includes the root folder name

`FileUtils::listFiles(dir)` prefixes every entry's `relPath` with the folder's
own `dirName()`. This means the receiver always recreates the top-level folder,
not just its contents. Example:

```
sender sends:  /home/user/Photos/
receiver gets: relPath = "Photos/2024/cat.jpg"
               saved to: <downloadDir>/Photos/2024/cat.jpg
```

### Sort order

Entries are sorted by `relPath` before queuing, so parent directories are always
sent before their children. The receiver can safely call `mkpath` in the order
packets arrive.

### Sender: disconnected-signal chain

`TransferClient` uses a queue (`QList<PendingFile>`) and starts one TCP
connection per entry. When a connection closes (either after data or
immediately for dirs/0-byte files), the `disconnected` signal fires
`startNext()`, which pops the next entry and connects again after a 10 ms
delay (allows the socket to fully close before reconnecting to the same port).

### Receiver: guard against use-after-free

`TransferServer` calls `cleanup(xfer)` inside `onReadyRead` for dir and 0-byte
entries (immediately after the header is parsed). The `disconnected` and
`errorOccurred` signal handlers check `d->transfers.contains(xfer)` before
touching `xfer` to prevent use-after-free when the socket signals after the
transfer struct has already been deleted.
