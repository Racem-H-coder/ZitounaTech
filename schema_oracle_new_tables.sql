-- =============================================================================
-- Oracle : création des tables CLIENT, FOURNISSEUR, FACTURE + séquence + index
-- A executer sur le MEME schema que la table EMPLOYEE (meme utilisateur Oracle).
-- Exécuter tout le script d'un coup (F5 dans SQL Developer) ou ligne par ligne.
-- =============================================================================

-- ---------------------------------------------------------------------------
-- ETAPE 0 - Nettoyage (si vous relancez le script apres une erreur)
-- Décommentez le bloc suivant si besoin, exécutez-le, puis relancez le reste.
-- ---------------------------------------------------------------------------
/*
DROP INDEX IDX_FACTURE_FOUR;
DROP INDEX IDX_FACTURE_CLIENT;
DROP INDEX IDX_FACTURE_DATE;
DROP TABLE FACTURE;
DROP TABLE FOURNISSEUR;
DROP TABLE CLIENT;
DROP SEQUENCE SEQ_FACTURE;
DROP SEQUENCE SEQ_CLIENT;
*/

-- ---------------------------------------------------------------------------
-- ETAPE 1 - Sequence pour les IDs clients
-- ---------------------------------------------------------------------------
CREATE SEQUENCE SEQ_CLIENT
    START WITH 1
    INCREMENT BY 1
    NOCACHE
    NOCYCLE;

-- ---------------------------------------------------------------------------
-- ETAPE 2 - Table CLIENT
-- ---------------------------------------------------------------------------
CREATE TABLE CLIENT (
    ID_CLIENT             NUMBER        NOT NULL,
    NOM                   VARCHAR2(100) NOT NULL,
    PRENOM                VARCHAR2(100) NOT NULL,
    EMAIL                 VARCHAR2(120) NOT NULL,
    TELEPHONE             VARCHAR2(20)  NOT NULL,
    CIN                   VARCHAR2(8)   NOT NULL,
    GENRE                 VARCHAR2(20)  NOT NULL,
    DATE_ENREGISTREMENT   DATE          NOT NULL,
    ACHATS                VARCHAR2(100),
    CONSTRAINT PK_CLIENT PRIMARY KEY (ID_CLIENT),
    CONSTRAINT UQ_CLIENT_EMAIL UNIQUE (EMAIL),
    CONSTRAINT UQ_CLIENT_TELEPHONE UNIQUE (TELEPHONE),
    CONSTRAINT UQ_CLIENT_CIN UNIQUE (CIN)
);

-- ---------------------------------------------------------------------------
-- ETAPE 3 - Table FOURNISSEUR
-- ---------------------------------------------------------------------------
CREATE TABLE FOURNISSEUR (
    ID_FOURNISSEUR        NUMBER        NOT NULL,
    NOM                   VARCHAR2(100) NOT NULL,
    REGION                VARCHAR2(100) NOT NULL,
    EMAIL                 VARCHAR2(120) NOT NULL,
    TYPE_FOURNISSEUR      VARCHAR2(80)  NOT NULL,
    DATE_AJOUT            DATE          NOT NULL,
    STATUS_FOURNISSEUR    VARCHAR2(40)  NOT NULL,
    TELEPHONE             VARCHAR2(20)  NOT NULL,
    CONSTRAINT PK_FOURNISSEUR PRIMARY KEY (ID_FOURNISSEUR),
    CONSTRAINT UQ_FOURNISSEUR_EMAIL UNIQUE (EMAIL)
);

