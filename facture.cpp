#include "facture.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QString>
#include <QVariant>

QString Facture::m_dernier_erreur;

namespace {
// ODBC Oracle (QODBC) : QVariant() NULL sur NUMBER et certains types -> ORA-00932 (BINARY).
// On integre les nombres / NULL en SQL et on lie seulement des chaines + dates formatees en texte.
QString sqlNombreOuNull(int v)
{
    return v > 0 ? QString::number(v) : QStringLiteral("NULL");
}

QString normalizeSigner(const QString &signedBy)
{
    const QString value = signedBy.trimmed();
    return value.isEmpty() ? QStringLiteral("UtilisateurApp") : value.left(100);
}

QString normalizeSignatureDraw(const QString &signatureDraw)
{
    return signatureDraw.trimmed().left(4000);
}
} // namespace

Facture::Facture()
    : m_id_facture(0)
    , m_prix_unitaire(0.0)
    , m_quantite(0)
    , m_id_client(0)
    , m_id_fournisseur(0)
    , m_signed_by(QStringLiteral("UtilisateurApp"))
    , m_signature_draw(QString())
{
}

Facture::Facture(int id_facture,
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
                 const QString &signature_draw)
    : m_id_facture(id_facture)
    , m_matricule_fiscal(matricule_fiscal)
    , m_nom_entreprise(nom_entreprise)
    , m_nom_raison_sociale(nom_raison_sociale)
    , m_type_facture(type_facture)
    , m_date_facturation(date_facturation)
    , m_prix_unitaire(prix_unitaire)
    , m_quantite(quantite)
    , m_description(description)
    , m_id_client(id_client)
    , m_id_fournisseur(id_fournisseur)
    , m_signed_by(normalizeSigner(signed_by))
    , m_signature_draw(normalizeSignatureDraw(signature_draw))
{
}

bool Facture::ensureSignatureColumns()
{
    QSqlQuery readColumns;
    if (!readColumns.exec(QStringLiteral(
            "SELECT COLUMN_NAME FROM USER_TAB_COLUMNS WHERE TABLE_NAME = 'FACTURE'"))) {
        m_dernier_erreur = QStringLiteral("Lecture schema FACTURE impossible: ")
                           + readColumns.lastError().text();
        return false;
    }

    QSet<QString> existingColumns;
    while (readColumns.next()) {
        existingColumns.insert(readColumns.value(0).toString().trimmed().toUpper());
    }

    auto ensureColumn = [&](const QString &columnName, const QString &columnDef) -> bool {
        if (existingColumns.contains(columnName)) {
            return true;
        }

        QSqlQuery alter;
        if (alter.exec(QStringLiteral("ALTER TABLE FACTURE ADD (") + columnDef + QStringLiteral(")"))) {
            existingColumns.insert(columnName);
            return true;
        }

        const QString err = alter.lastError().text();
        if (err.contains(QStringLiteral("ORA-01430"), Qt::CaseInsensitive)
            || err.contains(QStringLiteral("already exists"), Qt::CaseInsensitive)) {
            existingColumns.insert(columnName);
            return true;
        }

        m_dernier_erreur = QStringLiteral("Impossible d'ajouter %1: %2").arg(columnName, err);
        return false;
    };

    if (!ensureColumn(QStringLiteral("SIGNATURE_ELEC"), QStringLiteral("SIGNATURE_ELEC VARCHAR2(64)"))) {
        return false;
    }
    if (!ensureColumn(QStringLiteral("SIGNED_AT"), QStringLiteral("SIGNED_AT DATE"))) {
        return false;
    }
    if (!ensureColumn(QStringLiteral("SIGNED_BY"), QStringLiteral("SIGNED_BY VARCHAR2(100)"))) {
        return false;
    }
    if (!ensureColumn(QStringLiteral("SIGNATURE_DRAW"), QStringLiteral("SIGNATURE_DRAW VARCHAR2(4000)"))) {
        return false;
    }

    m_dernier_erreur.clear();
    return true;
}

