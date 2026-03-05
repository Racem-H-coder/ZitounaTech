#ifndef STOCK_H
#define STOCK_H

#include <QDate>
#include <QString>

class Stock
{
public:
    Stock() = default;
    Stock(const QString &typeProduit, const QString &region, double quantite, const QDate &dateStock);

    bool ajouter();
    bool supprimer(int idStock);
    bool modifier(int idStock);

private:
    QString m_typeProduit;
    QString m_region;
    double m_quantite = 0.;
    QDate m_dateStock;
};

#endif
