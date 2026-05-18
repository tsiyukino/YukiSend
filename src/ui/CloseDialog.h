#pragma once

#include <QDialog>

#include "model/CloseAction.h"

class QRadioButton;
class QCheckBox;

// One-shot dialog asking "minimize to tray or quit?".
// Read choice() and rememberChoice() after exec() returns QDialog::Accepted.
class CloseDialog : public QDialog {
    Q_OBJECT
public:
    explicit CloseDialog(QWidget *parent = nullptr);

    CloseAction choice() const;
    bool        rememberChoice() const;

private:
    QRadioButton *m_minimizeRadio;
    QRadioButton *m_quitRadio;
    QCheckBox    *m_rememberCheck;
};
