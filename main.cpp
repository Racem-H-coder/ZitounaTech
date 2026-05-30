#include "mainwindow.h"
#include "connection.h"
#include "envloader.h"

#include <QApplication>
#include <QMessageBox>
#include <QSslSocket>

int main(int argc, char *argv[])
{
    qDebug() << "PROGRAM STARTED";
    QApplication a(argc, argv);

    // Vérification SSL au démarrage (HTTPS vers Anthropic, etc.)
    if (!QSslSocket::supportsSsl()) {
        QMessageBox::critical(
            nullptr,
            QObject::tr("SSL manquant"),
            QObject::tr("OpenSSL introuvable.\n"
                          "Copiez libssl-3-x64.dll et libcrypto-3-x64.dll\n"
                          "dans le même dossier que l'exécutable.\n\n"
                          "Chemin Qt : C:/Qt/6.x.x/mingw_64/bin/"));
        return -1;
    }

    // Chargement optionnel d'un fichier .env (dossier courant / exe / parent exe).
    // Important: ne jamais commiter le .env (il peut contenir des clés).
    EnvLoader::loadDotEnvAuto(false);

    Connection c;
    if (!c.createConnect()) {
        QMessageBox::critical(
            nullptr,
            QObject::tr("Connexion base de données"),
            QObject::tr("Impossible d'ouvrir la connexion ODBC (Zitounatech).\n"
                          "Vérifiez le DSN / Oracle et exécutez le script SQL des nouvelles tables si besoin.\n\n"
                          "Détails:\n%1").arg(c.lastError()));
        return -1;
    }
    MainWindow w;
    w.show();
    return a.exec();
}
