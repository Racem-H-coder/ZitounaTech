#ifndef FINGERPRINTDIALOG_H
#define FINGERPRINTDIALOG_H

#include <QDialog>

class QLabel;
class QPushButton;
class QProgressBar;
class QTimer;
class ArduinoHandler;

class FingerprintDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FingerprintDialog(ArduinoHandler *handler, QWidget *parent = nullptr);

signals:
    void enrollCompleted(int slotId);
    void enrollCancelled();

private slots:
    void onScan1Done();
    void onRemoveFingerRequested();
    void onScan2Done();
    void onFingerSaved(int slotId);
    void onFingerError(const QString &message);
    void onTimeout();
    void onCancelClicked();

private:
    void setStepText(const QString &text, int progressValue);

    ArduinoHandler *m_handler = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_iconLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QTimer *m_timeoutTimer = nullptr;
};

#endif // FINGERPRINTDIALOG_H
