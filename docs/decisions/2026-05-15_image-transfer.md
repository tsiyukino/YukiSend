---
date: 2026-05-15
status: accepted
---

# Image transfer via temp-file + existing File offer/accept flow

## Context

Users wanted to send images directly from the clipboard and see them as inline
thumbnails in the chat, without leaving the app for a file manager.

## Decision

Images are encoded to a PNG temp file before entering the network layer, so
`TransferClient` and `TransferServer` need no changes — they remain generic
byte-stream movers. A new `MessageType::Image` enum value distinguishes image
messages from regular files so the UI can render them differently.

An `"image": true` flag in the `FileRequest` JSON payload lets the receiver set
the correct message type. The same Accept/Deny flow used for files applies
unchanged.

## Consequences

- No new network primitives needed.
- A temp PNG file exists on disk until the OS cleans temp storage. For
  clipboard images this is acceptable; the file is small and short-lived.
- The thumbnail cache in `MessageDelegateRenderer` must be invalidated when a
  transfer completes so the final saved copy replaces the sender's temp-file
  thumbnail.
- `MediaViewer` is a lazy-created child widget of `ChatView`, not a separate
  `QDialog`, so it doesn't trigger a new OS window and animates smoothly over
  the chat surface.
- A 40×40 JPEG thumbnail (~600–1100 bytes base64) is embedded in the
  `FileRequest` frame. The receiver scales it up, blurs it (box blur, two O(w+h)
  passes), and caches the result — so `paintEvent` never pays the blur cost more
  than once per message.

## Alternatives considered

- **In-memory image transfer** (encode to QByteArray, send over chat channel):
  rejected because the chat channel uses small JSON frames and has no provision
  for large binary payloads.
- **Separate image protocol port**: rejected — adds network complexity with no
  benefit over the existing per-file TCP stream.
