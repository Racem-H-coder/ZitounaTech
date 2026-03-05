#include "stock.h"

#include <QSqlQuery>
#include <QVariant>

Stock::Stock(const QString &typeProduit, const QString &region, double quantite, const QDate &dateStock)
    : m_typeProduit(typeProduit)
    , m_region(region)
    , m_quantite(quantite)
    , m_dateStock(dateStock)
{
}

bool Stock::ajouter()
{
    QSqlQuery query;
    query.prepare(QStringLiteral("INSERT INTO STOCK (TYPE_PRODUIT, REGION, QUANTITE, DATE_STOCK) "
                                 "VALUES (:tp, :reg, :qty, :d)"));
    query.bindValue(QStringLiteral(":tp"), m_typeProduit);
    query.bindValue(QStringLiteral(":reg"), m_region);
    query.bindValue(QStringLiteral(":qty"), m_quantite);
    query.bindValue(QStringLiteral(":d"), m_dateStock);
    return query.exec();
}

bool Stock::supprimer(int idStock)
{
    QSqlQuery query;
    query.prepare(QStringLiteral("DELETE FROM STOCK WHERE ID_STOCK = :id"));
    query.bindValue(QStringLiteral(":id"), idStock);
    return query.exec();
}

bool Stock::modifier(int idStock)
{
    QSqlQuery query;
    query.prepare(QStringLiteral("UPDATE STOCK SET TYPE_PRODUIT=:tp, REGION=:reg, QUANTITE=:qty, DATE_STOCK=:d "
                                 "WHERE ID_STOCK=:id"));
    query.bindValue(QStringLiteral(":tp"), m_typeProduit);
    query.bindValue(QStringLiteral(":reg"), m_region);
    query.bindValue(QStringLiteral(":qty"), m_quantite);
    query.bindValue(QStringLiteral(":d"), m_dateStock);
    query.bindValue(QStringLiteral(":id"), idStock);
    return query.exec();
}
