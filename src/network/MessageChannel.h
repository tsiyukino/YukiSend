#pragma once

#include <QObject>
#include <QJsonObject>

#include "Protocol.h"

class QTcpSocket;

// Wraps a single QTcpSocket and provides JSON-envelope framing.
//
// Wire format: [4 bytes big-endian length][UTF-8 JSON payload]
// JSON envelope: { "type": uint, "msgId": int64, "payload": {...} }
//
// Ownership: MessageChannel does NOT take ownership of the socket;
// the caller is responsible for the socket's lifetime.
class MessageChannel : public QObject {
    Q_OBJECT
public:
    explicit MessageChannel(QTcpSocket *socket, QObject *parent = nullptr);

    // Send a framed JSON envelope.
    void send(Protocol::MessageType type, qint64 msgId, const QJsonObject &payload);

    bool isConnected() const;

signals:
    void frameReceived(Protocol::MessageType type, qint64 msgId, QJsonObject payload);
    void disconnected();

private slots:
    void onReadyRead();

private:
    QTcpSocket *m_socket;
    QByteArray  m_buf;      // accumulates partial frames
    quint32     m_frameLen = 0; // expected byte count of current frame, 0 = need to read length
};
