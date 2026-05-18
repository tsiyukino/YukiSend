# 2026-05-14 — Network protocol: mDNS + raw TCP

## Context

YukiSend needs two things from its network stack: automatic peer discovery on a LAN, and a fast binary file transfer channel. LocalSend (the reference app) uses HTTPS + mDNS. We evaluated the same split.

## Options considered

**Option A: mDNS discovery + HTTPS/TLS transfer**
Same as LocalSend. TLS provides encryption; HTTP framing gives a familiar structure. Downsides: QSslSocket adds complexity, certificate management is non-trivial (self-signed certs with no CA need manual trust on each peer), and HTTP framing is unnecessary overhead for a binary stream.

**Option B: mDNS discovery + raw TCP binary protocol**
mDNS for zero-config discovery; QTcpServer/QTcpSocket for transfer. A simple length-prefixed binary protocol replaces HTTP framing. No certificates, no TLS handshake overhead.

## Decision

Option B.

The threat model for LAN transfer is weak — an attacker must be on the same local network. The added complexity of TLS is not justified by the security gain in this context. If encryption is needed later, a ChaCha20 stream layer can be added over the existing TCP channel without changing the protocol structure.

Raw TCP also fits the project's stated goals: low complexity, minimal dependencies, no magic.

## Consequences

- mDNS library choice is still open. Candidates: OS-native (dns-sd on Windows/macOS, Avahi on Linux) or a vendored single-header library. Decide before the network layer is built.
- The binary protocol must be versioned from day one so future additions don't break existing peers.
