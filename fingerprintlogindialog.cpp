#include "fingerprintlogindialog.h"

#include "arduinohandler.h"

#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

FingerprintLoginDialog::FingerprintLoginDialog(ArduinoHandler *handler, QWidget *parent)
    : QDialog(parent)
    , m_handler(handler)
{
    setModal(true);
    setWindowTitle(tr("Connexion biométrique"));
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);

    m_iconLabel = new QLabel(QStringLiteral("👆"), this);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setStyleSheet(QStringLiteral("font-size: 48px;"));

    m_statusLabel = new QLabel(tr("En attente de votre empreinte..."), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);

    auto *buttonsRow = new QHBoxLayout();
    buttonsRow->addStretch();
    m_cancelButton = new QPushButton(tr("Annuler"), this);
    buttonsRow->addWidget(m_cancelButton);

    layout->addWidget(m_iconLabel);
    layout->addWidget(m_statusLabel);
    layout->addLayout(buttonsRow);

    auto *effect = new QGraphicsOpacityEffect(this);
    m_iconLabel->setGraphicsEffect(effect);
    m_pulseAnimation = new QPropertyAnimation(effect, "opacity", this);
    m_pulseAnimation->setDuration(950);
    m_pulseAnimation->setStartValue(0.35);
    m_pulseAnimation->setEndValue(1.0);
    m_pulseAnimation->setLoopCount(-1);
    m_pulseAnimation->start();

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(10000);
    m_timeoutTimer->start();

    connect(m_cancelButton, &QPushButton::clicked, this, &FingerprintLoginDialog::onCancelClicked);
    connect(m_timeoutTimer, &QTimer::timeout, this, &FingerprintLoginDialog::onDialogTimeout);

    if (m_handler) {
        connect(m_handler, &ArduinoHandler::fingerprintMatch, this, &FingerprintLoginDialog::onMatch);
        connect(m_handler, &ArduinoHandler::fingerprintNoMatch, this, &FingerprintLoginDialog::onNoMatch);
        connect(m_handler, &ArduinoHandler::fingerprintTimeout, this, &FingerprintLoginDialog::onTimeoutSignal);
        connect(m_handler, &ArduinoHandler::fingerError, this, &FingerprintLoginDialog::onFingerError);
        QTimer::singleShot(0, this, [this]() {
            if (m_handler)
                m_handler->startFingerprintVerify();
        });
    }
}

FingerprintLoginDialog::~FingerprintLoginDialog() = default;

void FingerprintLoginDialog::onMatch(int slotId, int confidence)
{
    m_timeoutTimer->stop();
    m_statusLabel->setText(tr("Empreinte reconnue (score: %1).").arg(confidence));
    emit loginSuccess(slotId);
    QTimer::singleShot(400, this, [this]() { accept(); });
}

void FingerprintLoginDialog::onNoMatch()
{
    m_statusLabel->setText(tr("Empreinte non reconnue."));
    emit loginFailed();
    if (m_handler) {
        QTimer::singleShot(250, this, [this]() {
            if (m_handler)
                m_handler->startFingerprintVerify();
        });
    }
}

void FingerprintLoginDialog::onTimeoutSignal()
{
    m_statusLabel->setText(tr("Délai dépassé côté capteur."));
    emit loginFailed();
    QTimer::singleShot(600, this, [this]() { reject(); });
}

void FingerprintLoginDialog::onFingerError(const QString &message)
{
    m_statusLabel->setText(tr("Erreur capteur: %1").arg(message));
    emit loginFailed();
    QTimer::singleShot(600, this, [this]() { reject(); });
}

void FingerprintLoginDialog::onDialogTimeout()
{
    if (m_handler)
        m_handler->cancelFingerprint();
    m_statusLabel->setText(tr("Temps écoulé."));
    emit loginFailed();
    reject();
}

void FingerprintLoginDialog::onCancelClicked()
{
    if (m_handler)
        m_handler->cancelFingerprint();
    emit loginCancelled();
    reject();
}
