#include "envloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

namespace {

static QString stripOptionalQuotes(QString v)
{
    v = v.trimmed();
    if (v.size() >= 2) {
        const QChar a = v.front();
        const QChar b = v.back();
        if ((a == QLatin1Char('"') && b == QLatin1Char('"')) || (a == QLatin1Char('\'') && b == QLatin1Char('\'')))
            return v.mid(1, v.size() - 2);
    }
    return v;
}

} // namespace

bool EnvLoader::loadDotEnvFile(const QString &path, bool overrideExisting, QString *errorMessage)
{
    if (errorMessage)
        errorMessage->clear();

    QFile f(path);
    if (!f.exists()) {
        // Pas une erreur bloquante: on laisse la config OS faire le job.
        return false;
    }
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Impossible d'ouvrir %1").arg(path);
        return false;
    }

    QTextStream in(&f);
    int lineNo = 0;
    while (!in.atEnd()) {
        lineNo++;
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;

        const QString key = line.left(eq).trimmed();
        QString value = line.mid(eq + 1);
        value = stripOptionalQuotes(value);

        if (key.isEmpty())
            continue;

        const QByteArray k = key.toUtf8();
        // Si la variable existe déjà mais est vide, on l'autorise à être remplie par .env
        // (cas fréquent avec l'environnement de lancement Qt Creator / Windows).
        if (!overrideExisting && qEnvironmentVariableIsSet(k.constData())) {
            const QString existing = qEnvironmentVariable(k.constData());
            if (!existing.trimmed().isEmpty())
                continue;
        }

        qputenv(k.constData(), value.toUtf8());
    }

    return true;
}

bool EnvLoader::loadDotEnvNearExecutable(bool overrideExisting, QString *errorMessage)
{
    const QString exeDir = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
    const QString envPath = exeDir + QStringLiteral("/.env");
    return loadDotEnvFile(envPath, overrideExisting, errorMessage);
}

bool EnvLoader::loadDotEnvAuto(bool overrideExisting, QString *errorMessage)
{
    if (errorMessage)
        errorMessage->clear();

    bool anyLoaded = false;
    QString lastErr;

    const QString exeDir = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
    const QStringList startDirs{
        QDir::currentPath(),
        exeDir,
        // Chemin du dossier source (fonctionne même en shadow build)
        QFileInfo(QStringLiteral(__FILE__)).absolutePath(),
    };

    // Important: en mode Qt Creator, le working dir est souvent le dossier build.
    // On remonte donc les parents pour trouver le .env du projet.
    QSet<QString> visited;
    for (const QString &start : startDirs) {
        QDir d(start);
        for (int depth = 0; depth < 8; ++depth) {
            const QString dirPath = d.absolutePath();
            if (visited.contains(dirPath))
                break;
            visited.insert(dirPath);

            const QString envPath = dirPath + QStringLiteral("/.env");
            if (loadDotEnvFile(envPath, overrideExisting, &lastErr))
                anyLoaded = true;

            if (!d.cdUp())
                break;
        }
    }

    if (!anyLoaded && errorMessage)
        *errorMessage = lastErr;
    return anyLoaded;
}

