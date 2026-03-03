#ifndef FOURNISSEUR_H
#define FOURNISSEUR_H

#include <QDate>
#include <QSqlQueryModel>
#include <QString>

class Fournisseur
{
public:
    Fournisseur();

    Fournisseur(int idFournisseur,
                const QString &nom,
                const QString &region,
                const QString &email,
                const QString &nature,
                const QDate &dateAjout,
                const QString &status,
                const QString &telephone);

    bool ajouter();
    bool modifier();
    static bool supprimer(int idFournisseur);
    static int prochainId();
    static QSqlQueryModel *afficher();
    static QString dernierErreur();

private:
    static QString m_dernierErreur;

    int m_idFournisseur;
    QString m_nom;
    QString m_region;
    QString m_email;
    QString m_nature;
    QDate m_dateAjout;
    QString m_status;
    QString m_telephone;
};

#endif // FOURNISSEUR_H
