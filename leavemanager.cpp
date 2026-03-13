#include "leavemanager.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

LeaveManager::LeaveManager(QObject *parent)
    : QObject(parent)
{
    // Best-effort: si la table existe, on charge.
    reloadFromDatabase(nullptr);
}

int LeaveManager::nextId()
{
    m_lastId++;
    return m_lastId;
}

bool LeaveManager::dbAvailable() const
{
    QSqlDatabase db = QSqlDatabase::database();
    return db.isValid() && db.isOpen();
}

bool LeaveManager::leaveTableAvailable() const
{
    if (!dbAvailable())
        return false;
    QSqlQuery q(QSqlDatabase::database());
    // Test "léger" : si la table n'existe pas, Oracle renvoie ORA-00942 (souvent masqué par ODBC).
    return q.exec(QStringLiteral("SELECT 1 FROM CONGE_REQUEST WHERE 1=0"));
}

bool LeaveManager::reloadFromDatabase(QString *errorMessage)
{
    if (errorMessage)
        errorMessage->clear();
    if (!leaveTableAvailable())
        return false;

    QSqlQuery q(QSqlDatabase::database());
    if (!q.exec(QStringLiteral(
            "SELECT CR.ID_CONGE, CR.EMPLOYEE_ID, E.NOM, CR.DATE_DEBUT, CR.DATE_FIN, "
            "CR.STATUS, CR.REASON, CR.CREATED_AT "
            "FROM CONGE_REQUEST CR "
            "LEFT JOIN EMPLOYEE E ON E.ID = CR.EMPLOYEE_ID "
            "ORDER BY CR.ID_CONGE DESC"))) {
        if (errorMessage)
            *errorMessage = q.lastError().text();
        return false;
    }

    m_requests.clear();
    while (q.next()) {
        LeaveRequest r;
        r.id = q.value(0).toInt();
        r.employeeId = q.value(1).toInt();
        r.employeeName = q.value(2).toString();
        r.startDate = q.value(3).toDate();
        r.endDate = q.value(4).toDate();
        const QString st = q.value(5).toString();
        if (st == QStringLiteral("APPROVED"))
            r.status = Approved;
        else if (st == QStringLiteral("REJECTED"))
            r.status = Rejected;
        else
            r.status = Pending;
        r.reason = q.value(6).toString();
        r.createdAt = q.value(7).toDate();
        m_requests.push_back(r);
        m_lastId = qMax(m_lastId, r.id);
    }

    emit leaveDataChanged();
    return true;
}

int LeaveManager::addRequest(int employeeId,
                             const QString &employeeName,
                             const QDate &startDate,
                             const QDate &endDate,
                             const QString &reason,
                             QString *errorMessage)
{
    if (errorMessage)
        errorMessage->clear();

    if (employeeId <= 0) {
        if (errorMessage)
            *errorMessage = tr("Employé invalide.");
        return -1;
    }
    if (!startDate.isValid() || !endDate.isValid() || endDate < startDate) {
        if (errorMessage)
            *errorMessage = tr("Période de congé invalide.");
        return -1;
    }

    // Si BD + table dispo: INSERT en base.
    // Sinon: fallback mémoire (évite les erreurs ODBC "Unable to execute statement").
    if (leaveTableAvailable()) {
        QSqlQuery q(QSqlDatabase::database());
        q.prepare(QStringLiteral(
            "INSERT INTO CONGE_REQUEST (ID_CONGE, EMPLOYEE_ID, DATE_DEBUT, DATE_FIN, STATUS, REASON, CREATED_AT) "
            "VALUES (NULL, :eid, :d1, :d2, 'PENDING', :r, SYSDATE)"));
        q.bindValue(QStringLiteral(":eid"), employeeId);
        q.bindValue(QStringLiteral(":d1"), startDate);
        q.bindValue(QStringLiteral(":d2"), endDate);
        q.bindValue(QStringLiteral(":r"), reason.trimmed());
        if (!q.exec()) {
            // On ne bloque pas l'utilisateur: si la requête échoue (ODBC/Oracle), on passe en mémoire.
            // On garde quand même le message si demandé.
            if (errorMessage)
                *errorMessage = q.lastError().text();
            // Fallback mémoire juste après
        } else {
            // Refresh
            reloadFromDatabase(nullptr);
            // Retourne le dernier id (approx) pour cet employé/période
            QSqlQuery q2(QSqlDatabase::database());
            q2.prepare(QStringLiteral(
                "SELECT MAX(ID_CONGE) FROM CONGE_REQUEST WHERE EMPLOYEE_ID=:eid AND DATE_DEBUT=:d1 AND DATE_FIN=:d2"));
            q2.bindValue(QStringLiteral(":eid"), employeeId);
            q2.bindValue(QStringLiteral(":d1"), startDate);
            q2.bindValue(QStringLiteral(":d2"), endDate);
            if (q2.exec() && q2.next())
                return q2.value(0).toInt();
            return 0;
        }
    }

    // Fallback mémoire (si pas de BD)
    LeaveRequest r;
    r.id = nextId();
    r.employeeId = employeeId;
    r.employeeName = employeeName;
    r.startDate = startDate;
    r.endDate = endDate;
    r.reason = reason.trimmed();
    r.status = Pending;
    r.createdAt = QDate::currentDate();
    m_requests.push_back(r);
    emit leaveDataChanged();
    return r.id;
}

bool LeaveManager::setStatus(int requestId, Status status, QString *errorMessage)
{
    if (errorMessage)
        errorMessage->clear();

    if (leaveTableAvailable()) {
        const QString st = (status == Approved) ? QStringLiteral("APPROVED")
                          : (status == Rejected) ? QStringLiteral("REJECTED")
                          : QStringLiteral("PENDING");
        QSqlQuery q(QSqlDatabase::database());
        q.prepare(QStringLiteral("UPDATE CONGE_REQUEST SET STATUS=:st WHERE ID_CONGE=:id"));
        q.bindValue(QStringLiteral(":st"), st);
        q.bindValue(QStringLiteral(":id"), requestId);
        if (!q.exec()) {
            // Pareil: si la BD échoue, on tente un fallback mémoire.
            if (errorMessage)
                *errorMessage = q.lastError().text();
            // pas de return ici
        } else {
            reloadFromDatabase(nullptr);
            return true;
        }
    }

    for (LeaveRequest &r : m_requests) {
        if (r.id == requestId) {
            r.status = status;
            emit leaveDataChanged();
            return true;
        }
    }
    if (errorMessage)
        *errorMessage = tr("Demande introuvable.");
    return false;
}

bool LeaveManager::isOnApprovedLeave(int employeeId, const QDate &day) const
{
    if (employeeId <= 0 || !day.isValid())
        return false;
    for (const LeaveRequest &r : m_requests) {
        if (r.employeeId != employeeId)
            continue;
        if (r.status != Approved)
            continue;
        if (day >= r.startDate && day <= r.endDate)
            return true;
    }
    return false;
}

