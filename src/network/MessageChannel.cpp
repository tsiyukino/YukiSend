#include "MessageChannel.h"

#include <QTcpSocket>
#include <QJsonDocument>
#include <QtEndian>

MessageChannel::MessageChannel(QTcpSocket *socket, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
{
    connect(m_socket, &QTcpSocket::readyRead,
            this, &MessageChannel::onReadyRead);

    connect(m_socket, &QTcpSocket::disconnected,
            this, &MessageChannel::disconnected);
}

bool MessageChannel::isConnected() const {
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void MessageChannel::send(Protocol::MessageType type,
                          qint64 msgId,
                          const QJsonObject &payload)
{
    QJsonObject env;
    env[QStringLiteral("type")]    = static_cast<qint64>(static_cast<quint32>(type));
    env[QStringLiteral("msgId")]   = msgId;
    env[QStringLiteral("payload")] = payload;

    const QByteArray data = QJsonDocument(env).toJson(QJsonDocument::Compact);

    quint32 len = qToBigEndian(static_cast<quint32>(data.size()));
    m_socket->write(reinterpret_cast<const char *>(&len), sizeof(len));
    m_socket->write(data);
}

void MessageChannel::onReadyRead() {
    m_buf += m_socket->readAll();

    // Parse as many complete frames as are buffered.
    while (true) {
        if (m_frameLen == 0) {
            if (m_buf.size() < 4) return;
            quint32 raw = 0;
            memcpy(&raw, m_buf.constData(), 4);
            m_frameLen = qFromBigEndian(raw);
            m_buf.remove(0, 4);

            if (m_frameLen == 0 || m_frameLen > static_cast<quint32>(Protocol::MaxFrameBytes)) {
                // Malformed frame — drop connection.
                m_socket->abort();
                return;
            }
        }

        if (static_cast<quint32>(m_buf.size()) < m_frameLen) return;

        const QByteArray frameData = m_buf.left(static_cast<int>(m_frameLen));
        m_buf.remove(0, static_cast<int>(m_frameLen));
        m_frameLen = 0;

        const QJsonObject env = QJsonDocument::fromJson(frameData).object();
        if (env.isEmpty()) continue; // skip malformed JSON

        const auto rawType = static_cast<quint32>(
            env[QStringLiteral("type")].toInteger());
        const qint64 msgId  = env[QStringLiteral("msgId")].toInteger();
        const QJsonObject payload = env[QStringLiteral("payload")].toObject();

        emit frameReceived(static_cast<Protocol::MessageType>(rawType),
                           msgId, payload);
    }
}
