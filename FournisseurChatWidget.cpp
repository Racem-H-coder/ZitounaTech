#include "FournisseurChatWidget.h"

#include "LlmClientFournisseurs.h"

#include <QAbstractItemView>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlRecord>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QSvgRenderer>

namespace {

QPixmap makeSendIconPixmap()
{
    const QByteArray svg = QByteArrayLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"32\" height=\"32\" viewBox=\"0 0 32 32\">"
        "<path fill=\"#FFFFFF\" d=\"M6 16h14l-4-4 1.4-1.4L26.8 17l-9.4 9.4L16 25l4-4H6z\"/></svg>");
    QSvgRenderer renderer(svg);
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    renderer.render(&p);
    return pm;
}

int columnIndexOf(const QStringList &cols, const QStringList &cands)
{
    for (const QString &cand : cands) {
        for (int i = 0; i < cols.size(); ++i) {
            if (cols.at(i).compare(cand, Qt::CaseInsensitive) == 0) {
                return i;
            }
        }
    }
    return -1;
}

QString fournisseurNatureColumn()
{
    static QString cachedColumn;
    if (!cachedColumn.isEmpty()) {
        return cachedColumn;
    }

    QSqlQuery q;
    if (q.exec(QStringLiteral(
            "SELECT COLUMN_NAME "
            "FROM USER_TAB_COLUMNS "
            "WHERE TABLE_NAME = 'FOURNISSEUR' "
            "AND COLUMN_NAME IN ('NATURE', 'TYPE', 'TYPE_FOURNISSEUR')"))) {
        bool hasNature = false;
        bool hasType = false;
        bool hasTypeFournisseur = false;
        while (q.next()) {
            const QString c = q.value(0).toString().trimmed().toUpper();
            if (c == QStringLiteral("NATURE")) {
                hasNature = true;
            } else if (c == QStringLiteral("TYPE")) {
                hasType = true;
            } else if (c == QStringLiteral("TYPE_FOURNISSEUR")) {
                hasTypeFournisseur = true;
            }
        }
        if (hasNature) {
            cachedColumn = QStringLiteral("NATURE");
        } else if (hasType) {
            cachedColumn = QStringLiteral("TYPE");
        } else if (hasTypeFournisseur) {
            cachedColumn = QStringLiteral("TYPE_FOURNISSEUR");
        }
    }

    if (cachedColumn.isEmpty()) {
        cachedColumn = QStringLiteral("NATURE");
    }
    return cachedColumn;
}

} // namespace

FournisseurChatWidget::FournisseurChatWidget(const QSqlDatabase &db, QWidget *parent)
    : QWidget(parent)
    , m_db(db)
    , m_llm(new LlmClientFournisseurs(this))
{
    connect(m_llm, &LlmClientFournisseurs::sqlReady, this, &FournisseurChatWidget::onLlmSqlReady);
    connect(m_llm, &LlmClientFournisseurs::answerReady, this, &FournisseurChatWidget::onLlmAnswerReady);
    connect(m_llm, &LlmClientFournisseurs::errorOccurred, this, &FournisseurChatWidget::onNetworkError);

    setupUi();
    applyStyles();
    addSuggestions();
    addMessage(tr("Bonjour ! Je peux vous renseigner uniquement sur vos fournisseurs. "
                  "Comment puis-je vous aider ?"),
               false);
}

