#include "arduinohandler.h"

#include <QRegularExpression>
#include <QTimer>

ArduinoHandler::ArduinoHandler(QObject *parent)
    : QObject(parent)
{
    connect(&m_serial, &QSerialPort::readyRead, this, &ArduinoHandler::onReadyRead);
    connect(&m_serial, &QSerialPort::errorOccurred, this, &ArduinoHandler::onErrorOccurred);
}

bool ArduinoHandler::open(const QString &portName, int baudRate, QString *errorOut)
{
    if (errorOut)
        errorOut->clear();

    if (m_serial.isOpen())
        m_serial.close();

    m_serial.setPortName(portName);
    m_serial.setBaudRate(baudRate);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial.open(QIODevice::ReadWrite)) {
        if (errorOut)
            *errorOut = m_serial.errorString();
        return false;
    }

    m_serial.setDataTerminalReady(false);
    m_serial.setRequestToSend(false);
    m_serial.waitForBytesWritten(20);
    m_serial.setDataTerminalReady(true);
    m_serial.setRequestToSend(true);
    m_serial.waitForReadyRead(1200);
    m_serial.readAll();
    m_buffer.clear();
    return true;
}

void ArduinoHandler::close()
{
    if (m_serial.isOpen())
        m_serial.close();
}

bool ArduinoHandler::isOpen() const
{
    return m_serial.isOpen();
}

void ArduinoHandler::startPinEntry()
{
    if (!m_serial.isOpen())
        return;

    // Reliability: many Arduino boards reset when serial opens.
    // Send START_PIN more than once with short delays to avoid lost first command.
    sendToArduino(QStringLiteral("START_PIN"));
    QTimer::singleShot(350, this, [this]() { sendToArduino(QStringLiteral("START_PIN")); });
    QTimer::singleShot(900, this, [this]() { sendToArduino(QStringLiteral("START_PIN")); });
}

void ArduinoHandler::startFingerprintEnroll()
{
    if (!m_serial.isOpen())
        return;

    // Reliability: after opening COM, Arduino can still be rebooting.
    // Send START_FINGER multiple times to avoid staying stuck in IDLE.
    sendToArduino(QStringLiteral("START_FINGER"));
    QTimer::singleShot(350, this, [this]() { sendToArduino(QStringLiteral("START_FINGER")); });
    QTimer::singleShot(900, this, [this]() { sendToArduino(QStringLiteral("START_FINGER")); });
    QTimer::singleShot(1500, this, [this]() { sendToArduino(QStringLiteral("START_FINGER")); });
}

void ArduinoHandler::startFingerprintVerify()
{
    if (!m_serial.isOpen())
        return;

    sendToArduino(QStringLiteral("VERIFY_FINGER"));
    QTimer::singleShot(300, this, [this]() { sendToArduino(QStringLiteral("VERIFY_FINGER")); });
    QTimer::singleShot(800, this, [this]() { sendToArduino(QStringLiteral("VERIFY_FINGER")); });
}

void ArduinoHandler::cancelFingerprint()
{
    sendToArduino(QStringLiteral("CANCEL_FINGER"));
}

void ArduinoHandler::deleteFingerprint(int slotId)
{
    if (slotId < 1)
        return;
    sendToArduino(QStringLiteral("DELETE_FINGER:%1").arg(slotId));
}

void ArduinoHandler::sendToArduino(const QString &command)
{
    if (!m_serial.isOpen())
        return;
    const QByteArray line = command.toUtf8() + "\r\n";
    m_serial.write(line);
    m_serial.flush();
}

