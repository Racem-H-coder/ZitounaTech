#include "client.h"

#include <QDebug>
#include <QMessageBox>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

Client::Client()
    : id_client(0)
    , date_enregistrement(QDate::currentDate())
{
}

Client::Client(QString nom, QString prenom, QString email,
               QString telephone, QString cin, QString genre,
               QDate date_enregistrement, QString achats)
    : id_client(0)
    , nom(nom)
    , prenom(prenom)
    , email(email)
    , telephone(telephone)
    , cin(cin)
    , genre(genre)
    , date_enregistrement(date_enregistrement)
    , achats(achats)
{
}

bool Client::ajouter()
{
    if (nom.isEmpty() || prenom.isEmpty()) {
        QMessageBox::warning(nullptr, QStringLiteral("Champs manquants"),
                             QStringLiteral("Le nom et le prénom sont obligatoires."));
        return false;
    }

    QSqlQuery idQuery;
    if (!idQuery.exec(QStringLiteral("SELECT NVL(MAX(ID_CLIENT), 0) + 1 FROM CLIENT")) || !idQuery.next()) {
        qWarning() << "Erreur calcul prochain ID client:" << idQuery.lastError().text();
        return false;
    }
    const int nextId = idQuery.value(0).toInt();

    QSqlQuery query;
    query.prepare(
        QStringLiteral("INSERT INTO CLIENT "
                       "(ID_CLIENT, NOM, PRENOM, EMAIL, TELEPHONE, CIN, GENRE, DATE_ENREGISTREMENT, ACHATS) "
                       "VALUES (:id_client, :nom, :prenom, :email, :telephone, :cin, :genre, "
                       ":date_enregistrement, :achats)"));

    query.bindValue(QStringLiteral(":id_client"), nextId);
    query.bindValue(QStringLiteral(":nom"), nom);
    query.bindValue(QStringLiteral(":prenom"), prenom);
    query.bindValue(QStringLiteral(":email"), email);
    query.bindValue(QStringLiteral(":telephone"), telephone);
    query.bindValue(QStringLiteral(":cin"), cin);
    query.bindValue(QStringLiteral(":genre"), genre);
    query.bindValue(QStringLiteral(":date_enregistrement"), date_enregistrement);
    query.bindValue(QStringLiteral(":achats"), achats);

    if (query.exec())
        return true;

    qWarning() << "Erreur ajout client:" << query.lastError().text();
    return false;
}

QSqlQueryModel *Client::afficher()
{
    auto *model = new QSqlQueryModel();
    model->setQuery(
        QStringLiteral("SELECT ID_CLIENT, NOM, PRENOM, EMAIL, TELEPHONE, CIN, "
                       "TO_CHAR(DATE_ENREGISTREMENT, 'DD/MM/YYYY') AS DATE_ENREG, ACHATS "
                       "FROM CLIENT ORDER BY ID_CLIENT"));
    model->setHeaderData(0, Qt::Horizontal, QObject::tr("ID"));
    model->setHeaderData(1, Qt::Horizontal, QObject::tr("Nom"));
    model->setHeaderData(2, Qt::Horizontal, QObject::tr("Prénom"));
    model->setHeaderData(3, Qt::Horizontal, QObject::tr("Email"));
    model->setHeaderData(4, Qt::Horizontal, QObject::tr("Téléphone"));
    model->setHeaderData(5, Qt::Horizontal, QObject::tr("CIN"));
    model->setHeaderData(6, Qt::Horizontal, QObject::tr("Date d'enreg."));
    model->setHeaderData(7, Qt::Horizontal, QObject::tr("Achats"));
    return model;
}