void FournisseurChatWidget::setupUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("fChatHeader"));
    auto *hLay = new QVBoxLayout(header);
    hLay->setContentsMargins(12, 12, 12, 12);
    auto *title = new QLabel(tr("Assistant Fournisseurs"), header);
    title->setObjectName(QStringLiteral("fChatTitle"));
    auto *sub = new QLabel(tr("Posez vos questions sur vos fournisseurs"), header);
    sub->setObjectName(QStringLiteral("fChatSub"));
    hLay->addWidget(title);
    hLay->addWidget(sub);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setObjectName(QStringLiteral("fChatScroll"));

    m_messagesWidget = new QWidget();
    m_messagesLayout = new QVBoxLayout(m_messagesWidget);
    m_messagesLayout->setContentsMargins(12, 12, 12, 12);
    m_messagesLayout->setSpacing(10);
    m_messagesLayout->addStretch(1);
    m_scrollArea->setWidget(m_messagesWidget);

    auto *suggestions = new QWidget(this);
    suggestions->setObjectName(QStringLiteral("fChatSugWrap"));
    m_suggestionsLayout = new QHBoxLayout(suggestions);
    m_suggestionsLayout->setContentsMargins(12, 8, 12, 8);
    m_suggestionsLayout->setSpacing(8);

    auto *bottom = new QWidget(this);
    bottom->setObjectName(QStringLiteral("fChatBottom"));
    auto *bLay = new QHBoxLayout(bottom);
    bLay->setContentsMargins(8, 8, 8, 8);
    bLay->setSpacing(8);
    m_inputField = new QLineEdit(bottom);
    m_inputField->setPlaceholderText(tr("Votre question fournisseur…"));
    m_inputField->setObjectName(QStringLiteral("fChatInput"));
    m_sendButton = new QPushButton(bottom);
    m_sendButton->setObjectName(QStringLiteral("fChatSend"));
    m_sendButton->setFixedSize(32, 32);
    m_sendButton->setIcon(QIcon(makeSendIconPixmap()));
    m_sendButton->setIconSize(QSize(20, 20));
    bLay->addWidget(m_inputField, 1);
    bLay->addWidget(m_sendButton, 0, Qt::AlignVCenter);

    m_typingIndicator = new QWidget(m_messagesWidget);
    auto *tLay = new QHBoxLayout(m_typingIndicator);
    tLay->setContentsMargins(0, 0, 0, 0);
    auto *ia = new QLabel(QStringLiteral("IA"), m_typingIndicator);
    ia->setObjectName(QStringLiteral("fTypingIA"));
    ia->setFixedSize(28, 28);
    ia->setAlignment(Qt::AlignCenter);
    tLay->addWidget(ia);
    for (int i = 0; i < 3; ++i) {
        m_typingDots[i] = new QLabel(QStringLiteral("•"), m_typingIndicator);
        m_typingDots[i]->setObjectName(QStringLiteral("fTypingDot"));
        tLay->addWidget(m_typingDots[i]);
    }
    tLay->addStretch(1);
    m_typingIndicator->hide();

    root->addWidget(header);
    root->addWidget(m_scrollArea, 1);
    root->addWidget(suggestions);
    root->addWidget(bottom);

    connect(m_sendButton, &QPushButton::clicked, this, &FournisseurChatWidget::onSendClicked);
    connect(m_inputField, &QLineEdit::returnPressed, this, &FournisseurChatWidget::onSendClicked);
}

void FournisseurChatWidget::applyStyles()
{
    setStyleSheet(QStringLiteral(
        "QWidget#fChatHeader { background:#2E7D32; border-radius:10px 10px 0 0; }"
        "QLabel#fChatTitle { color:white; font-size:14px; font-weight:700; }"
        "QLabel#fChatSub { color:#C8E6C9; font-size:11px; }"
        "QScrollArea#fChatScroll { background:#FFFFFF; border-left:1px solid #C8E6C9; border-right:1px solid #C8E6C9; border-top:none; border-bottom:none; }"
        "QWidget#fChatSugWrap { background:#F9FBE7; border-top:1px solid #E8F5E9; }"
        "QPushButton#fChatChip { background:white; border:1px solid #A5D6A7; border-radius:14px; padding:4px 10px; font-size:11px; color:#2E5016; }"
        "QPushButton#fChatChip:hover { background:#E8F5E9; }"
        "QWidget#fChatBottom { background:white; border:1px solid #C8E6C9; border-radius:0 0 10px 10px; }"
        "QLineEdit#fChatInput { border:1px solid #C8E6C9; border-radius:20px; padding:6px 14px; background:#F9FBF9; font-size:13px; }"
        "QPushButton#fChatSend { background:#2E7D32; border:none; border-radius:16px; }"
        "QPushButton#fChatSend:hover { background:#388E3C; }"
        "QLabel#fTypingIA { background:#A5D6A7; border-radius:14px; color:#1B5E20; font-size:11px; font-weight:700; }"
        "QLabel#fTypingDot { color:#2E7D32; font-size:20px; }"));
}

