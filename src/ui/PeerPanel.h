#pragma once

#include <QWidget>
#include <QPropertyAnimation>

#include "network/Discovery.h"      // Peer
#include "model/ChatStore.h"        // StorageStrategy
#include "model/PeerStore.h"
#include "theme/Theme.h"

// Slide-in overlay panel for per-peer settings.
// Position and size are managed by MainWindow. The panel slides in over the
// right side of the chat area without pushing content.
class PeerPanel : public QWidget {
    Q_OBJECT
public:
    explicit PeerPanel(QWidget *parent = nullptr);

    // Load state from PeerStore before showing. strategy is the current
    // per-peer value from ChatStore so the radio reflects the real setting.
    void openFor(const Peer &peer, PeerStore *store, StorageStrategy strategy);

    void slideIn();
    void slideOut();

    bool isOpen() const { return m_open; }

signals:
    void savedChanged(const QString &peerId, bool saved);
    void favoriteChanged(const QString &peerId, bool favorite);
    void blockedChanged(const QString &peerId, bool blocked);
    void strategyChanged(const QString &peerId, StorageStrategy strategy);
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // Row hit rects (relative to this widget's origin)
    QRect closeRect()     const;
    QRect savedRect()     const;
    QRect favoriteRect()  const;
    QRect blockedRect()   const;
    QRect persistRect()   const;
    QRect sessionRect()   const;

    void paintToggleRow(QPainter &p, const QRect &row,
                        bool on, bool hovered, const QString &label) const;
    void paintRadioRow(QPainter &p, const QRect &row,
                       bool checked, bool hovered, const QString &label) const;
    void paintSectionLabel(QPainter &p, int y, const QString &text) const;

    // State
    Peer            m_peer;
    PeerStore      *m_store      = nullptr;
    bool            m_saved      = false;
    bool            m_favorite   = false;
    bool            m_blocked    = false;
    StorageStrategy m_strategy   = StorageStrategy::Persistent;
    bool            m_open       = false;

    // Hover flags
    bool m_closeHover    = false;
    bool m_savedHover    = false;
    bool m_favoriteHover = false;
    bool m_blockedHover  = false;
    bool m_persistHover  = false;
    bool m_sessionHover  = false;

    QPropertyAnimation *m_anim;

public:
    // Panel width used by MainWindow for geometry calculations.
    static constexpr int kW = Theme::Space::PeerPanelWidth;
};