bool Client::modifier(int id)
{
    QSqlQuery query;
    query.prepare(
        QStringLiteral("UPDATE CLIENT SET NOM = :nom, PRENOM = :prenom, EMAIL = :email, "
                       "TELEPHONE = :telephone, CIN = :cin, GENRE = :genre, DATE_ENREGISTREMENT = :date_enregistrement, "
                       "ACHATS = :achats WHERE ID_CLIENT = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    query.bindValue(QStringLiteral(":nom"), nom);
    query.bindValue(QStringLiteral(":prenom"), prenom);
    query.bindValue(QStringLiteral(":email"), email);
    query.bindValue(QStringLiteral(":telephone"), telephone);
    query.bindValue(QStringLiteral(":cin"), cin);
    query.bindValue(QStringLiteral(":genre"), genre);
    query.bindValue(QStringLiteral(":date_enregistrement"), date_enregistrement);
    query.bindValue(QStringLiteral(":achats"), achats);

    if (query.exec())
        return true;
    qWarning() << "Erreur modification client:" << query.lastError().text();
    return false;
}

bool Client::supprimer(int id)
{
    QSqlQuery query;
    query.prepare(QStringLiteral("DELETE FROM CLIENT WHERE ID_CLIENT = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (query.exec())
        return true;
    qWarning() << "Erreur suppression client:" << query.lastError().text();
    return false;
}

QSqlQueryModel *Client::rechercher(const QString &critere)
{
    auto *model = new QSqlQueryModel();
    QSqlQuery q;
    q.prepare(
        QStringLiteral("SELECT ID_CLIENT, NOM, PRENOM, EMAIL, TELEPHONE, CIN, "
                       "TO_CHAR(DATE_ENREGISTREMENT, 'DD/MM/YYYY') AS DATE_ENREG, ACHATS FROM CLIENT WHERE "
                       "(LOWER(NOM) LIKE LOWER(:pat) OR LOWER(PRENOM) LIKE LOWER(:pat) OR "
                       "LOWER(EMAIL) LIKE LOWER(:pat) OR TELEPHONE LIKE :pat2 OR CIN LIKE :pat2) "
                       "ORDER BY ID_CLIENT"));
    const QString pat = QStringLiteral("%") + critere + QStringLiteral("%");
    q.bindValue(QStringLiteral(":pat"), pat);
    q.bindValue(QStringLiteral(":pat2"), pat);
    if (!q.exec()) {
        qWarning() << "rechercher:" << q.lastError().text();
        model->setQuery(QStringLiteral("SELECT ID_CLIENT FROM CLIENT WHERE 1=0"));
        return model;
    }
    model->setQuery(std::move(q));
    model->setHeaderData(0, Qt::Horizontal, QObject::tr("ID"));
    model->setHeaderData(1, Qt::Horizontal, QObject::tr("Nom"));
    model->setHeaderData(2, Qt::Horizontal, QObject::tr("Prénom"));
    model->setHeaderData(3, Qt::Horizontal, QObject::tr("Email"));
    model->setHeaderData(4, Qt::Horizontal, QObject::tr("Téléphone"));
    model->setHeaderData(5, Qt::Horizontal, QObject::tr("CIN"));
    model->setHeaderData(6, Qt::Horizontal, QObject::tr("Date d'enreg."));
    model->setHeaderData(7, Qt::Horizontal, QObject::tr("Achats"));
    return model;
}

QSqlQueryModel *Client::trier(const QString &colonne, const QString &ordre)
{
    auto *model = new QSqlQueryModel();
    QString colonneTri = QStringLiteral("ID_CLIENT");
    if (colonne == QStringLiteral("nom"))
        colonneTri = QStringLiteral("NOM");
    else if (colonne == QStringLiteral("date"))
        colonneTri = QStringLiteral("DATE_ENREGISTREMENT");
    else if (colonne == QStringLiteral("email"))
        colonneTri = QStringLiteral("EMAIL");

    const QString ord = (ordre.toUpper() == QStringLiteral("DESC")) ? QStringLiteral("DESC") : QStringLiteral("ASC");
    const QString sql = QStringLiteral("SELECT ID_CLIENT, NOM, PRENOM, EMAIL, TELEPHONE, CIN, "
                                       "TO_CHAR(DATE_ENREGISTREMENT, 'DD/MM/YYYY') AS DATE_ENREG, ACHATS "
                                       "FROM CLIENT ORDER BY ")
        + colonneTri + QLatin1Char(' ') + ord;
    model->setQuery(sql);
    model->setHeaderData(0, Qt::Horizontal, QObject::tr("ID"));
    model->setHeaderData(1, Qt::Horizontal, QObject::tr("Nom"));
    model->setHeaderData(2, Qt::Horizontal, QObject::tr("Prénom"));
    model->setHeaderData(3, Qt::Horizontal, QObject::tr("Email"));
    model->setHeaderData(4, Qt::Horizontal, QObject::tr("Téléphone"));
    model->setHeaderData(5, Qt::Horizontal, QObject::tr("CIN"));
    model->setHeaderData(6, Qt::Horizontal, QObject::tr("Date d'enreg."));
    model->setHeaderData(7, Qt::Horizontal, QObject::tr("Achats"));
    return model;
}

bool Client::charger(int id)
{
    QSqlQuery query;
    query.prepare(QStringLiteral("SELECT * FROM CLIENT WHERE ID_CLIENT = :id"));
    query.bindValue(QStringLiteral(":id"), id);

    if (query.exec() && query.next()) {
        id_client = query.value(QStringLiteral("ID_CLIENT")).toInt();
        nom = query.value(QStringLiteral("NOM")).toString();
        prenom = query.value(QStringLiteral("PRENOM")).toString();
        email = query.value(QStringLiteral("EMAIL")).toString();
        telephone = query.value(QStringLiteral("TELEPHONE")).toString();
        cin = query.value(QStringLiteral("CIN")).toString();
        genre = query.value(QStringLiteral("GENRE")).toString();
        const QVariant dv = query.value(QStringLiteral("DATE_ENREGISTREMENT"));
        QDate d = dv.toDate();
        if (!d.isValid())
            d = dv.toDateTime().date();
        if (!d.isValid())
            d = QDate::fromString(dv.toString(), QStringLiteral("yyyy-MM-dd"));
        date_enregistrement = d.isValid() ? d : QDate::currentDate();
        achats = query.value(QStringLiteral("ACHATS")).toString();
        return true;
    }
    return false;
}