void FournisseurChatWidget::addSuggestions()
{
    const QStringList chips = {
        tr("Fournisseurs actifs"),
        tr("Fournisseurs à risque"),
        tr("Nouveaux fournisseurs ce mois"),
        tr("Compter fournisseurs"),
    };
    for (const QString &s : chips) {
        auto *b = new QPushButton(s, this);
        b->setObjectName(QStringLiteral("fChatChip"));
        connect(b, &QPushButton::clicked, this, [this, s]() { onSuggestionClicked(s); });
        m_suggestionsLayout->addWidget(b);
    }
    m_suggestionsLayout->addStretch(1);
}

void FournisseurChatWidget::addMessage(const QString &text, bool isUser)
{
    auto *row = new QWidget(m_messagesWidget);
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);
    auto *bubble = new QLabel(text, row);
    bubble->setWordWrap(true);
    bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (isUser) {
        bubble->setStyleSheet(QStringLiteral(
            "QLabel { background:#2E7D32; color:white; border-radius:16px 4px 16px 16px; padding:10px 14px; font-size:13px; }"));
        lay->addStretch(1);
        lay->addWidget(bubble);
    } else {
        auto *av = new QLabel(QStringLiteral("IA"), row);
        av->setFixedSize(28, 28);
        av->setAlignment(Qt::AlignCenter);
        av->setStyleSheet(QStringLiteral(
            "QLabel { background:#A5D6A7; border-radius:14px; color:#1B5E20; font-size:11px; font-weight:700; }"));
        bubble->setStyleSheet(QStringLiteral(
            "QLabel { background:#F1F8E9; border:1px solid #AED581; border-radius:4px 16px 16px 16px; padding:10px 14px; font-size:13px; color:#2E5016; }"));
        lay->addWidget(av);
        lay->addWidget(bubble);
        lay->addStretch(1);
    }
    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, row);
    scrollToBottom();
}

void FournisseurChatWidget::addDataCard(const QuerySnapshot &snapshot)
{
    if (snapshot.rows.isEmpty()) {
        return;
    }
    auto *card = new QWidget(m_messagesWidget);
    card->setStyleSheet(QStringLiteral(
        "QWidget { background:#F9FBE7; border:1px solid #C5E1A5; border-radius:8px; padding:10px; }"));
    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(10, 10, 10, 10);
    auto *table = new QTableWidget(card);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->setStyleSheet(QStringLiteral(
        "QTableWidget { background:#FFFFFF; font-size:12px; border:none; }"
        "QTableWidget::item { padding:6px; border:none; }"
        "QHeaderView::section { background:#E8F5E9; color:#558B2F; font-weight:600; border:none; padding:6px; }"));
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);

    const QStringList cols = snapshot.columnNames;
    const int iId = columnIndexOf(cols, {QStringLiteral("ID_FOURNISSEUR"), QStringLiteral("id_fournisseur")});
    const int iNom = columnIndexOf(cols, {QStringLiteral("NOM"), QStringLiteral("nom")});
    const int iRegion = columnIndexOf(cols, {QStringLiteral("REGION"), QStringLiteral("region")});
    const int iEmail = columnIndexOf(cols, {QStringLiteral("EMAIL"), QStringLiteral("email")});
    const int iNature = columnIndexOf(cols, {QStringLiteral("NATURE"), QStringLiteral("TYPE_FOURNISSEUR"),
                                             QStringLiteral("TYPE"), QStringLiteral("nature")});
    const int iDate = columnIndexOf(cols, {QStringLiteral("DATE_AJOUT"), QStringLiteral("date_ajout"),
                                           QStringLiteral("DATE_AJOUT_FMT")});
    const int iStatus = columnIndexOf(cols, {QStringLiteral("STATUS_FOURNISSEUR"), QStringLiteral("status_fournisseur"),
                                             QStringLiteral("STATUS")});
    const int iTel = columnIndexOf(cols, {QStringLiteral("TELEPHONE"), QStringLiteral("telephone")});

    table->setColumnCount(8);
    table->setHorizontalHeaderLabels({tr("ID"), tr("Nom"), tr("Region"), tr("Email"),
                                      tr("Nature"), tr("Date ajout"), tr("Status"), tr("Telephone")});
    table->setRowCount(snapshot.rows.size());
    for (int r = 0; r < snapshot.rows.size(); ++r) {
        const QStringList &row = snapshot.rows.at(r);
        auto val = [&](int idx) -> QString { return (idx >= 0 && idx < row.size()) ? row.at(idx) : QString(); };
        table->setItem(r, 0, new QTableWidgetItem(val(iId)));
        table->setItem(r, 1, new QTableWidgetItem(val(iNom)));
        table->setItem(r, 2, new QTableWidgetItem(val(iRegion)));
        table->setItem(r, 3, new QTableWidgetItem(val(iEmail)));
        table->setItem(r, 4, new QTableWidgetItem(val(iNature)));
        table->setItem(r, 5, new QTableWidgetItem(val(iDate)));
        table->setItem(r, 6, new QTableWidgetItem(val(iStatus)));
        table->setItem(r, 7, new QTableWidgetItem(val(iTel)));
    }
    v->addWidget(table);

    auto *row = new QWidget(m_messagesWidget);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->addStretch(1);
    h->addWidget(card);
    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, row);
    scrollToBottom();
}

