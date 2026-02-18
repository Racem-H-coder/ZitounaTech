#include "equipement.h"

#include <QSqlQuery>
#include <QVariant>

Equipement::Equipement(const QString &reference,
                       const QString &modele,
                       const QString &marque,
                       const QString &typeEqui,
                       const QString &etat,
                       const QString &numeroSerie,
                       double cout,
                       const QDate &dateAjout)
    : m_reference(reference)
    , m_modele(modele)
    , m_marque(marque)
    , m_typeEqui(typeEqui)
    , m_etat(etat)
    , m_numeroSerie(numeroSerie)
    , m_cout(cout)
    , m_dateAjout(dateAjout)
{
}

bool Equipement::ajouter()
{
    QSqlQuery query;
    query.prepare(
        QStringLiteral("INSERT INTO EQUIPEMENT "
                       "(REFERENCE, MODELE, MARQUE, TYPE_EQUI, ETAT, NUMERO_SERIE, COUT, DATE_AJOUT) "
                       "VALUES (:ref, :modele, :marque, :type_e, :etat, :nserie, :cout, :daj)"));
    query.bindValue(QStringLiteral(":ref"), m_reference);
    query.bindValue(QStringLiteral(":modele"), m_modele);
    query.bindValue(QStringLiteral(":marque"), m_marque);
    query.bindValue(QStringLiteral(":type_e"), m_typeEqui);
    query.bindValue(QStringLiteral(":etat"), m_etat);
    query.bindValue(QStringLiteral(":nserie"), m_numeroSerie);
    query.bindValue(QStringLiteral(":cout"), m_cout);
    query.bindValue(QStringLiteral(":daj"), m_dateAjout);
    return query.exec();
}

bool Equipement::supprimer(int id)
{
    QSqlQuery query;
    query.prepare(QStringLiteral("DELETE FROM EQUIPEMENT WHERE ID = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    return query.exec();
}

bool Equipement::modifier(int id)
{
    QSqlQuery query;
    query.prepare(
        QStringLiteral("UPDATE EQUIPEMENT SET "
                       "REFERENCE=:ref, MODELE=:modele, MARQUE=:marque, TYPE_EQUI=:type_e, "
                       "ETAT=:etat, NUMERO_SERIE=:nserie, COUT=:cout, DATE_AJOUT=:daj "
                       "WHERE ID=:id"));
    query.bindValue(QStringLiteral(":ref"), m_reference);
    query.bindValue(QStringLiteral(":modele"), m_modele);
    query.bindValue(QStringLiteral(":marque"), m_marque);
    query.bindValue(QStringLiteral(":type_e"), m_typeEqui);
    query.bindValue(QStringLiteral(":etat"), m_etat);
    query.bindValue(QStringLiteral(":nserie"), m_numeroSerie);
    query.bindValue(QStringLiteral(":cout"), m_cout);
    query.bindValue(QStringLiteral(":daj"), m_dateAjout);
    query.bindValue(QStringLiteral(":id"), id);
    return query.exec();
}