QString Facture::buildSignatureToken(int id_facture,
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
                                     const QDateTime &signed_at)
{
    const QString drawHash = QString::fromUtf8(
        QCryptographicHash::hash(signature_draw.toUtf8(), QCryptographicHash::Sha256).toHex()).toUpper();
    const QString payload = QStringLiteral(
                                "%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14")
                                .arg(QString::number(id_facture),
                                     matricule_fiscal.trimmed(),
                                     nom_entreprise.trimmed(),
                                     nom_raison_sociale.trimmed(),
                                     type_facture.trimmed(),
                                     date_facturation.toString(QStringLiteral("yyyy-MM-dd")),
                                     QString::number(prix_unitaire, 'f', 2),
                                     QString::number(quantite),
                                     description.trimmed(),
                                     QString::number(id_client),
                                     QString::number(id_fournisseur),
                                     normalizeSigner(signed_by),
                                     drawHash,
                                     signed_at.toUTC().toString(Qt::ISODate));
    return QString::fromUtf8(QCryptographicHash::hash(payload.toUtf8(), QCryptographicHash::Sha256).toHex()).toUpper();
}

bool Facture::ajouter()
{
    m_signature_draw = normalizeSignatureDraw(m_signature_draw);
    if (m_signature_draw.trimmed().isEmpty()) {
        m_dernier_erreur = QStringLiteral("Signature manuscrite obligatoire.");
        return false;
    }

    if (!ensureSignatureColumns()) {
        if (m_dernier_erreur.isEmpty()) {
            m_dernier_erreur = QStringLiteral("Colonnes de signature indisponibles.");
        }
        qWarning() << "Erreur schema facture:" << m_dernier_erreur;
        return false;
    }

    // ID auto : MAX+1. PRIX/QUANTITE/NULL en litteraux SQL pour eviter ORA-00932 avec QODBC.
    const QString prixLit = QString::number(m_prix_unitaire, 'f', 2);
    const QString qteLit = QString::number(m_quantite);
    const QString idCLit = sqlNombreOuNull(m_id_client);
    const QString idFLit = sqlNombreOuNull(m_id_fournisseur);
    const QString signedBy = normalizeSigner(m_signed_by);
    const QDateTime signedAt = QDateTime::currentDateTime();

    QSqlQuery qid;
    if (!(qid.exec(QStringLiteral("SELECT NVL(MAX(ID_FACTURE), 0) + 1 FROM FACTURE")) && qid.next())) {
        m_dernier_erreur = qid.lastError().text();
        qWarning() << "Erreur calcul id facture:" << m_dernier_erreur;
        return false;
    }
    const int nextId = qid.value(0).toInt();
    const QString signature = buildSignatureToken(nextId,
                                                  m_matricule_fiscal,
                                                  m_nom_entreprise,
                                                  m_nom_raison_sociale,
                                                  m_type_facture,
                                                  m_date_facturation,
                                                  m_prix_unitaire,
                                                  m_quantite,
                                                  m_description,
                                                  m_id_client,
                                                  m_id_fournisseur,
                                                  signedBy,
                                                  m_signature_draw,
                                                  signedAt);

    const QString sql = QStringLiteral(
                            "INSERT INTO FACTURE ("
                            "ID_FACTURE, MATRICULE_FISCAL, NOM_ENTREPRISE, NOM_RAISON_SOCIALE, "
                            "TYPE_FACTURE, DATE_FACTURATION, PRIX_UNITAIRE, QUANTITE, DESCRIPTION, "
                            "ID_CLIENT, ID_FOURNISSEUR, SIGNATURE_ELEC, SIGNED_AT, SIGNED_BY, SIGNATURE_DRAW) VALUES ("
                            "%1, :matricule_fiscal, :nom_entreprise, :nom_raison_sociale, "
                            ":type_facture, TO_DATE(:date_facturation, 'YYYY-MM-DD'), "
                            "%2, %3, :description, %4, %5, :signature, "
                            "TO_DATE(:signed_at, 'YYYY-MM-DD HH24:MI:SS'), :signed_by, :signature_draw)")
                            .arg(QString::number(nextId), prixLit, qteLit, idCLit, idFLit);

    QSqlQuery query;
    query.prepare(sql);

    query.bindValue(QStringLiteral(":matricule_fiscal"), m_matricule_fiscal);
    query.bindValue(QStringLiteral(":nom_entreprise"), m_nom_entreprise);
    query.bindValue(QStringLiteral(":nom_raison_sociale"), m_nom_raison_sociale);
    query.bindValue(QStringLiteral(":type_facture"), m_type_facture);
    query.bindValue(QStringLiteral(":date_facturation"),
                    m_date_facturation.toString(QStringLiteral("yyyy-MM-dd")));
    query.bindValue(QStringLiteral(":description"), m_description);
    query.bindValue(QStringLiteral(":signature"), signature);
    query.bindValue(QStringLiteral(":signed_at"),
                    signedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    query.bindValue(QStringLiteral(":signed_by"), signedBy);
    query.bindValue(QStringLiteral(":signature_draw"), m_signature_draw);

    if (!query.exec()) {
        m_dernier_erreur = query.lastError().text();
        qWarning() << "Erreur ajout facture:" << m_dernier_erreur;
        return false;
    }

    m_id_facture = nextId;
    m_signature_elec = signature;
    m_signed_at = signedAt;
    m_signed_by = signedBy;

    m_dernier_erreur.clear();
    return true;
}

bool Facture::modifier()
{
    m_signature_draw = normalizeSignatureDraw(m_signature_draw);
    if (m_signature_draw.trimmed().isEmpty()) {
        m_dernier_erreur = QStringLiteral("Signature manuscrite obligatoire.");
        return false;
    }

    if (!ensureSignatureColumns()) {
        if (m_dernier_erreur.isEmpty()) {
            m_dernier_erreur = QStringLiteral("Colonnes de signature indisponibles.");
        }
        qWarning() << "Erreur schema facture:" << m_dernier_erreur;
        return false;
    }

    const QString prixLit = QString::number(m_prix_unitaire, 'f', 2);
    const QString qteLit = QString::number(m_quantite);
    const QString idCLit = sqlNombreOuNull(m_id_client);
    const QString idFLit = sqlNombreOuNull(m_id_fournisseur);
    const QString idFactLit = QString::number(m_id_facture);
    const QString signedBy = normalizeSigner(m_signed_by);
    const QDateTime signedAt = QDateTime::currentDateTime();
    const QString signature = buildSignatureToken(m_id_facture,
                                                  m_matricule_fiscal,
                                                  m_nom_entreprise,
                                                  m_nom_raison_sociale,
                                                  m_type_facture,
                                                  m_date_facturation,
                                                  m_prix_unitaire,
                                                  m_quantite,
                                                  m_description,
                                                  m_id_client,
                                                  m_id_fournisseur,
                                                  signedBy,
                                                  m_signature_draw,
                                                  signedAt);

    const QString sql = QStringLiteral(
                            "UPDATE FACTURE SET "
                            "MATRICULE_FISCAL = :matricule_fiscal, "
                            "NOM_ENTREPRISE = :nom_entreprise, "
                            "NOM_RAISON_SOCIALE = :nom_raison_sociale, "
                            "TYPE_FACTURE = :type_facture, "
                            "DATE_FACTURATION = TO_DATE(:date_facturation, 'YYYY-MM-DD'), "
                            "PRIX_UNITAIRE = %1, "
                            "QUANTITE = %2, "
                            "DESCRIPTION = :description, "
                            "ID_CLIENT = %3, "
                            "ID_FOURNISSEUR = %4, "
                            "SIGNATURE_ELEC = :signature, "
                            "SIGNED_AT = TO_DATE(:signed_at, 'YYYY-MM-DD HH24:MI:SS'), "
                            "SIGNED_BY = :signed_by, "
                            "SIGNATURE_DRAW = :signature_draw "
                            "WHERE ID_FACTURE = %5")
                            .arg(prixLit)
                            .arg(qteLit)
                            .arg(idCLit)
                            .arg(idFLit)
                            .arg(idFactLit);

    QSqlQuery query;
    query.prepare(sql);

    query.bindValue(QStringLiteral(":matricule_fiscal"), m_matricule_fiscal);
    query.bindValue(QStringLiteral(":nom_entreprise"), m_nom_entreprise);
    query.bindValue(QStringLiteral(":nom_raison_sociale"), m_nom_raison_sociale);
    query.bindValue(QStringLiteral(":type_facture"), m_type_facture);
    query.bindValue(QStringLiteral(":date_facturation"),
                    m_date_facturation.toString(QStringLiteral("yyyy-MM-dd")));
    query.bindValue(QStringLiteral(":description"), m_description);
    query.bindValue(QStringLiteral(":signature"), signature);
    query.bindValue(QStringLiteral(":signed_at"),
                    signedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    query.bindValue(QStringLiteral(":signed_by"), signedBy);
    query.bindValue(QStringLiteral(":signature_draw"), m_signature_draw);

    const bool ok = query.exec();
    if (!ok) {
        m_dernier_erreur = query.lastError().text();
        qWarning() << "Erreur modification facture:" << m_dernier_erreur;
    } else {
        m_signature_elec = signature;
        m_signed_at = signedAt;
        m_signed_by = signedBy;
        m_dernier_erreur.clear();
    }
    return ok;
}

bool Facture::supprimer(int id_facture)
{
    QSqlQuery query;
    query.prepare(QStringLiteral("DELETE FROM FACTURE WHERE ID_FACTURE = :id_facture"));
    query.bindValue(QStringLiteral(":id_facture"), id_facture);
    const bool ok = query.exec();
    if (!ok) {
        m_dernier_erreur = query.lastError().text();
        qWarning() << "Erreur suppression facture:" << m_dernier_erreur;
    } else {
        m_dernier_erreur.clear();
    }
    return ok;
}

QSqlQueryModel *Facture::afficher()
{
    auto *model = new QSqlQueryModel();
    const bool signatureReady = ensureSignatureColumns();
    const QString sql = signatureReady
                            ? QStringLiteral("SELECT "
                                             "ID_FACTURE, MATRICULE_FISCAL, NOM_ENTREPRISE, NOM_RAISON_SOCIALE, "
                                             "TYPE_FACTURE, DATE_FACTURATION, PRIX_UNITAIRE, QUANTITE, DESCRIPTION, "
                                             "ID_CLIENT, ID_FOURNISSEUR, SIGNATURE_ELEC, SIGNED_AT, SIGNED_BY, SIGNATURE_DRAW "
                                             "FROM FACTURE ORDER BY ID_FACTURE")
                            : QStringLiteral("SELECT "
                                             "ID_FACTURE, MATRICULE_FISCAL, NOM_ENTREPRISE, NOM_RAISON_SOCIALE, "
                                             "TYPE_FACTURE, DATE_FACTURATION, PRIX_UNITAIRE, QUANTITE, DESCRIPTION, "
                                             "ID_CLIENT, ID_FOURNISSEUR, '' AS SIGNATURE_ELEC, "
                                             "NULL AS SIGNED_AT, '' AS SIGNED_BY, '' AS SIGNATURE_DRAW "
                                             "FROM FACTURE ORDER BY ID_FACTURE");
    model->setQuery(sql);
    if (model->lastError().isValid()) {
        m_dernier_erreur = model->lastError().text();
    }
    model->setHeaderData(0, Qt::Horizontal, QObject::tr("ID Facture"));
    model->setHeaderData(1, Qt::Horizontal, QObject::tr("Matricule Fiscal"));
    model->setHeaderData(2, Qt::Horizontal, QObject::tr("Nom Entreprise"));
    model->setHeaderData(3, Qt::Horizontal, QObject::tr("Raison Sociale"));
    model->setHeaderData(4, Qt::Horizontal, QObject::tr("Type Facture"));
    model->setHeaderData(5, Qt::Horizontal, QObject::tr("Date Facturation"));
    model->setHeaderData(6, Qt::Horizontal, QObject::tr("Prix Unitaire"));
    model->setHeaderData(7, Qt::Horizontal, QObject::tr("Quantite"));
    model->setHeaderData(8, Qt::Horizontal, QObject::tr("Description"));
    model->setHeaderData(9, Qt::Horizontal, QObject::tr("ID Client"));
    model->setHeaderData(10, Qt::Horizontal, QObject::tr("ID Fournisseur"));
    model->setHeaderData(11, Qt::Horizontal, QObject::tr("Signature"));
    model->setHeaderData(12, Qt::Horizontal, QObject::tr("Date signature"));
    model->setHeaderData(13, Qt::Horizontal, QObject::tr("Signe par"));
    model->setHeaderData(14, Qt::Horizontal, QObject::tr("Signature dessinee"));
    return model;
}

int Facture::getIdFacture() const
{
    return m_id_facture;
}

void Facture::setIdFacture(int id_facture)
{
    m_id_facture = id_facture;
}

QString Facture::getSignatureElec() const
{
    return m_signature_elec;
}

QString Facture::dernierErreur()
{
    return m_dernier_erreur;
}