void FournisseurChatWidget::showTyping(bool visible)
{
    if (visible) {
        if (m_messagesLayout->indexOf(m_typingIndicator) < 0) {
            m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, m_typingIndicator);
        }
        m_typingIndicator->show();
    } else {
        m_typingIndicator->hide();
    }
    scrollToBottom();
}

void FournisseurChatWidget::scrollToBottom()
{
    if (!m_scrollArea) {
        return;
    }
    QTimer::singleShot(0, this, [this]() {
        if (m_scrollArea && m_scrollArea->verticalScrollBar()) {
            m_scrollArea->verticalScrollBar()->setValue(m_scrollArea->verticalScrollBar()->maximum());
        }
    });
}

QString FournisseurChatWidget::normalizeSql(QString sql)
{
    sql = sql.trimmed();
    while (sql.endsWith(QLatin1Char(';'))) {
        sql.chop(1);
        sql = sql.trimmed();
    }
    return sql;
}

bool FournisseurChatWidget::isSqlSafe(const QString &sql) const
{
    const QString s = sql.toLower();
    const QStringList forbidden = {
        QStringLiteral("client"), QStringLiteral("clients"), QStringLiteral("facture"),
        QStringLiteral("stock"),  QStringLiteral("produit"), QStringLiteral("employe"),
        QStringLiteral("equipement"), QStringLiteral("commande"), QStringLiteral("commandes"),
    };
    for (const QString &t : forbidden) {
        if (s.contains(QRegularExpression(QStringLiteral("\\b%1\\b").arg(t)))) {
            return false;
        }
    }
    const QStringList dangerous = {
        QStringLiteral("drop"), QStringLiteral("delete"), QStringLiteral("update"),
        QStringLiteral("insert"), QStringLiteral("alter"), QStringLiteral("create"),
        QStringLiteral("truncate"),
    };
    for (const QString &d : dangerous) {
        if (s.contains(QRegularExpression(QStringLiteral("\\b%1\\b").arg(d)))) {
            return false;
        }
    }
    if (s.contains(QStringLiteral("--")) || s.contains(QLatin1Char(';'))) {
        return false;
    }
    return true;
}

QString FournisseurChatWidget::buildDbContext() const
{
    return QStringLiteral(
        "TABLE FOURNISSEUR :\n"
        "  ID_FOURNISSEUR (NUMBER PK),\n"
        "  NOM (VARCHAR2), REGION (VARCHAR2), EMAIL (VARCHAR2),\n"
        "  NATURE (VARCHAR2),\n"
        "  DATE_AJOUT (DATE), STATUS_FOURNISSEUR (VARCHAR2), TELEPHONE (VARCHAR2),\n"
        "  TYPE (VARCHAR2, optionnel)\n");
}

