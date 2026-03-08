#include "employee.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QSqlQueryModel>
#include <QSqlError>
#include <QVariant>

namespace {

bool isSha256Hex(const QString &value)
{
    static const QRegularExpression re(QStringLiteral("^[A-Fa-f0-9]{64}$"));
    return re.match(value.trimmed()).hasMatch();
}

QString ensureHashedPassword(const QString &password)
{
    const QString trimmed = password.trimmed();
    if (isSha256Hex(trimmed)) {
        return trimmed.toUpper();
    }
    return QString::fromUtf8(
               QCryptographicHash::hash(trimmed.toUtf8(), QCryptographicHash::Sha256).toHex())
        .toUpper();
}

QString ensureHashedPasswordLocal8Bit(const QString &password)
{
    const QString trimmed = password.trimmed();
    if (isSha256Hex(trimmed)) {
        return trimmed.toUpper();
    }
    return QString::fromUtf8(
               QCryptographicHash::hash(trimmed.toLocal8Bit(), QCryptographicHash::Sha256).toHex())
        .toUpper();
}

} // namespace

Employee::Employee(QString nom, QString email, QString telephone,
                   QString depart, QString poste,
                   double salaire, QDate date_debut,
                   QString mot_de_passe)
{
    this->nom = nom;
    this->email = email;
    this->telephone = telephone;
    this->depart = depart;
    this->poste = poste;
    this->salaire = salaire;
    this->date_debut = date_debut;
    this->mot_de_passe = mot_de_passe;
}


bool Employee::ajouter()
{
    QSqlQuery query;

    // ID est renseigné par le trigger Oracle (EMPLOYEE_TRIGGER + EMPLOYEE_SEQ).
    query.prepare(
        QStringLiteral("INSERT INTO EMPLOYEE "
                       "(NOM, EMAIL, TELEPHONE, DEPART, POSTE, SALAIRE, DATE_DEBUT, MOT_DE_PASSE) "
                       "VALUES (:nom, :email, :telephone, :depart, :poste, :salaire, :date_debut, :mot)"));

    query.bindValue(QStringLiteral(":nom"), nom);
    query.bindValue(QStringLiteral(":email"), email);
    query.bindValue(QStringLiteral(":telephone"), telephone);
    query.bindValue(QStringLiteral(":depart"), depart);
    query.bindValue(QStringLiteral(":poste"), poste);
    query.bindValue(QStringLiteral(":salaire"), salaire);
    query.bindValue(QStringLiteral(":date_debut"), date_debut);
    query.bindValue(QStringLiteral(":mot"), ensureHashedPassword(mot_de_passe));

    return query.exec();
}

bool Employee::supprimer(int id)
{
    QSqlQuery query;
    query.prepare("DELETE FROM EMPLOYEE WHERE ID = :id");
    query.bindValue(":id", id);

    return query.exec();
}

bool Employee::modifier(int id)
{
    QSqlQuery query;

    if (mot_de_passe.isEmpty()) {
        query.prepare("UPDATE EMPLOYEE SET "
                      "NOM=:nom, EMAIL=:email, TELEPHONE=:telephone, "
                      "DEPART=:depart, POSTE=:poste, SALAIRE=:salaire, "
                      "DATE_DEBUT=:date_debut "
                      "WHERE ID=:id");
    } else {
        query.prepare("UPDATE EMPLOYEE SET "
                      "NOM=:nom, EMAIL=:email, TELEPHONE=:telephone, "
                      "DEPART=:depart, POSTE=:poste, SALAIRE=:salaire, "
                      "DATE_DEBUT=:date_debut, MOT_DE_PASSE=:mot "
                      "WHERE ID=:id");
        query.bindValue(":mot", ensureHashedPassword(mot_de_passe));
    }

    query.bindValue(":nom", nom);
    query.bindValue(":email", email);
    query.bindValue(":telephone", telephone);
    query.bindValue(":depart", depart);
    query.bindValue(":poste", poste);
    query.bindValue(":salaire", salaire);
    query.bindValue(":date_debut", date_debut);
    query.bindValue(":id", id);

    return query.exec();
}

QSqlQuery Employee::afficher()
{
    QSqlQuery query;
    query.prepare("SELECT ID, NOM, EMAIL, TELEPHONE, DEPART, POSTE, SALAIRE, DATE_DEBUT FROM EMPLOYEE");
    query.exec();
    return query;
}

QString Employee::hashPassword(const QString &plainPassword)
{
    return ensureHashedPassword(plainPassword);
}

bool Employee::hasAnyEmployee(QString *errorMessage)
{
    QSqlQuery query;
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM EMPLOYEE"));
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return true;
    }
    if (!query.next()) {
        return true;
    }
    return query.value(0).toInt() > 0;
}

bool Employee::authenticate(const QString &identifier, const QString &password, QString *errorMessage)
{
    const QString normalizedIdentifier = identifier.trimmed().toLower();
    const QString normalizedPassword = password.trimmed();
    if (normalizedIdentifier.isEmpty() || normalizedPassword.isEmpty()) {
        return false;
    }

    const QString hashedInput = ensureHashedPassword(normalizedPassword);
    const QString hashedInputLocal = ensureHashedPasswordLocal8Bit(normalizedPassword);

    QSqlQuery query;
    query.prepare(QStringLiteral(
        "SELECT ID, MOT_DE_PASSE "
        "FROM EMPLOYEE "
        "WHERE (LOWER(TRIM(EMAIL)) = :identifier OR LOWER(TRIM(NOM)) = :identifier)"));
    query.bindValue(QStringLiteral(":identifier"), normalizedIdentifier);
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    bool accountFound = false;
    while (query.next()) {
        accountFound = true;
        const int employeeId = query.value(0).toInt();
        const QString storedPassword = query.value(1).toString().trimmed();
        if (storedPassword.compare(hashedInput, Qt::CaseInsensitive) == 0
            || storedPassword.compare(hashedInputLocal, Qt::CaseInsensitive) == 0) {
            return true;
        }

        // Compatibilité temporaire: si ancien mot de passe en clair, migrer vers SHA-256.
        if (storedPassword == normalizedPassword) {
            QSqlQuery updateQuery;
            updateQuery.prepare(QStringLiteral("UPDATE EMPLOYEE SET MOT_DE_PASSE=:mot WHERE ID=:id"));
            updateQuery.bindValue(QStringLiteral(":mot"), hashedInput);
            updateQuery.bindValue(QStringLiteral(":id"), employeeId);
            updateQuery.exec();
            return true;
        }
    }
    if (errorMessage) {
        if (!accountFound) {
            *errorMessage = QStringLiteral("No employee found for this email/username.");
        } else {
            *errorMessage = QStringLiteral("Employee found but password mismatch.");
        }
    }
    return false;
}



