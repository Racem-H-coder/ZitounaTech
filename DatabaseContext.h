#ifndef DATABASECONTEXT_H
#define DATABASECONTEXT_H

#include <QString>

namespace DatabaseContext {

inline QString authorizedSchema()
{
    return QStringLiteral(
        "Base Oracle — tables AUTORISÉES : CLIENT et FACTURE uniquement.\n\n"
        "TABLE CLIENT :\n"
        "  ID_CLIENT (NUMBER PK),\n"
        "  NOM (VARCHAR2),\n"
        "  PRENOM (VARCHAR2),\n"
        "  EMAIL (VARCHAR2),\n"
        "  TELEPHONE (VARCHAR2),\n"
        "  GENRE (VARCHAR2),\n"
        "  DATE_ENREGISTREMENT (DATE),\n"
        "  ACHATS (VARCHAR2) — ex. 'ACHAT OLIVE', 'PRESSION OLIVE',\n"
        "  CIN (VARCHAR2)\n\n"
        "TABLE FACTURE (commandes / facturation liée à un client) :\n"
        "  ID_FACTURE (NUMBER PK),\n"
        "  ID_CLIENT (NUMBER FK → CLIENT.ID_CLIENT),\n"
        "  TYPE_FACTURE (VARCHAR2),\n"
        "  DATE_FACTURATION (DATE),\n"
        "  QUANTITE (NUMBER),\n"
        "  PRIX_UNITAIRE (NUMBER), … (autres colonnes métier)\n\n"
        "Les \"commandes clients\" à l'écran combinent des lignes FACTURE+CLIENT "
        "et des lignes dérivées de CLIENT (voir requêtes avec JOIN CLIENT / FACTURE).\n");
}

} // namespace DatabaseContext

#endif // DATABASECONTEXT_H