void FournisseurChatWidget::fillSnapshotAndCsv(QSqlQuery &query, QuerySnapshot *snap, QString *csvOut)
{
    snap->rows.clear();
    snap->columnNames.clear();

    const QSqlRecord rec = query.record();
    QStringList headers;
    for (int i = 0; i < rec.count(); ++i) {
        headers << rec.fieldName(i);
    }
    snap->columnNames = headers;

    QString csv = headers.join(QLatin1Char(';')) + QLatin1Char('\n');
    bool any = false;
    while (query.next()) {
        any = true;
        QStringList row;
        for (int i = 0; i < rec.count(); ++i) {
            row << query.value(i).toString();
        }
        snap->rows.append(row);
        csv += row.join(QLatin1Char(';')) + QLatin1Char('\n');
    }
    if (!any) {
        csv += QStringLiteral("(aucune ligne)\n");
    }
    *csvOut = csv;
}

bool FournisseurChatWidget::ensureDatabaseReady(QString *errorText)
{
    if (m_db.isValid() && m_db.isOpen()) {
        return true;
    }
    if (QSqlDatabase::contains(QStringLiteral("qt_sql_default_connection"))) {
        m_db = QSqlDatabase::database(QStringLiteral("qt_sql_default_connection"), false);
    } else {
        m_db = QSqlDatabase::database();
    }
    if (!m_db.isValid()) {
        if (errorText)
            *errorText = tr("Connexion SQL invalide.");
        return false;
    }
    if (!m_db.isOpen() && !m_db.open()) {
        if (errorText)
            *errorText = m_db.lastError().text();
        return false;
    }
    return true;
}

bool FournisseurChatWidget::tryLocalFallbackQuery(const QString &question, QuerySnapshot *snap, QString *csvOut)
{
    const QString qLower = question.toLower();
    QSqlQuery q(m_db);
    bool prepared = false;
    const QString natureCol = fournisseurNatureColumn();

    if (qLower.contains(QStringLiteral("actif"))) {
        prepared = q.prepare(QStringLiteral(
            "SELECT ID_FOURNISSEUR, NOM, REGION, EMAIL, "
            "%1 AS NATURE, "
            "TO_CHAR(DATE_AJOUT,'DD/MM/YYYY') AS DATE_AJOUT, "
            "STATUS_FOURNISSEUR, TELEPHONE "
            "FROM FOURNISSEUR WHERE LOWER(STATUS_FOURNISSEUR) LIKE '%actif%' AND ROWNUM <= 100")
                                 .arg(natureCol));
    } else if (qLower.contains(QStringLiteral("risque"))) {
        prepared = q.prepare(QStringLiteral(
            "SELECT ID_FOURNISSEUR, NOM, REGION, EMAIL, "
            "%1 AS NATURE, "
            "TO_CHAR(DATE_AJOUT,'DD/MM/YYYY') AS DATE_AJOUT, "
            "STATUS_FOURNISSEUR, TELEPHONE "
            "FROM FOURNISSEUR WHERE LOWER(STATUS_FOURNISSEUR) LIKE '%risque%' AND ROWNUM <= 100")
                                 .arg(natureCol));
    } else if (qLower.contains(QStringLiteral("ce mois")) || qLower.contains(QStringLiteral("nouveau"))) {
        prepared = q.prepare(QStringLiteral(
            "SELECT * FROM ( "
            "SELECT ID_FOURNISSEUR, NOM, REGION, EMAIL, "
            "%1 AS NATURE, "
            "TO_CHAR(DATE_AJOUT,'DD/MM/YYYY') AS DATE_AJOUT, "
            "STATUS_FOURNISSEUR, TELEPHONE "
            "FROM FOURNISSEUR "
            "WHERE TO_CHAR(DATE_AJOUT,'YYYY-MM') = TO_CHAR(SYSDATE,'YYYY-MM') "
            "ORDER BY DATE_AJOUT DESC ) WHERE ROWNUM <= 100")
                                 .arg(natureCol));
    } else if (qLower.contains(QStringLiteral("combien")) || qLower.contains(QStringLiteral("nombre"))) {
        prepared = q.prepare(QStringLiteral("SELECT COUNT(*) AS TOTAL_FOURNISSEURS FROM FOURNISSEUR"));
    } else {
        prepared = q.prepare(QStringLiteral(
            "SELECT ID_FOURNISSEUR, NOM, REGION, EMAIL, "
            "%1 AS NATURE, "
            "TO_CHAR(DATE_AJOUT,'DD/MM/YYYY') AS DATE_AJOUT, "
            "STATUS_FOURNISSEUR, TELEPHONE "
            "FROM FOURNISSEUR WHERE ROWNUM <= 100")
                                 .arg(natureCol));
    }

    if (!prepared || !q.exec()) {
        return false;
    }
    fillSnapshotAndCsv(q, snap, csvOut);
    return !snap->rows.isEmpty();
}

