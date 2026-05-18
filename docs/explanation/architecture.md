# Architecture

YukiSend is a LAN file transfer and chat application. Peers on the same network
discover each other automatically, exchange chat messages, and transfer files
and folders directly over TCP. There is no server, no cloud relay, and no
internet dependency.

## What it does

- Discovers peers on the local network via UDP broadcast (subnet-directed, not
  `255.255.255.255` which Windows blocks asymmetrically).
- Transfers text messages over a persistent JSON-framed TCP chat channel.
- Transfers files and folders peer-to-peer over a per-file TCP connection using
  a length-prefixed JSON header followed by raw file bytes.
- Sends images (from clipboard paste or file picker) with inline thumbnail
  preview in the chat view; clicking the thumbnail opens a fullscreen overlay.
- Presents all of this through a custom-painted Qt 6 UI that owns every
  pixel — no QSS, no platform widget styling.

## Layer breakdown

```
┌─────────────────────────────────┐
│           UI Layer              │  src/ui/
│  Custom QWidget subclasses,     │
│  all drawing via QPainter       │
└────────────┬────────────────────┘
             │ signals / slots
┌────────────▼────────────────────┐
│         App Layer               │  src/app/
│  App.cpp — central controller   │
│  Routes signals, owns network   │
│  objects, maintains message     │
│  store and pending-offer table  │
└────────────┬────────────────────┘
             │ direct calls / signals
┌────────────▼────────────────────┐
│       Network Layer             │  src/network/
│  Discovery  — UDP announce/     │
│               expire loop       │
│  ChatServer — JSON TCP frames,  │
│               incoming messages │
│  ChatClient — outbound frames   │
│  TransferServer — per-file TCP  │
│                   server        │
│  TransferClient — per-file TCP  │
│                   sender        │
└─────────────────────────────────┘

src/model/   — Message, ChatStore (pure data, no I/O)
src/theme/   — constexpr color/spacing/font constants (no runtime state)
src/utils/   — FileUtils: recursive directory listing, size formatting
```

Dependencies flow strictly downward: UI → App → Network/Model. Nothing in
Network or Model knows about UI. Theme and utils are leaves — imported by any
layer, they import nothing from the project.

## Network protocols

### Discovery — UDP broadcast

Port **48321**. Wire format is UTF-8, newline-delimited key=value:

```
YUKISEND/1\nid=<uuid>\nname=<display>\nport=<tcp>\n
```

Each peer broadcasts an announce packet every 5 s to all subnet broadcast
addresses (obtained from `QNetworkInterface::allInterfaces()` — see decision
doc). A goodbye packet (`bye=1`) is sent on clean shutdown. Peers that have
not announced for 30 s are expired from the list.

Using `255.255.255.255` was found to be blocked asymmetrically on Windows;
subnet-directed broadcasts (`192.168.x.255` etc.) work reliably on both ends.

### Chat channel — TCP JSON frames

Port **48322**. A persistent TCP connection is kept open between peers while the
app is running. Messages are framed as:

```
[4 bytes big-endian: JSON length] [JSON bytes]
```

`Protocol::MessageType` enum values embedded in the JSON:

| Type          | Value | Direction         | Purpose                        |
|---------------|-------|-------------------|--------------------------------|
| ChatMessage   | 0x10  | both              | Plain text chat                |
| FileRequest   | 0x11  | sender → receiver | Offer a file/folder/image transfer |
| FileAccept    | 0x12  | receiver → sender | Accept the offer               |
| FileReject    | 0x13  | receiver → sender | Deny the offer                 |
| RequestAgain  | 0x14  | receiver → sender | Re-request a previously sent file |

`remoteMsgId` in a `FileAccept`/`FileReject` echoes back the sender's local
`msgId` so the sender can match the response to the correct pending offer.

### Transfer channel — per-file TCP

Port **48320**. A new TCP connection is made for every file (or directory entry).
The sender writes a length-prefixed JSON header, then raw file bytes:

```
[4 bytes big-endian: header length] [JSON header] [file bytes]
```

Header fields:

| Field    | Type    | Present when        | Meaning                              |
|----------|---------|---------------------|--------------------------------------|
| `name`   | string  | always              | File or directory name               |
| `size`   | number  | always              | File size in bytes (0 for dirs)      |
| `relPath`| string  | folder transfer     | Path including root: `Photos/sub/f`  |
| `isDir`  | bool    | directory entries   | True → receiver must `mkpath`, no data follows |

For directory entries and 0-byte files the sender flushes and disconnects
immediately after the header; no payload bytes are sent. The receiver handles
these explicitly without waiting for data.

Folder transfers enumerate all files **and** subdirectories (including empty
ones) via `FileUtils::listFiles()`, sort by `relPath` so parents are created
before children, and send one TCP connection per entry.

## Image transfer

Images are treated as a distinct `MessageType::Image` — separate from `File` so
the UI can show an inline thumbnail without the user having to open a file
manager.

**Sending:** `App::sendImage()` encodes the `QImage` to a PNG temp file under
`QStandardPaths::TempLocation`, then follows the same File offer/accept flow as
a regular file. The `FileRequest` payload carries an `"image": true` flag so the
receiver knows to set `MessageType::Image` on their local message. It also
carries a `"thumb"` field: a base64-encoded 40×40 JPEG (quality 40, ~600–1100
bytes) generated by `ImageUtils::makeThumbnail` + `ImageUtils::encodeJpeg`.

