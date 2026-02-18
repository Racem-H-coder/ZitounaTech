#ifndef EQUIPEMENT_H
#define EQUIPEMENT_H

#include <QDate>
#include <QString>
#include <QSqlQuery>

class Equipement
{
public:
    Equipement() = default;
    Equipement(const QString &reference,
               const QString &modele,
               const QString &marque,
               const QString &typeEqui,
               const QString &etat,
               const QString &numeroSerie,
               double cout,
               const QDate &dateAjout);

    bool ajouter();
    bool supprimer(int id);
    bool modifier(int id);

private:
    QString m_reference;
    QString m_modele;
    QString m_marque;
    QString m_typeEqui;
    QString m_etat;
    QString m_numeroSerie;
    double m_cout = 0.;
    QDate m_dateAjout;
};

#endif
