#include "connection.h"
#include <QDebug>
#include <QStringList>

namespace {

QStringList odbcConnectionCandidates()
{
    QStringList candidates;

    const QString explicitConnect = qEnvironmentVariable("ZITOUNATECH_DB_ODBC_CONNECT").trimmed();
    const QString envDsn = qEnvironmentVariable("ZITOUNATECH_DB_DSN").trimmed();
    const QString driver = qEnvironmentVariable("ZITOUNATECH_DB_DRIVER").trimmed();
    const QString dbq = qEnvironmentVariable("ZITOUNATECH_DB_DBQ").trimmed();

    if (!explicitConnect.isEmpty())
        candidates << explicitConnect;

    if (!envDsn.isEmpty())
        candidates << QStringLiteral("DSN=%1;").arg(envDsn);

    // Common DSN aliases used in the project/team.
    candidates << QStringLiteral("DSN=zitouna;")
               << QStringLiteral("DSN=ZITOUNA;")
               << QStringLiteral("DSN=Zitounatech;");

    const QString finalDriver = driver.isEmpty() ? QStringLiteral("Oracle in XE") : driver;
    const QString finalDbq = dbq.isEmpty() ? QStringLiteral("localhost:1521/XE") : dbq;
    candidates << QStringLiteral("Driver={%1};DBQ=%2;").arg(finalDriver, finalDbq);

    candidates.removeDuplicates();
    return candidates;
}

} // namespace

Connection::Connection() {}

bool Connection::createConnect()
{
    const QString defaultConn = QStringLiteral("qt_sql_default_connection");
    if (QSqlDatabase::contains(defaultConn)) {
        db = QSqlDatabase::database(defaultConn);
        if (db.isOpen()) {
            qDebug() << "Connexion BD deja ouverte";
            return true;
        }
    } else {
        db = QSqlDatabase::addDatabase(QStringLiteral("QODBC"));
    }
    const QString user = qEnvironmentVariable("ZITOUNATECH_DB_USER").trimmed().isEmpty()
                             ? QStringLiteral("racem")
                             : qEnvironmentVariable("ZITOUNATECH_DB_USER").trimmed();
    const QString pass = qEnvironmentVariable("ZITOUNATECH_DB_PASSWORD").trimmed().isEmpty()
                             ? QStringLiteral("1234")
                             : qEnvironmentVariable("ZITOUNATECH_DB_PASSWORD").trimmed();

    db.setUserName(user);
    db.setPassword(pass);

    const QStringList candidates = odbcConnectionCandidates();
    QStringList failureDetails;
    for (const QString &candidate : candidates) {
        db.setDatabaseName(candidate);
        if (db.open()) {
            qDebug() << "Connexion BD OK via" << candidate;
            lastErrorMessage.clear();
            return true;
        }
        failureDetails << QStringLiteral("[%1] %2").arg(candidate, db.lastError().text());
    }

    lastErrorMessage = failureDetails.join(QStringLiteral("\n"));
    qDebug() << "Connexion FAIL:" << lastErrorMessage;
    return false;
}

void Connection::closeConnect()
{
    if (db.isOpen())
        db.close();
}

QString Connection::lastError() const
{
    return lastErrorMessage;
}
