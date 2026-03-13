#ifndef LEAVEMANAGER_H
#define LEAVEMANAGER_H

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>

// Gestion simple des congés:
// - stockage en mémoire (fonctionnel même si la BD n'a pas de table dédiée)
// - API claire pour le planning: est-ce que l'employé est indisponible ce jour ?
class LeaveManager : public QObject
{
    Q_OBJECT
public:
    enum Status { Pending, Approved, Rejected };
    Q_ENUM(Status)

    struct LeaveRequest {
        int id = 0;
        int employeeId = 0;
        QString employeeName;
        QDate startDate;
        QDate endDate;
        QString reason;
        Status status = Pending;
        QDate createdAt;
    };

    explicit LeaveManager(QObject *parent = nullptr);

    const QList<LeaveRequest> &requests() const { return m_requests; }

    // Recharge depuis la base si la table CONGE_REQUEST existe.
    // Si la BD n'est pas disponible, garde les données en mémoire.
    bool reloadFromDatabase(QString *errorMessage = nullptr);

    int addRequest(int employeeId,
                   const QString &employeeName,
                   const QDate &startDate,
                   const QDate &endDate,
                   const QString &reason,
                   QString *errorMessage = nullptr);

    bool setStatus(int requestId, Status status, QString *errorMessage = nullptr);

    bool isOnApprovedLeave(int employeeId, const QDate &day) const;

signals:
    void leaveDataChanged();

private:
    int nextId();
    bool dbAvailable() const;
    bool leaveTableAvailable() const;
    QList<LeaveRequest> m_requests;
    int m_lastId = 0;
};

#endif // LEAVEMANAGER_H

