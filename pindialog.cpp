#include "pindialog.h"

#include "arduinohandler.h"

#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>

PinDialog::PinDialog(ArduinoHandler *handler, QWidget *parent)
    : QDialog(parent)
    , m_handler(handler)
{
    setModal(true);
    setWindowTitle(tr("Saisie du code PIN"));
    setMinimumWidth(360);

    auto *layout = new QVBoxLayout(this);
    m_titleLabel = new QLabel(tr("Saisie du code PIN"), this);
    m_messageLabel = new QLabel(tr("Entrez votre code (4 chiffres)"), this);
    m_pinDisplayLabel = new QLabel(QStringLiteral("_ _ _ _"), this);
    m_pinDisplayLabel->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700; letter-spacing: 4px;"));
    m_pinDisplayLabel->setAlignment(Qt::AlignCenter);

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_messageLabel);
    layout->addWidget(m_pinDisplayLabel);

    auto *buttonsLayout = new QHBoxLayout();
    m_confirmButton = new QPushButton(tr("Confirmer"), this);
    m_confirmButton->setEnabled(false);
    m_cancelButton = new QPushButton(tr("Annuler"), this);
    buttonsLayout->addStretch(1);
    buttonsLayout->addWidget(m_confirmButton);
    buttonsLayout->addWidget(m_cancelButton);
    layout->addLayout(buttonsLayout);

    connect(m_confirmButton, &QPushButton::clicked, this, &PinDialog::onConfirmButtonClicked);
    connect(m_cancelButton, &QPushButton::clicked, this, &PinDialog::reject);

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(60000);
    connect(m_timeoutTimer, &QTimer::timeout, this, &PinDialog::onTimeout);
    m_timeoutTimer->start();

    if (m_handler) {
        connect(m_handler, &ArduinoHandler::digitReceived, this, &PinDialog::onDigitReceived);
        connect(m_handler, &ArduinoHandler::backspaceReceived, this, &PinDialog::onBackspaceReceived);
        connect(m_handler, &ArduinoHandler::confirmReceived, this, &PinDialog::onConfirmReceived);
        connect(m_handler, &ArduinoHandler::deviceError, this, &PinDialog::onArduinoError);
    } else {
        m_messageLabel->setText(tr("Arduino indisponible. Saisissez le code avec le clavier PC."));
    }
}

QString PinDialog::pinCode() const
{
    return m_pin;
}

void PinDialog::keyPressEvent(QKeyEvent *event)
{
    const int key = event->key();
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        onDigitReceived(key - Qt::Key_0);
        return;
    }
    if (key == Qt::Key_Backspace) {
        onBackspaceReceived();
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_NumberSign) {
        onConfirmReceived();
        return;
    }
    QDialog::keyPressEvent(event);
}

void PinDialog::onDigitReceived(int digit)
{
    if (m_pin.size() >= 4)
        return;
    m_pin.append(QString::number(digit));
    updateDisplay();
}

void PinDialog::onBackspaceReceived()
{
    if (m_pin.isEmpty())
        return;
    m_pin.chop(1);
    updateDisplay();
}

void PinDialog::onConfirmReceived()
{
    if (m_pin.size() == 4) {
        accept();
    }
}

void PinDialog::onTimeout()
{
    QMessageBox::warning(this, tr("PIN"), tr("Temps écoulé: saisie PIN annulée."));
    reject();
}

void PinDialog::onConfirmButtonClicked()
{
    onConfirmReceived();
}

void PinDialog::onArduinoError(const QString &message)
{
    m_messageLabel->setText(tr("Erreur Arduino: %1\nSaisissez avec clavier PC.").arg(message));
}

void PinDialog::updateDisplay()
{
    QString mask;
    for (int i = 0; i < 4; ++i) {
        if (i < m_pin.size()) {
            mask += QChar(0x2022);
        } else {
            mask += QLatin1Char('_');
        }
        if (i < 3)
            mask += QLatin1Char(' ');
    }
    m_pinDisplayLabel->setText(mask);
    m_confirmButton->setEnabled(m_pin.size() == 4);
}