-- ---------------------------------------------------------------------------
-- ETAPE 4 - Table FACTURE (references CLIENT et FOURNISSEUR)
-- ---------------------------------------------------------------------------
CREATE TABLE FACTURE (
    ID_FACTURE            NUMBER         NOT NULL,
    MATRICULE_FISCAL      VARCHAR2(20)   NOT NULL,
    NOM_ENTREPRISE        VARCHAR2(100)  NOT NULL,
    NOM_RAISON_SOCIALE    VARCHAR2(100)  NOT NULL,
    TYPE_FACTURE          VARCHAR2(80)   NOT NULL,
    DATE_FACTURATION      DATE           NOT NULL,
    PRIX_UNITAIRE         NUMBER(14, 2)  NOT NULL,
    QUANTITE              NUMBER(10)     NOT NULL,
    DESCRIPTION           VARCHAR2(255),
    ID_CLIENT             NUMBER,
    ID_FOURNISSEUR        NUMBER,
    SIGNATURE_ELEC        VARCHAR2(64),
    SIGNED_AT             DATE,
    SIGNED_BY             VARCHAR2(100),
    SIGNATURE_DRAW        VARCHAR2(4000),
    CONSTRAINT PK_FACTURE PRIMARY KEY (ID_FACTURE),
    CONSTRAINT FK_FACTURE_CLIENT
        FOREIGN KEY (ID_CLIENT) REFERENCES CLIENT (ID_CLIENT)
        ON DELETE SET NULL,
    CONSTRAINT FK_FACTURE_FOURNISSEUR
        FOREIGN KEY (ID_FOURNISSEUR) REFERENCES FOURNISSEUR (ID_FOURNISSEUR)
        ON DELETE SET NULL,
    CONSTRAINT CHK_FACTURE_PRIX CHECK (PRIX_UNITAIRE > 0),
    CONSTRAINT CHK_FACTURE_QTE CHECK (QUANTITE > 0)
);

-- ---------------------------------------------------------------------------
-- Séquence pour les IDs factures (auto-incrément côté base, comme SEQ_CLIENT)
-- Si la table FACTURE contient déjà des données : remplacez START WITH 1 par
-- la valeur (SELECT NVL(MAX(ID_FACTURE),0) + 1 FROM FACTURE) avant la première utilisation.
-- ---------------------------------------------------------------------------
CREATE SEQUENCE SEQ_FACTURE
    START WITH 1
    INCREMENT BY 1
    NOCACHE
    NOCYCLE;

-- ---------------------------------------------------------------------------
-- ETAPE 5 - Index (apres creation de FACTURE)
-- ---------------------------------------------------------------------------
CREATE INDEX IDX_FACTURE_DATE ON FACTURE (DATE_FACTURATION);
CREATE INDEX IDX_FACTURE_CLIENT ON FACTURE (ID_CLIENT);
CREATE INDEX IDX_FACTURE_FOUR ON FACTURE (ID_FOURNISSEUR);

-- Vérification
-- SELECT table_name FROM user_tables WHERE table_name IN ('CLIENT','FOURNISSEUR','FACTURE') ORDER BY 1;

-- ---------------------------------------------------------------------------
-- Base déjà en place : ajouter seulement la séquence des factures
-- (si ORA-02289: la séquence SEQ_FACTURE n'existe pas)
-- ---------------------------------------------------------------------------
-- DROP SEQUENCE SEQ_FACTURE;  -- seulement si recréation
-- CREATE SEQUENCE SEQ_FACTURE START WITH 1 INCREMENT BY 1 NOCACHE NOCYCLE;
-- Si vous migrez une base existante vers la signature electronique:
-- ALTER TABLE FACTURE ADD (SIGNATURE_ELEC VARCHAR2(64));
-- ALTER TABLE FACTURE ADD (SIGNED_AT DATE);
-- ALTER TABLE FACTURE ADD (SIGNED_BY VARCHAR2(100));
-- ALTER TABLE FACTURE ADD (SIGNATURE_DRAW VARCHAR2(4000));
-- Puis si FACTURE contient déjà des lignes, synchroniser le prochain ID :
-- DROP SEQUENCE SEQ_FACTURE;
-- CREATE SEQUENCE SEQ_FACTURE START WITH <resultat de : SELECT NVL(MAX(ID_FACTURE),0)+1 FROM FACTURE>
--     INCREMENT BY 1 NOCACHE NOCYCLE;


