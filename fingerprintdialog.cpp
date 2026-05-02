#include "fingerprintdialog.h"

#include "arduinohandler.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

FingerprintDialog::FingerprintDialog(ArduinoHandler *handler, QWidget *parent)
    : QDialog(parent)
    , m_handler(handler)
{
    setModal(true);
    setWindowTitle(tr("Enregistrement Empreinte"));
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);

    m_iconLabel = new QLabel(QStringLiteral("👆"), this);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setStyleSheet(QStringLiteral("font-size: 46px;"));

    m_statusLabel = new QLabel(tr("Posez votre doigt sur le capteur"), this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setWordWrap(true);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setFormat(tr("Scan 1"));

    auto *buttonsRow = new QHBoxLayout();
    buttonsRow->addStretch();
    m_cancelButton = new QPushButton(tr("Annuler"), this);
    buttonsRow->addWidget(m_cancelButton);

    layout->addWidget(m_iconLabel);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_progressBar);
    layout->addLayout(buttonsRow);

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(30000);
    m_timeoutTimer->start();

    connect(m_timeoutTimer, &QTimer::timeout, this, &FingerprintDialog::onTimeout);
    connect(m_cancelButton, &QPushButton::clicked, this, &FingerprintDialog::onCancelClicked);

    if (m_handler) {
        connect(m_handler, &ArduinoHandler::scan1Done, this, &FingerprintDialog::onScan1Done);
        connect(m_handler, &ArduinoHandler::removeFingerRequested, this, &FingerprintDialog::onRemoveFingerRequested);
        connect(m_handler, &ArduinoHandler::scan2Done, this, &FingerprintDialog::onScan2Done);
        connect(m_handler, &ArduinoHandler::fingerSaved, this, &FingerprintDialog::onFingerSaved);
        connect(m_handler, &ArduinoHandler::fingerError, this, &FingerprintDialog::onFingerError);
        QTimer::singleShot(0, this, [this]() {
            if (m_handler)
                m_handler->startFingerprintEnroll();
        });
    }
}

void FingerprintDialog::onScan1Done()
{
    setStepText(tr("Scan 1 validé. Retirez votre doigt."), 33);
    m_progressBar->setFormat(tr("Scan 1 terminé"));
}

void FingerprintDialog::onRemoveFingerRequested()
{
    setStepText(tr("Retirez puis reposez votre doigt."), 50);
    m_progressBar->setFormat(tr("Retrait doigt"));
}

void FingerprintDialog::onScan2Done()
{
    setStepText(tr("Scan 2 validé. Enregistrement en cours..."), 66);
    m_progressBar->setFormat(tr("Scan 2 terminé"));
}

void FingerprintDialog::onFingerSaved(int slotId)
{
    setStepText(tr("Empreinte enregistrée avec succès !"), 100);
    m_progressBar->setFormat(tr("Enregistré"));
    m_cancelButton->setEnabled(false);
    m_timeoutTimer->stop();
    emit enrollCompleted(slotId);
    QTimer::singleShot(2000, this, [this]() { accept(); });
}

void FingerprintDialog::onFingerError(const QString &message)
{
    setStepText(tr("Erreur empreinte: %1").arg(message), m_progressBar->value());
    m_timeoutTimer->stop();
    emit enrollCancelled();
    QTimer::singleShot(900, this, [this]() { reject(); });
}

void FingerprintDialog::onTimeout()
{
    setStepText(tr("Temps écoulé. Enregistrement annulé."), m_progressBar->value());
    if (m_handler)
        m_handler->cancelFingerprint();
    emit enrollCancelled();
    reject();
}

void FingerprintDialog::onCancelClicked()
{
    if (m_handler)
        m_handler->cancelFingerprint();
    emit enrollCancelled();
    reject();
}

void FingerprintDialog::setStepText(const QString &text, int progressValue)
{
    m_statusLabel->setText(text);
    m_progressBar->setValue(progressValue);
}
