#ifndef ARDUINOHANDLER_H
#define ARDUINOHANDLER_H

#include <QObject>
#include <QSerialPort>

class ArduinoHandler : public QObject
{
    Q_OBJECT
public:
    explicit ArduinoHandler(QObject *parent = nullptr);

    bool open(const QString &portName, int baudRate, QString *errorOut = nullptr);
    void close();
    bool isOpen() const;

public slots:
    void startPinEntry();
    void startFingerprintEnroll();
    void startFingerprintVerify();
    void cancelFingerprint();
    void deleteFingerprint(int slotId);
    void sendToArduino(const QString &command);

signals:
    void digitReceived(int digit);
    void confirmReceived();
    void backspaceReceived();
    void scan1Done();
    void removeFingerRequested();
    void scan2Done();
    void fingerSaved(int slotId);
    void fingerError(const QString &message);
    void fingerprintMatch(int slotId, int confidence);
    void fingerprintNoMatch();
    void fingerprintTimeout();
    void fingerprintDeleted(int slotId);
    void deviceError(const QString &message);
    void rawMessageReceived(const QString &message);
    void accessCodeCheckRequested(const QString &code);
    void pumpCycleComplete(qulonglong cycleId);

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);

private:
    QSerialPort m_serial;
    QByteArray m_buffer;
};

#endif // ARDUINOHANDLER_H
