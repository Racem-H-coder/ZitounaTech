-- =============================================================================
-- Optionnel : séquence SEQ_FACTURE (l'application peut utiliser MAX(ID)+1 sans séquence)
-- À exécuter seulement si vous préférez une séquence Oracle classique.
-- Même utilisateur que la connexion ODBC.
-- =============================================================================

-- Étape 1 : si une ancienne séquence incorrecte existe
-- DROP SEQUENCE SEQ_FACTURE;

-- Étape 2a — Table FACTURE vide (ou nouvelle base) :
CREATE SEQUENCE SEQ_FACTURE
    START WITH 1
    INCREMENT BY 1
    NOCACHE
    NOCYCLE;

-- Étape 2b — Si FACTURE a déjà des lignes : commentez le bloc 2a et exécutez ceci
-- (remplacez 100 par le résultat de : SELECT NVL(MAX(ID_FACTURE),0)+1 FROM FACTURE;)
/*
DROP SEQUENCE SEQ_FACTURE;
CREATE SEQUENCE SEQ_FACTURE
    START WITH 100
    INCREMENT BY 1
    NOCACHE
    NOCYCLE;
*/

-- Vérification
-- SELECT sequence_name, last_number FROM user_sequences WHERE sequence_name = 'SEQ_FACTURE';