**Compose mode:** Pasting an image from the clipboard (`Ctrl+V` in
`ChatInputBar`) enters compose mode — a 36 px thumbnail and a cancel (✕) button
are shown in the input bar instead of the text field. Pressing Enter sends;
pressing Escape or the ✕ cancels.

**Blurred preview:** On the receiver side, `App::routeIncoming` decodes the
base64 thumbnail into `Message::thumbData`. `MessageDelegateRenderer::thumbnail()`
detects a not-Done image with non-empty `thumbData`, scales it up to fill the
preview box (320 × 180 px), runs `ImageUtils::boxBlur(radius=6)` on the result,
and caches the blurred `QImage` in `m_thumbnails`. A centered circular download
button is drawn over the darkened blur. The cache entry is invalidated when the
transfer completes so the sharp image replaces the blur.

**UI rendering:** `MessageDelegateRenderer` renders Image messages with a
rounded-rect thumbnail (up to 320 × 180 px). Thumbnails are lazy-loaded and
cached in a `QHash<qint64, QImage>` keyed by message ID. Box blur is O(w+h) per
pass (sliding-window, two passes), so it runs once on cache miss and is free
on every subsequent `paintEvent`.

**Fullscreen viewer:** Clicking a completed image thumbnail opens `MediaViewer`,
a child `QWidget` overlay on `ChatView`. It fades in (180 ms), scales the image
to fit with a 48 px margin, and dismisses on click or Escape.

## UI design language

Telegram Desktop day-blue theme: clean, minimal, pixel-precise. All spacing,
sizing, and color values are defined as `constexpr` in `src/theme/Theme.h` —
no magic numbers in widget code. Controls inherit `QWidget` and override
`paintEvent()`. Animations use the `Animator` class (`src/ui/Animator.h`), a
lightweight timer-driven 0→1 progress value with three easing functions
(`easeOutCirc`, `easeOutCubic`, `linear`). Font rendering uses
platform-appropriate hinting.

High-DPI note: Qt 6 scales logical pixels by `devicePixelRatio` automatically.
All constants in `Theme.h` are plain logical pixel values — no manual DPI
scaling is applied. The visual difference between a 125 % and a 100 % display
is intentional: the 125 % screen has a higher pixel density; logical units are
physically smaller on it. There is no workaround at the application level.

## Text selection — chat messages

`ChatView` owns the selection state: `m_selMsgIdx` (which message), `m_selFrom` and `m_selTo` (character indices, inclusive/exclusive). Only one message can be selected at a time; any new click clears the previous selection.

- **Mouse down** on a text message body starts a selection at the character under the cursor (`MessageDelegateRenderer::charAtX`).
- **Mouse drag** extends the selection; `m_selAnchor` stays fixed at the click point so backward drag works correctly.
- **Mouse release** ends the drag; the selection persists until the next click.
- **Ctrl+C** copies the selected substring to the clipboard.
- **Hover copy button** (⎘) appears to the right of any text message on hover; clicking it copies the current selection or the full message text. After clicking it flashes ✓ in accent color for 600 ms (single-shot `QTimer`).

`MessageDelegateRenderer::charAtX` maps an absolute pixel X to a character index using `QFontMetrics::horizontalAdvance` — the same font (`Fonts::regular(SizeBody)`) used to draw the text. `draw()` receives a `TextSelection` and paints a highlight rect behind the text before rendering characters on top.

## Text selection — input bar

`ChatInputBar` implements its own text selection to match standard input box behavior without using `QLineEdit`.

- `m_cursorPos` is the moving end of the selection; `m_selStart` is the anchor end. Selection = `[min, max)` of the two.
- **Mouse click** in the input area positions cursor and anchor at the clicked character; drag extends cursor while anchor stays fixed.
- **Shift+Left/Right/Home/End** extend the selection; bare arrow keys collapse it to the appropriate end.
- **Ctrl+A** selects all. **Ctrl+C/X** copy/cut. **Ctrl+V** replaces selection then pastes (images enter compose mode).
- **Typing or Backspace/Delete** replaces the selection first if one is active.
- **IME (Chinese, Japanese, Korean, etc.):** `inputMethodEvent` stores `preeditString` in `m_preedit`. The preedit is drawn inline at the cursor position with an underline; `commitString` inserts the finalized characters and clears `m_preedit`. The cursor is hidden during composition. `inputMethodQuery` returns `ImCursorPosition`, `ImAnchorPosition`, `ImSurroundingText`, and `ImCurrentSelection` so the platform IME panel has full context.

## Peer list behavior

- Clicking an unselected peer opens the chat area for that peer.
- Clicking the already-selected peer deselects it and hides the chat area.
- When the discovery layer removes a peer (timeout or goodbye packet), if that
  peer was selected the chat area is hidden automatically (`peerDeselected`
  signal from `PeerListWidget`).

## Build

CMake. Qt 6 found via:

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
```

Single executable target `YukiSend`. No external runtime dependencies beyond
Qt itself.
