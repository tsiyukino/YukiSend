#pragma once

#include <QString>
#include <QByteArray>
#include <QDateTime>

// A single chat message. Covers text, file offer, and folder offer.
// Stored in SQLite via ChatStore; passed by value throughout the UI layer.

enum class MessageType {
    Text,    // plain text message
    File,    // single-file transfer offer
    Folder,  // folder transfer offer
    Image,   // single image — clipboard paste or file pick; shown as inline thumbnail
};

enum class MessageStatus {
    WaitingAccept,  // offer sent, receiver has not responded
    Transferring,   // accepted, bytes flowing
    Done,           // transfer complete
    Denied,         // receiver denied
    RequestAgain,   // receiver requested re-send after deleting file
    Pending,        // queued for offline peer — not yet sent
    Failed,         // delivery failed (e.g. file was deleted before peer came online)
};

struct Message {
    qint64        id              = -1;    // assigned by ChatStore on insert
    QString       peerId;                  // remote peer's id
    MessageType   type            = MessageType::Text;
    bool          outgoing        = true;  // true = we sent it
    QString       text;                    // MessageType::Text body
    QString       fileName;                // File/Folder display name
    QString       filePath;                // local path (sender) or save path (receiver)
    qint64        fileSize        = 0;     // total bytes
    QString       fileHash;                // SHA-256 hex; empty until computed
    MessageStatus status          = MessageStatus::WaitingAccept;
    QDateTime     timestamp;
    qint64        bytesTransferred = 0;    // updated during transfer
    qint64        remoteMsgId     = -1;    // sender's local msgId, stored on receiver side
                                           // so accept/deny can echo it back correctly
    QByteArray    thumbData;               // JPEG-encoded low-res thumbnail for Image messages;
                                           // blurred by MessageDelegateRenderer before accept
};
