#ifndef PINDIALOG_H
#define PINDIALOG_H

#include <QDialog>

class QLabel;
class QPushButton;
class QTimer;
class ArduinoHandler;

class PinDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PinDialog(ArduinoHandler *handler, QWidget *parent = nullptr);
    QString pinCode() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onDigitReceived(int digit);
    void onBackspaceReceived();
    void onConfirmReceived();
    void onTimeout();
    void onConfirmButtonClicked();
    void onArduinoError(const QString &message);

private:
    void updateDisplay();

    ArduinoHandler *m_handler = nullptr;
    QString m_pin;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_messageLabel = nullptr;
    QLabel *m_pinDisplayLabel = nullptr;
    QPushButton *m_confirmButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QTimer *m_timeoutTimer = nullptr;
};

#endif // PINDIALOG_H
