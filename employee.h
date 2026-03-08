#ifndef EMPLOYEE_H
#define EMPLOYEE_H

#include <QString>
#include <QSqlQuery>
#include <QDate>

class Employee
{
private:
    int id;
    QString nom, email, telephone, depart, poste, mot_de_passe;
    double salaire;
    QDate date_debut;

public:
    Employee(){}
    Employee(QString nom, QString email, QString telephone,
             QString depart, QString poste,
             double salaire, QDate date_debut,
             QString mot_de_passe);

    bool ajouter();
    bool supprimer(int id);
    bool modifier(int id);
    QSqlQuery afficher();
    static QString hashPassword(const QString &plainPassword);
    static bool hasAnyEmployee(QString *errorMessage = nullptr);
    static bool authenticate(const QString &identifier, const QString &password, QString *errorMessage = nullptr);
};

#endif

