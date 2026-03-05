-- Oracle : table STOCK + identifiant auto (séquence + trigger)
-- Exécuter en tant que propriétaire du schéma utilisé par l'application.

CREATE SEQUENCE STOCK_SEQ
    START WITH 1
    INCREMENT BY 1
    NOCACHE
    NOCYCLE;

CREATE TABLE STOCK (
    ID_STOCK      NUMBER         PRIMARY KEY,
    TYPE_PRODUIT  VARCHAR2(200),
    REGION        VARCHAR2(200),
    QUANTITE      NUMBER(12, 2),
    DATE_STOCK    DATE
);

CREATE OR REPLACE TRIGGER STOCK_BI_TRG
    BEFORE INSERT ON STOCK
    FOR EACH ROW
BEGIN
    IF :NEW.ID_STOCK IS NULL THEN
        SELECT STOCK_SEQ.NEXTVAL INTO :NEW.ID_STOCK FROM DUAL;
    END IF;
END;
/
