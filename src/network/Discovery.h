#pragma once

#include <QObject>
#include <QString>
#include <QList>

struct Peer {
    QString id;           // unique device identity UUID (stable across restarts)
    QString displayName;
    QString address;
    quint16 port;
    // Random UUID broadcast by each instance; stored in known_peers on first trust.
    // FUTURE: Replace with Ed25519 public key — see FingerprintDialog.cpp.
    QString fingerprint;
};

// Announces this device via UDP broadcast and maintains a list of discovered peers.
// Emits peersChanged() whenever the list updates.
class Discovery : public QObject {
    Q_OBJECT
public:
    // selfId and fingerprint must be stable across restarts (loaded from AppSettings).
    explicit Discovery(quint16 listenPort, const QString &selfId,
                       const QString &selfFingerprint, QObject *parent = nullptr);
    ~Discovery() override;

    void start();
    void stop();

    QList<Peer> peers() const;

    // Change the name broadcast to peers. Takes effect on the next announce
    // (≤ 5 s) and triggers an immediate re-announce if already running.
    void setDisplayName(const QString &name);

signals:
    void peersChanged();

private:
    struct Private;
    Private *d;
};
