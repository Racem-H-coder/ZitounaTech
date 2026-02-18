-- Oracle : table équipements + identifiant auto (séquence + trigger)
-- Exécuter en tant que propriétaire du schéma utilisé par l'application.

CREATE SEQUENCE EQUIPEMENT_SEQ
    START WITH 1
    INCREMENT BY 1
    NOCACHE
    NOCYCLE;

CREATE TABLE EQUIPEMENT (
    ID            NUMBER         PRIMARY KEY,
    REFERENCE     VARCHAR2(100),
    MODELE        VARCHAR2(200),
    MARQUE        VARCHAR2(100),
    TYPE_EQUI     VARCHAR2(200),
    ETAT          VARCHAR2(50),
    NUMERO_SERIE  VARCHAR2(100),
    COUT          NUMBER(12, 2),
    DATE_AJOUT    DATE
);

CREATE OR REPLACE TRIGGER EQUIPEMENT_BI_TRG
    BEFORE INSERT ON EQUIPEMENT
    FOR EACH ROW
BEGIN
    IF :NEW.ID IS NULL THEN
        SELECT EQUIPEMENT_SEQ.NEXTVAL INTO :NEW.ID FROM DUAL;
    END IF;
END;
/

-- Optionnel : unicité du numéro de série
-- CREATE UNIQUE INDEX UK_EQUIPEMENT_NS ON EQUIPEMENT (NUMERO_SERIE);
