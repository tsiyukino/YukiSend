#pragma once

// FUTURE: Upgrade fingerprint authentication to Ed25519 key pairs.
//
// Current implementation uses a random UUID as the fingerprint. This prevents
// accidental mis-connection and passive IP spoofing, but does NOT protect against
// an active attacker who observes the UDP discovery broadcast and clones the UUID.
//
// To upgrade:
// 1. Replace UUID generation in AppSettings::fingerprint() with an Ed25519 key pair.
//    Store the private key in QSettings (or the OS credential store for better security).
//    The fingerprint becomes the SHA-256 of the public key, displayed as a hex string.
// 2. At the start of each ChatClient TCP connection, perform a challenge-response:
//    the server sends a random 32-byte nonce; the connecting peer signs it with its
//    private key and sends back the signature.
// 3. The verifying peer checks the signature against the public key derived from the
//    peer's broadcasted fingerprint. Only then is the connection accepted.
// 4. This requires adding libsodium (crypto_sign_ed25519) or OpenSSL
//    (EVP_DigestSignInit with EVP_PKEY_ED25519) as a CMake dependency, and a new
//    Protocol::MessageType::Handshake frame at the start of each ChatClient session.
//
// Until then, the UUID approach is sufficient for trusted LAN environments where
// passive eavesdropping is the primary concern, not active impersonation.

#include <QWidget>
#include <QString>

#include "network/Discovery.h"  // Peer

// Full-QPainter modal overlay dialog for peer fingerprint trust decisions.
// Matches the visual language of PeerPanel (Inter font, Theme colors, hand-drawn controls).
//
// Usage:
//   auto *dlg = FingerprintDialog::forUnknownPeer(peer, parentWidget);
//   dlg->exec();
//   switch (dlg->userResult()) { ... }
//   delete dlg;
class FingerprintDialog : public QWidget {
    Q_OBJECT
public:
    enum class UserResult { Trust, Block, Later };

    // "New device detected" variant.
    static FingerprintDialog *forUnknownPeer(const Peer &peer, QWidget *parent = nullptr);

    // "Fingerprint changed" warning variant.
    static FingerprintDialog *forMismatch(const Peer &peer,
                                          const QString &knownFp,
                                          const QString &seenFp,
                                          QWidget *parent = nullptr);

    // Blocks until the user clicks a button. Returns immediately if parent is null.
    void exec();

    UserResult userResult() const { return m_result; }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Variant { Unknown, Mismatch };

    explicit FingerprintDialog(Variant variant,
                               const Peer &peer,
                               const QString &knownFp,
                               const QString &seenFp,
                               QWidget *parent);

    void recomputeCard();

    // Button hit rects (widget-local coordinates, computed in paintEvent)
    QRect m_primaryRect;    // Trust / Trust New Fingerprint
    QRect m_secondaryRect;  // Block
    QRect m_tertiaryRect;   // Ask Me Later (Unknown variant only)

    // Hover state
    bool m_primaryHover   = false;
    bool m_secondaryHover = false;
    bool m_tertiaryHover  = false;

    Variant    m_variant;
    Peer       m_peer;
    QString    m_knownFp;
    QString    m_seenFp;
    UserResult m_result  = UserResult::Later;
    bool       m_done    = false;

    // Card dimensions (set in updateGeometry)
    int m_cardW = 0;
    int m_cardH = 0;
    int m_cardX = 0;
    int m_cardY = 0;
};
