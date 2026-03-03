#include "fournisseur.h"

#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>

namespace {
QString colonneNatureFournisseur()
{
    static QString cachedColumn;
    if (!cachedColumn.isEmpty()) {
        return cachedColumn;
    }

    QSqlQuery query;
    if (query.exec(QStringLiteral(
            "SELECT COLUMN_NAME "
            "FROM USER_TAB_COLUMNS "
            "WHERE TABLE_NAME = 'FOURNISSEUR' "
            "AND COLUMN_NAME IN ('NATURE', 'TYPE_FOURNISSEUR', 'TYPE')"))) {
        bool hasNature = false;
        bool hasTypeFournisseur = false;
        bool hasType = false;
        while (query.next()) {
            const QString col = query.value(0).toString().trimmed().toUpper();
            if (col == QStringLiteral("NATURE")) {
                hasNature = true;
            } else if (col == QStringLiteral("TYPE_FOURNISSEUR")) {
                hasTypeFournisseur = true;
            } else if (col == QStringLiteral("TYPE")) {
                hasType = true;
            }
        }
        if (hasNature) {
            cachedColumn = QStringLiteral("NATURE");
        } else if (hasTypeFournisseur) {
            cachedColumn = QStringLiteral("TYPE_FOURNISSEUR");
        } else if (hasType) {
            cachedColumn = QStringLiteral("TYPE");
        }
    }

    if (cachedColumn.isEmpty()) {
        cachedColumn = QStringLiteral("NATURE");
    }
    return cachedColumn;
}
} // namespace

QString Fournisseur::m_dernierErreur;

Fournisseur::Fournisseur()
    : m_idFournisseur(0)
{
}

Fournisseur::Fournisseur(int idFournisseur,
                         const QString &nom,
                         const QString &region,
                         const QString &email,
                         const QString &nature,
                         const QDate &dateAjout,
                         const QString &status,
                         const QString &telephone)
    : m_idFournisseur(idFournisseur)
    , m_nom(nom)
    , m_region(region)
    , m_email(email)
    , m_nature(nature)
    , m_dateAjout(dateAjout)
    , m_status(status)
    , m_telephone(telephone)
{
}

bool Fournisseur::ajouter()
{
    QSqlQuery query;
    query.prepare(
        QStringLiteral("INSERT INTO FOURNISSEUR ("
                       "ID_FOURNISSEUR, NOM, REGION, EMAIL, %1, DATE_AJOUT, STATUS_FOURNISSEUR, TELEPHONE) "
                       "VALUES (:id, :nom, :region, :email, :nature, :date_ajout, :status, :telephone)")
            .arg(colonneNatureFournisseur()));
    query.bindValue(QStringLiteral(":id"), m_idFournisseur);
    query.bindValue(QStringLiteral(":nom"), m_nom);
    query.bindValue(QStringLiteral(":region"), m_region);
    query.bindValue(QStringLiteral(":email"), m_email);
    query.bindValue(QStringLiteral(":nature"), m_nature);
    query.bindValue(QStringLiteral(":date_ajout"), m_dateAjout);
    query.bindValue(QStringLiteral(":status"), m_status);
    query.bindValue(QStringLiteral(":telephone"), m_telephone);

    const bool ok = query.exec();
    if (!ok) {
        m_dernierErreur = query.lastError().text();
        qWarning() << "Erreur ajout fournisseur:" << m_dernierErreur;
    } else {
        m_dernierErreur.clear();
    }
    return ok;
}

bool Fournisseur::modifier()
{
    QSqlQuery query;
    query.prepare(
        QStringLiteral("UPDATE FOURNISSEUR SET "
                       "NOM = :nom, REGION = :region, EMAIL = :email, %1 = :nature, "
                       "DATE_AJOUT = :date_ajout, STATUS_FOURNISSEUR = :status, TELEPHONE = :telephone "
                       "WHERE ID_FOURNISSEUR = :id")
            .arg(colonneNatureFournisseur()));
    query.bindValue(QStringLiteral(":id"), m_idFournisseur);
    query.bindValue(QStringLiteral(":nom"), m_nom);
    query.bindValue(QStringLiteral(":region"), m_region);
    query.bindValue(QStringLiteral(":email"), m_email);
    query.bindValue(QStringLiteral(":nature"), m_nature);
    query.bindValue(QStringLiteral(":date_ajout"), m_dateAjout);
    query.bindValue(QStringLiteral(":status"), m_status);
    query.bindValue(QStringLiteral(":telephone"), m_telephone);

    const bool ok = query.exec();
    if (!ok) {
        m_dernierErreur = query.lastError().text();
        qWarning() << "Erreur modification fournisseur:" << m_dernierErreur;
    } else {
        m_dernierErreur.clear();
    }
    return ok;
}

bool Fournisseur::supprimer(int idFournisseur)
{
    QSqlQuery query;
    query.prepare(QStringLiteral("DELETE FROM FOURNISSEUR WHERE ID_FOURNISSEUR = :id"));
    query.bindValue(QStringLiteral(":id"), idFournisseur);

    const bool ok = query.exec();
    if (!ok) {
        m_dernierErreur = query.lastError().text();
        qWarning() << "Erreur suppression fournisseur:" << m_dernierErreur;
    } else {
        m_dernierErreur.clear();
    }
    return ok;
}

int Fournisseur::prochainId()
{
    QSqlQuery query;
    if (!query.exec(QStringLiteral("SELECT NVL(MAX(ID_FOURNISSEUR), 0) + 1 FROM FOURNISSEUR"))) {
        m_dernierErreur = query.lastError().text();
        qWarning() << "Erreur calcul prochain id fournisseur:" << m_dernierErreur;
        return -1;
    }
    if (query.next()) {
        m_dernierErreur.clear();
        return query.value(0).toInt();
    }
    m_dernierErreur = QStringLiteral("Impossible de lire le prochain id fournisseur.");
    return -1;
}

QSqlQueryModel *Fournisseur::afficher()
{
    auto *model = new QSqlQueryModel();
    model->setQuery(
        QStringLiteral("SELECT "
                       "ID_FOURNISSEUR AS ID_FOURNISSEUR, "
                       "NOM AS NOM, REGION AS REGION, EMAIL AS EMAIL, %1 AS NATURE, "
                       "TO_CHAR(DATE_AJOUT, 'YYYY-MM-DD') AS DATE_AJOUT, "
                       "STATUS_FOURNISSEUR AS STATUS_FOURNISSEUR, TELEPHONE AS TELEPHONE "
                       "FROM FOURNISSEUR ORDER BY ID_FOURNISSEUR ASC")
            .arg(colonneNatureFournisseur()));
    if (model->lastError().isValid()) {
        m_dernierErreur = model->lastError().text();
        qWarning() << "Erreur affichage fournisseur:" << m_dernierErreur;
    } else {
        m_dernierErreur.clear();
    }
    model->setHeaderData(0, Qt::Horizontal, QObject::tr("ID"));
    model->setHeaderData(1, Qt::Horizontal, QObject::tr("Nom"));
    model->setHeaderData(2, Qt::Horizontal, QObject::tr("Region"));
    model->setHeaderData(3, Qt::Horizontal, QObject::tr("Email"));
    model->setHeaderData(4, Qt::Horizontal, QObject::tr("Nature"));
    model->setHeaderData(5, Qt::Horizontal, QObject::tr("Date ajout"));
    model->setHeaderData(6, Qt::Horizontal, QObject::tr("Status"));
    model->setHeaderData(7, Qt::Horizontal, QObject::tr("Telephone"));
    return model;
}

QString Fournisseur::dernierErreur()
{
    return m_dernierErreur;
}
