#ifndef EMPLOYEEMANAGER_H
#define EMPLOYEEMANAGER_H

#include <QDate>
#include <QString>

struct EmployeeData
{
    int id = -1;
    QString nom;
    QString prenom;
    QString poste;
    QDate dateEmbauche;
};

class EmployeeManager
{
public:
    bool addEmployee(const EmployeeData &data,
                     const QString &pinCode,
                     int fingerSlotId = -1,
                     QString *errorOut = nullptr) const;
    int getFingerprintSlotByEmployeeId(int employeeId, QString *errorOut = nullptr) const;
    bool getEmployeeByFingerSlot(int slotId, EmployeeData *employeeOut, QString *errorOut = nullptr) const;
    bool deleteEmployeeFingerprint(int employeeId, QString *errorOut = nullptr) const;
};

#endif // EMPLOYEEMANAGER_H
