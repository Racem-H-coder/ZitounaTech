#ifndef CLIENT_H
#define CLIENT_H

#include <QString>
#include <QDate>
#include <QSqlQuery>
#include <QSqlQueryModel>
#include <QSqlRecord>

class Client
{
private:
    int id_client;
    QString nom;
    QString prenom;
    QString email;
    QString telephone;
    QString cin;
    QString genre;
    QDate date_enregistrement;
    QString achats;

public:
    Client();
    Client(QString nom, QString prenom, QString email,
           QString telephone, QString cin, QString genre,
           QDate date_enregistrement, QString achats);

    int getId() const { return id_client; }
    QString getNom() const { return nom; }
    QString getPrenom() const { return prenom; }
    QString getEmail() const { return email; }
    QString getTelephone() const { return telephone; }
    QString getCin() const { return cin; }
    QString getGenre() const { return genre; }
    QDate getDateEnregistrement() const { return date_enregistrement; }
    QString getAchats() const { return achats; }

    void setId(int id) { id_client = id; }
    void setNom(const QString &n) { nom = n; }
    void setPrenom(const QString &p) { prenom = p; }
    void setEmail(const QString &e) { email = e; }
    void setTelephone(const QString &t) { telephone = t; }
    void setCin(const QString &c) { cin = c; }
    void setGenre(const QString &g) { genre = g; }
    void setDateEnregistrement(const QDate &d) { date_enregistrement = d; }
    void setAchats(const QString &a) { achats = a; }

    bool ajouter();
    static QSqlQueryModel *afficher();
    bool modifier(int id);
    bool supprimer(int id);
    static QSqlQueryModel *rechercher(const QString &critere);
    static QSqlQueryModel *trier(const QString &colonne, const QString &ordre);
    bool charger(int id);
};

#endif
