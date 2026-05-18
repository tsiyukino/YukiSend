#include "CloseDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QCheckBox>
#include <QPushButton>
#include <QDialogButtonBox>

CloseDialog::CloseDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Close YukiSend"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(24, 24, 24, 16);

    auto *label = new QLabel(QStringLiteral("What should happen when you close the window?"), this);
    label->setWordWrap(true);
    layout->addWidget(label);

    m_minimizeRadio = new QRadioButton(QStringLiteral("Minimize to system tray"), this);
    m_minimizeRadio->setChecked(true);
    layout->addWidget(m_minimizeRadio);

    m_quitRadio = new QRadioButton(QStringLiteral("Quit YukiSend"), this);
    layout->addWidget(m_quitRadio);

    layout->addSpacing(4);

    m_rememberCheck = new QCheckBox(QStringLiteral("Remember my choice"), this);
    layout->addWidget(m_rememberCheck);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    setFixedWidth(360);
}

CloseAction CloseDialog::choice() const {
    return m_quitRadio->isChecked() ? CloseAction::Quit : CloseAction::MinimizeToTray;
}

bool CloseDialog::rememberChoice() const {
    return m_rememberCheck->isChecked();
}
