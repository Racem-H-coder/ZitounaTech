#include "employeemanager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

namespace {
QString sqlitePath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.trimmed().isEmpty()) {
        base = QCoreApplication::applicationDirPath();
    }
    QDir d(base);
    if (!d.exists())
        d.mkpath(QStringLiteral("."));
    return d.filePath(QStringLiteral("huilerie.sqlite"));
}

QString pinHashSha256(const QString &pin)
{
    return QString::fromUtf8(QCryptographicHash::hash(pin.toUtf8(), QCryptographicHash::Sha256).toHex());
}
} // namespace

bool EmployeeManager::addEmployee(const EmployeeData &data, const QString &pinCode, int fingerSlotId, QString *errorOut) const
{
    if (errorOut)
        errorOut->clear();

    const QString connName = QStringLiteral("sqlite_employees_conn");
    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase::removeDatabase(connName);
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(sqlitePath());
    if (!db.open()) {
        if (errorOut)
            *errorOut = db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS employees ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "oracle_employee_id INTEGER,"
            "nom TEXT NOT NULL,"
            "prenom TEXT,"
            "poste TEXT,"
            "date_embauche TEXT,"
            "pin_hash TEXT NOT NULL,"
            "finger_slot_id INTEGER DEFAULT -1)"))) {
        if (errorOut)
            *errorOut = q.lastError().text();
        db.close();
        QSqlDatabase::removeDatabase(connName);
        return false;
    }

    q.exec(QStringLiteral("ALTER TABLE employees ADD COLUMN oracle_employee_id INTEGER"));
    q.exec(QStringLiteral("ALTER TABLE employees ADD COLUMN finger_slot_id INTEGER DEFAULT -1"));

    q.prepare(QStringLiteral(
        "INSERT INTO employees (oracle_employee_id, nom, prenom, poste, date_embauche, pin_hash, finger_slot_id) "
        "VALUES (:oracle_employee_id, :nom, :prenom, :poste, :date_embauche, :pin_hash, :finger_slot_id)"));
    q.bindValue(QStringLiteral(":oracle_employee_id"), data.id > 0 ? QVariant(data.id) : QVariant(QVariant::Int));
    q.bindValue(QStringLiteral(":nom"), data.nom.trimmed());
    q.bindValue(QStringLiteral(":prenom"), data.prenom.trimmed());
    q.bindValue(QStringLiteral(":poste"), data.poste.trimmed());
    q.bindValue(QStringLiteral(":date_embauche"), data.dateEmbauche.toString(Qt::ISODate));
    q.bindValue(QStringLiteral(":pin_hash"), pinHashSha256(pinCode.trimmed()));
    q.bindValue(QStringLiteral(":finger_slot_id"), fingerSlotId);

    const bool ok = q.exec();
    if (!ok && errorOut) {
        *errorOut = q.lastError().text();
    }
    db.close();
    QSqlDatabase::removeDatabase(connName);
    return ok;
}

bool EmployeeManager::getEmployeeByFingerSlot(int slotId, EmployeeData *employeeOut, QString *errorOut) const
{
    if (errorOut)
        errorOut->clear();
    if (employeeOut)
        *employeeOut = EmployeeData();

    const QString connName = QStringLiteral("sqlite_employees_conn_lookup");
    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase::removeDatabase(connName);
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(sqlitePath());
    if (!db.open()) {
        if (errorOut)
            *errorOut = db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT oracle_employee_id, nom, prenom, poste, date_embauche "
        "FROM employees WHERE finger_slot_id = :slot LIMIT 1"));
    q.bindValue(QStringLiteral(":slot"), slotId);

    if (!q.exec()) {
        if (errorOut)
            *errorOut = q.lastError().text();
        db.close();
        QSqlDatabase::removeDatabase(connName);
        return false;
    }
    if (!q.next()) {
        db.close();
        QSqlDatabase::removeDatabase(connName);
        return false;
    }

    if (employeeOut) {
        employeeOut->id = q.value(0).toInt();
        employeeOut->nom = q.value(1).toString();
        employeeOut->prenom = q.value(2).toString();
        employeeOut->poste = q.value(3).toString();
        employeeOut->dateEmbauche = QDate::fromString(q.value(4).toString(), Qt::ISODate);
    }

    db.close();
    QSqlDatabase::removeDatabase(connName);
    return true;
}

int EmployeeManager::getFingerprintSlotByEmployeeId(int employeeId, QString *errorOut) const
{
    if (errorOut)
        errorOut->clear();

    const QString connName = QStringLiteral("sqlite_employees_conn_fp_lookup");
    if (QSqlDatabase::contains(connName))
        QSqlDatabase::removeDatabase(connName);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(sqlitePath());
    if (!db.open()) {
        if (errorOut)
            *errorOut = db.lastError().text();
        return -1;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT finger_slot_id FROM employees WHERE oracle_employee_id = :id LIMIT 1"));
    q.bindValue(QStringLiteral(":id"), employeeId);
    if (!q.exec()) {
        if (errorOut)
            *errorOut = q.lastError().text();
        db.close();
        QSqlDatabase::removeDatabase(connName);
        return -1;
    }
    const int slotId = q.next() ? q.value(0).toInt() : -1;
    db.close();
    QSqlDatabase::removeDatabase(connName);
    return slotId;
}

bool EmployeeManager::deleteEmployeeFingerprint(int employeeId, QString *errorOut) const
{
    if (errorOut)
        errorOut->clear();

    const QString connName = QStringLiteral("sqlite_employees_conn_delete_fp");
    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase::removeDatabase(connName);
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
    db.setDatabaseName(sqlitePath());
    if (!db.open()) {
        if (errorOut)
            *errorOut = db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE employees SET finger_slot_id = -1 WHERE oracle_employee_id = :id"));
    q.bindValue(QStringLiteral(":id"), employeeId);
    const bool ok = q.exec();
    if (!ok && errorOut)
        *errorOut = q.lastError().text();

    db.close();
    QSqlDatabase::removeDatabase(connName);
    return ok;
}