void ArduinoHandler::onReadyRead()
{
    m_buffer += m_serial.readAll();
    QList<QByteArray> lines = m_buffer.split('\n');
    m_buffer = lines.isEmpty() ? QByteArray() : lines.takeLast();

    static const QRegularExpression digitOnly(QStringLiteral("^\\d$"));
    for (QByteArray raw : lines) {
        QString msg = QString::fromUtf8(raw).trimmed();
        if (msg.isEmpty())
            continue;
        emit rawMessageReceived(msg);

        if (msg.startsWith(QStringLiteral("DIGIT:"), Qt::CaseInsensitive)) {
            const QString value = msg.mid(6).trimmed();
            if (digitOnly.match(value).hasMatch())
                emit digitReceived(value.toInt());
            continue;
        }

        if (msg.compare(QStringLiteral("BACKSPACE"), Qt::CaseInsensitive) == 0) {
            emit backspaceReceived();
            continue;
        }

        if (msg.compare(QStringLiteral("CONFIRM"), Qt::CaseInsensitive) == 0) {
            emit confirmReceived();
            continue;
        }

        if (msg.startsWith(QStringLiteral("CHECK_CODE:"), Qt::CaseInsensitive)) {
            const QString code = msg.mid(QStringLiteral("CHECK_CODE:").size()).trimmed();
            emit accessCodeCheckRequested(code);
            continue;
        }

        if (msg.compare(QStringLiteral("SCAN1_OK"), Qt::CaseInsensitive) == 0) {
            emit scan1Done();
            continue;
        }

        if (msg.compare(QStringLiteral("REMOVE_FINGER"), Qt::CaseInsensitive) == 0) {
            emit removeFingerRequested();
            continue;
        }

        if (msg.compare(QStringLiteral("SCAN2_OK"), Qt::CaseInsensitive) == 0) {
            emit scan2Done();
            continue;
        }

        if (msg.startsWith(QStringLiteral("FINGER_SAVED:"), Qt::CaseInsensitive)) {
            const int slotId = msg.mid(QStringLiteral("FINGER_SAVED:").size()).trimmed().toInt();
            emit fingerSaved(slotId);
            continue;
        }

        if (msg.startsWith(QStringLiteral("FINGER_ERROR:"), Qt::CaseInsensitive)) {
            emit fingerError(msg.mid(QStringLiteral("FINGER_ERROR:").size()).trimmed());
            continue;
        }

        if (msg.startsWith(QStringLiteral("MATCH:"), Qt::CaseInsensitive)) {
            const QString payload = msg.mid(QStringLiteral("MATCH:").size()).trimmed();
            const QStringList parts = payload.split(QLatin1Char(':'));
            if (parts.size() >= 2) {
                emit fingerprintMatch(parts.at(0).toInt(), parts.at(1).toInt());
            }
            continue;
        }

        if (msg.compare(QStringLiteral("NO_MATCH"), Qt::CaseInsensitive) == 0) {
            emit fingerprintNoMatch();
            continue;
        }

        if (msg.compare(QStringLiteral("TIMEOUT"), Qt::CaseInsensitive) == 0) {
            emit fingerprintTimeout();
            continue;
        }

        if (msg.startsWith(QStringLiteral("FINGER_DELETED:"), Qt::CaseInsensitive)) {
            const int slotId = msg.mid(QStringLiteral("FINGER_DELETED:").size()).trimmed().toInt();
            emit fingerprintDeleted(slotId);
            continue;
        }

        if (msg.startsWith(QStringLiteral("PUMP_CYCLE_COMPLETE"), Qt::CaseInsensitive)) {
            qulonglong cycleId = 0;
            const int sep = msg.indexOf(QLatin1Char(':'));
            if (sep >= 0) {
                bool ok = false;
                const qulonglong parsed = msg.mid(sep + 1).trimmed().toULongLong(&ok);
                if (ok)
                    cycleId = parsed;
            }
            emit pumpCycleComplete(cycleId);
            continue;
        }

        // Backward compatibility for old pump firmware:
        // old sketches only print "Pump OFF" after a 4-second fill cycle.
        if (msg.compare(QStringLiteral("Pump OFF"), Qt::CaseInsensitive) == 0) {
            emit pumpCycleComplete(0);
            continue;
        }

        // tolerate raw digits if firmware sends plain characters
        if (msg.size() == 1 && msg.front().isDigit()) {
            emit digitReceived(msg.toInt());
        }
    }
}

void ArduinoHandler::onErrorOccurred(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
        return;
    emit deviceError(m_serial.errorString());
}
