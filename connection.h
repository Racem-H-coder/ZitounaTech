#ifndef CONNECTION_H
#define CONNECTION_H

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>
#include <QString>

class Connection
{
public:
    Connection();
    bool createConnect();
    void closeConnect();
    QString lastError() const;

private:
    QSqlDatabase db;
    QString lastErrorMessage;
};

#endif // CONNECTION_H