void FournisseurChatWidget::onSendClicked()
{
    const QString t = m_inputField ? m_inputField->text().trimmed() : QString();
    if (t.isEmpty()) {
        return;
    }
    m_inputField->clear();
    m_pendingQuestion = t;
    addMessage(t, true);
    showTyping(true);
    m_llm->requestSql(t, buildDbContext());
}

void FournisseurChatWidget::onSuggestionClicked(const QString &text)
{
    if (m_inputField) {
        m_inputField->setText(text);
    }
    onSendClicked();
}

void FournisseurChatWidget::onLlmSqlReady(const QString &sql)
{
    QString dbErr;
    if (!ensureDatabaseReady(&dbErr)) {
        onNetworkError(tr("Connexion base de données indisponible.\n%1").arg(dbErr));
        return;
    }

    QString s = normalizeSql(sql);
    if (s.toLower().contains(QStringLiteral("hors_sujet"))) {
        m_pendingSnapshot = {};
        m_llm->requestAnswer(m_pendingQuestion,
                             QStringLiteral("Aucune donnée SQL exploitable: la requête a été marquée hors sujet."));
        return;
    }
    if (!isSqlSafe(s)) {
        m_pendingSnapshot = {};
        m_llm->requestAnswer(m_pendingQuestion,
                             QStringLiteral("Aucune donnée SQL exploitable: requête rejetée par les règles de sécurité."));
        return;
    }

    QSqlQuery q(m_db);
    if (!q.exec(s)) {
        m_pendingSnapshot = {};
        m_llm->requestAnswer(m_pendingQuestion,
                             QStringLiteral("Aucune donnée SQL exploitable: exécution de requête invalide."));
        return;
    }

    QString csv;
    QuerySnapshot snap;
    fillSnapshotAndCsv(q, &snap, &csv);
    m_pendingSnapshot = snap;
    m_llm->requestAnswer(m_pendingQuestion, csv);
}

void FournisseurChatWidget::onLlmAnswerReady(const QString &answer)
{
    showTyping(false);
    addMessage(answer, false);
    if (!m_pendingSnapshot.rows.isEmpty()) {
        addDataCard(m_pendingSnapshot);
    }
    m_pendingSnapshot = {};
}

void FournisseurChatWidget::onNetworkError(const QString &error)
{
    showTyping(false);
    auto *row = new QWidget(m_messagesWidget);
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(0, 4, 0, 4);
    auto *bubble = new QLabel(error, row);
    bubble->setWordWrap(true);
    bubble->setMaximumWidth(520);
    bubble->setStyleSheet(QStringLiteral(
        "background-color:#FFEBEE;color:#B71C1C;border:1px solid #EF9A9A;"
        "border-radius:4px 16px 16px 16px;padding:10px 14px;font-size:12px;"));
    l->addWidget(bubble);
    l->addStretch();
    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, row);
    scrollToBottom();
}

