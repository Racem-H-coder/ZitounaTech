#ifndef FACTURE_H
#define FACTURE_H

#include <QDate>
#include <QDateTime>
#include <QSqlQueryModel>
#include <QString>

class Facture
{
public:
    Facture();

    Facture(int id_facture,
            const QString &matricule_fiscal,
            const QString &nom_entreprise,
            const QString &nom_raison_sociale,
            const QString &type_facture,
            const QDate &date_facturation,
            double prix_unitaire,
            int quantite,
            const QString &description,
            int id_client,
            int id_fournisseur,
            const QString &signed_by = QString(),
            const QString &signature_draw = QString());

    bool ajouter();
    bool modifier();
    static bool supprimer(int id_facture);
    static QSqlQueryModel *afficher();
    static QString dernierErreur();
    static bool ensureSignatureColumns();

    int getIdFacture() const;
    void setIdFacture(int id_facture);
    QString getSignatureElec() const;

private:
    static QString buildSignatureToken(int id_facture,
                                       const QString &matricule_fiscal,
                                       const QString &nom_entreprise,
                                       const QString &nom_raison_sociale,
                                       const QString &type_facture,
                                       const QDate &date_facturation,
                                       double prix_unitaire,
                                       int quantite,
                                       const QString &description,
                                       int id_client,
                                       int id_fournisseur,
                                       const QString &signed_by,
                                       const QString &signature_draw,
                                       const QDateTime &signed_at);
    static QString m_dernier_erreur;

    int m_id_facture;
    QString m_matricule_fiscal;
    QString m_nom_entreprise;
    QString m_nom_raison_sociale;
    QString m_type_facture;
    QDate m_date_facturation;
    double m_prix_unitaire;
    int m_quantite;
    QString m_description;
    int m_id_client;
    int m_id_fournisseur;
    QString m_signature_elec;
    QDateTime m_signed_at;
    QString m_signed_by;
    QString m_signature_draw;
};

#endif // FACTURE_H
