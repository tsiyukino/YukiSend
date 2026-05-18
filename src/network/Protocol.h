#pragma once

#include <QtTypes>

// Binary protocol over TCP.
//
// ── File-data channel (port 48320) ──────────────────────────────────────────
// Legacy raw-bytes framing used by TransferClient / TransferServer.
// Frame: [4 bytes big-endian header length] [JSON header] [raw file bytes]
//
// ── Chat channel (port 48321) ───────────────────────────────────────────────
// JSON envelope framing used by ChatClient / ChatServer / MessageChannel.
// Frame: [4 bytes big-endian payload length] [UTF-8 JSON]
// JSON: { "type": <uint>, "msgId": <int64>, "payload": { ... } }
namespace Protocol {

inline constexpr quint32 Version      = 1;
inline constexpr quint16 DataPort     = 48320; // file bytes (TCP, TransferServer)
inline constexpr quint16 DiscoveryPort= 48321; // peer discovery (UDP, Discovery)
inline constexpr quint16 ChatPort     = 48322; // chat + file signalling (TCP, ChatServer)

enum class MessageType : quint32 {
    // ── file-data channel (reserved, not used on chat port) ─────────────────
    Handshake      = 0x01,
    FileOffer      = 0x02,
    AcceptOffer    = 0x03,
    RejectOffer    = 0x04,
    DataChunk      = 0x05,
    TransferDone   = 0x06,
    Cancel         = 0x07,

    // ── chat channel ─────────────────────────────────────────────────────────
    // payload fields shown in comments
    Hello          = 0x10, // { "id": str } — first frame on every new connection
    ChatMessage    = 0x11, // { "text": str }
    FileRequest    = 0x12, // { "name": str, "size": int64 }
    FileAccept     = 0x13, // {}
    FileReject     = 0x14, // {}
    RequestAgain   = 0x15, // {}
};

inline constexpr qint32 MaxChunkSize    = 64 * 1024; // 64 KB
inline constexpr qint32 MaxFrameBytes   = 4 * 1024 * 1024; // 4 MB safety cap

} // namespace Protocol
