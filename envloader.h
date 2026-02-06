#ifndef ENVLOADER_H
#define ENVLOADER_H

#include <QString>

// Charge un fichier ".env" (clé=valeur) et injecte dans l'environnement du process.
// - Ne remplace pas une variable déjà définie dans l'OS (sauf si overrideExisting=true).
// - Ignore les lignes vides et les commentaires (#).
//
// Usage recommandé: appeler une fois au démarrage (main.cpp) avant d'utiliser l'API.
namespace EnvLoader {
bool loadDotEnvFile(const QString &path, bool overrideExisting = false, QString *errorMessage = nullptr);
bool loadDotEnvNearExecutable(bool overrideExisting = false, QString *errorMessage = nullptr);
// Essaie plusieurs emplacements usuels:
// - dossier courant (souvent celui du projet en mode Qt Creator)
// - dossier de l'exécutable
// - dossier parent de l'exécutable (ex: build/Debug -> build)
// Retourne true si au moins un .env a été chargé.
bool loadDotEnvAuto(bool overrideExisting = false, QString *errorMessage = nullptr);
} // namespace EnvLoader

#endif // ENVLOADER_H

