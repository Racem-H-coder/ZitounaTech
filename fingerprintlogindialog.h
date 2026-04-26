#ifndef FINGERPRINTLOGINDIALOG_H
#define FINGERPRINTLOGINDIALOG_H

#include <QDialog>

class QLabel;
class QPushButton;
class QPropertyAnimation;
class QTimer;
class ArduinoHandler;

class FingerprintLoginDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FingerprintLoginDialog(ArduinoHandler *handler, QWidget *parent = nullptr);
    ~FingerprintLoginDialog() override;

signals:
    void loginSuccess(int slotId);
    void loginFailed();
    void loginCancelled();

private slots:
    void onMatch(int slotId, int confidence);
    void onNoMatch();
    void onTimeoutSignal();
    void onFingerError(const QString &message);
    void onDialogTimeout();
    void onCancelClicked();

private:
    ArduinoHandler *m_handler = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_iconLabel = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QTimer *m_timeoutTimer = nullptr;
    QPropertyAnimation *m_pulseAnimation = nullptr;
};

#endif // FINGERPRINTLOGINDIALOG_H
