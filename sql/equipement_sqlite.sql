-- SQLite (tests locaux) : équivalent simplifié avec AUTOINCREMENT

CREATE TABLE IF NOT EXISTS EQUIPEMENT (
    ID            INTEGER PRIMARY KEY AUTOINCREMENT,
    REFERENCE     TEXT,
    MODELE        TEXT,
    MARQUE        TEXT,
    TYPE_EQUI     TEXT,
    ETAT          TEXT,
    NUMERO_SERIE  TEXT,
    COUT          REAL,
    DATE_AJOUT    TEXT
);

-- Note : l'application Qt lie DATE_AJOUT en QDate ; avec SQLite vous pouvez
-- stocker ISO (YYYY-MM-DD) ou adapter equipement.cpp si besoin.
