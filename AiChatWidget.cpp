#include "AiChatWidget.h"

#include "DatabaseContext.h"
#include "LlmClient.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <QSvgRenderer>

namespace {

constexpr char kVertFonce[] = "#2E7D32";
constexpr char kVertMoyen[] = "#388E3C";
constexpr char kVertClair[] = "#A5D6A7";
constexpr char kVertPale[] = "#E8F5E9";
constexpr char kBlanc[] = "#FFFFFF";
constexpr char kTexteSombre[] = "#1B2E1C";
constexpr char kTexteMute[] = "#558B2F";

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

int columnIndexOf(const QStringList &cols, const QStringList &candidates)
{
    for (const QString &cand : candidates) {
        for (int i = 0; i < cols.size(); ++i) {
            if (cols.at(i).compare(cand, Qt::CaseInsensitive) == 0)
                return i;
        }
    }
    return -1;
}

QString extractCinToken(const QString &question)
{
    const QRegularExpression re(QStringLiteral("(\\d{6,12})"));
    const QRegularExpressionMatch m = re.match(question);
    if (m.hasMatch()) {
        return m.captured(1);
    }
    return QString();
}

QString extractNameToken(const QString &question)
{
    QString cleaned = question;
    cleaned.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\s]")));
    const QStringList words = cleaned.split(QRegularExpression(QStringLiteral("\\s+")),
                                            Qt::SkipEmptyParts);
    static const QStringList stopWords = {
        QStringLiteral("combien"), QStringLiteral("commande"), QStringLiteral("commandes"),
        QStringLiteral("client"),  QStringLiteral("clients"),  QStringLiteral("passe"),
        QStringLiteral("passé"),   QStringLiteral("avec"),     QStringLiteral("cin"),
        QStringLiteral("le"),      QStringLiteral("la"),       QStringLiteral("les"),
        QStringLiteral("de"),      QStringLiteral("du"),       QStringLiteral("des"),
        QStringLiteral("ce"),      QStringLiteral("mois"),     QStringLiteral("nouveau"),
        QStringLiteral("nouveaux"), QStringLiteral("plus"),    QStringLiteral("fidele"),
        QStringLiteral("fidèle"),  QStringLiteral("en"),       QStringLiteral("cours"),
    };
    for (const QString &wRaw : words) {
        const QString w = wRaw.trimmed();
        if (w.size() < 3) {
            continue;
        }
        if (stopWords.contains(w.toLower())) {
            continue;
        }
        return w;
    }
    return QString();
}

} // namespace

AiChatWidget::AiChatWidget(const QSqlDatabase &db, QWidget *parent)
    : QWidget(parent)
    , m_db(db)
{
    m_llmClient = new LlmClient(this);

    connect(m_llmClient, &LlmClient::sqlReady, this, &AiChatWidget::onLlmSqlReady);
    connect(m_llmClient, &LlmClient::answerReady, this, &AiChatWidget::onLlmAnswerReady);
    connect(m_llmClient, &LlmClient::errorOccurred, this, &AiChatWidget::onNetworkError);

    setupUI();
    applyStyles();
    addSuggestionChips();

    if (m_messagesWidget) {
        m_messagesWidget->installEventFilter(this);
    }

    if (LlmClient::anthropicApiKeyNeedsConfiguration()) {
        addMessage(tr("\u26A0 Clé API non configurée.\n"
                      "Définissez AI_CHAT_API_KEY (ou GROQ_API_KEY / ANTHROPIC_API_KEY) dans votre .env."),
                   false);
    }

    addMessage(tr("Bonjour ! Je peux vous renseigner sur vos clients "
                  "et leurs commandes. Comment puis-je vous aider ?"),
               false);
}

bool AiChatWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_messagesWidget && event->type() == QEvent::Resize) {
        updateBubbleWidths();
    }
    return QWidget::eventFilter(watched, event);
}

void AiChatWidget::updateBubbleWidths()
{
    if (!m_messagesWidget) {
        return;
    }
    const int w = m_messagesWidget->width();
    const int maxB = qMax(160, w * 7 / 10);
    for (int i = 0; i < m_widthTrackedBubbles.size(); ++i) {
        QWidget *bubble = m_widthTrackedBubbles.at(i);
        if (bubble) {
            bubble->setMaximumWidth(maxB);
        }
    }
}

QString AiChatWidget::normalizeSql(QString sql)
{
    sql = sql.trimmed();
    while (sql.endsWith(QLatin1Char(';'))) {
        sql.chop(1);
        sql = sql.trimmed();
    }
    return sql;
}

bool AiChatWidget::isSqlSafe(const QString &sql)
{
    const QString s = sql.toLower();

    // Tables hors périmètre — seules CLIENT, FACTURE (et DUAL pour hors_sujet) sont autorisées
    const QStringList forbidden = {QStringLiteral("clients"),
                                   QStringLiteral("commandes"),
                                   QStringLiteral("stock"),
                                   QStringLiteral("produits"),
                                   QStringLiteral("paiements"),
                                   QStringLiteral("recoltes"),
                                   QStringLiteral("recolte"),
                                   QStringLiteral("fournisseurs"),
                                   QStringLiteral("fournisseur"),
                                   QStringLiteral("employes"),
                                   QStringLiteral("employee"),
                                   QStringLiteral("equipement"),
                                   QStringLiteral("equipements"),
                                   QStringLiteral("users"),
                                   QStringLiteral("machine"),
                                   QStringLiteral("machines"),
                                   QStringLiteral("vente"),
                                   QStringLiteral("ventes")};
    for (const QString &t : forbidden) {
        const QRegularExpression re(
            QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(t)));
        if (s.contains(re)) {
            return false;
        }
    }

    const QStringList dangerous = {QStringLiteral("drop"), QStringLiteral("delete"),
                                   QStringLiteral("update"), QStringLiteral("insert"),
                                   QStringLiteral("alter"), QStringLiteral("create"),
                                   QStringLiteral("truncate")};
    for (const QString &d : dangerous) {
        const QRegularExpression re(
            QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(d)));
        if (s.contains(re)) {
            return false;
        }
    }
    if (s.contains(QStringLiteral("--"))) {
        return false;
    }
    if (s.contains(QLatin1Char(';'))) {
        return false;
    }
    return true;
}

QString AiChatWidget::buildDbContext()
{
    return DatabaseContext::authorizedSchema();
}

AiChatWidget::QuerySnapshot::Kind AiChatWidget::detectSnapshotKind(const QStringList &columnNamesLower)
{
    if (columnNamesLower.size() == 1 && columnNamesLower.first() == QStringLiteral("message")) {
        return QuerySnapshot::Kind::None;
    }
    if (columnNamesLower.contains(QStringLiteral("id_facture"))
        || columnNamesLower.contains(QStringLiteral("type_facture"))
        || columnNamesLower.contains(QStringLiteral("date_facturation"))
        || columnNamesLower.contains(QStringLiteral("id_cmd"))
        || columnNamesLower.contains(QStringLiteral("date_cmd"))) {
        return QuerySnapshot::Kind::Commandes;
    }
    if (columnNamesLower.contains(QStringLiteral("date_enregistrement"))
        || columnNamesLower.contains(QStringLiteral("date_enreg"))
        || (columnNamesLower.contains(QStringLiteral("genre"))
            && !columnNamesLower.contains(QStringLiteral("id_facture")))) {
        return QuerySnapshot::Kind::Clients;
    }
    if (columnNamesLower.contains(QStringLiteral("nom"))
        && columnNamesLower.contains(QStringLiteral("prenom"))
        && columnNamesLower.contains(QStringLiteral("email"))
        && !columnNamesLower.contains(QStringLiteral("id_facture"))
        && !columnNamesLower.contains(QStringLiteral("type_facture"))) {
        return QuerySnapshot::Kind::Clients;
    }
    return QuerySnapshot::Kind::Commandes;
}

void AiChatWidget::fillSnapshotFromQuery(QSqlQuery &query, QuerySnapshot *snap, QString *csvOut)
{
    snap->rows.clear();
    snap->columnNames.clear();
    snap->kind = QuerySnapshot::Kind::None;

    const QSqlRecord rec = query.record();
    QStringList headers;
    QStringList lower;
    for (int i = 0; i < rec.count(); ++i) {
        const QString fn = rec.fieldName(i);
        headers << fn;
        lower << fn.toLower();
    }
    snap->columnNames = headers;
    snap->kind = detectSnapshotKind(lower);

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

void AiChatWidget::setupUI()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("aiChatHeader"));
    auto *headerLay = new QVBoxLayout(header);
    headerLay->setContentsMargins(12, 12, 12, 12);
    auto *title = new QLabel(tr("Assistant Clients"), header);
    title->setObjectName(QStringLiteral("aiChatTitle"));
    auto *subtitle = new QLabel(
        tr("Posez vos questions sur vos clients et leurs commandes"), header);
    subtitle->setObjectName(QStringLiteral("aiChatSubtitle"));
    headerLay->addWidget(title);
    headerLay->addWidget(subtitle);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setObjectName(QStringLiteral("aiChatScroll"));

    m_messagesWidget = new QWidget();
    m_messagesWidget->setObjectName(QStringLiteral("aiChatMessagesInner"));
    m_messagesLayout = new QVBoxLayout(m_messagesWidget);
    m_messagesLayout->setContentsMargins(12, 12, 12, 12);
    m_messagesLayout->setSpacing(10);
    m_messagesLayout->addStretch(1);

    m_scrollArea->setWidget(m_messagesWidget);

    auto *suggestions = new QWidget(this);
    suggestions->setObjectName(QStringLiteral("aiChatSuggestions"));
    m_suggestionsLayout = new QHBoxLayout(suggestions);
    m_suggestionsLayout->setContentsMargins(12, 8, 12, 8);
    m_suggestionsLayout->setSpacing(8);

    auto *bottom = new QWidget(this);
    bottom->setObjectName(QStringLiteral("aiChatBottomBar"));
    auto *bottomLay = new QHBoxLayout(bottom);
    bottomLay->setContentsMargins(8, 8, 8, 8);
    bottomLay->setSpacing(8);

    m_inputField = new QLineEdit(bottom);
    m_inputField->setObjectName(QStringLiteral("aiChatInput"));
    m_inputField->setPlaceholderText(tr("Votre question…"));

    m_sendButton = new QPushButton(bottom);
    m_sendButton->setObjectName(QStringLiteral("aiChatSend"));
    m_sendButton->setFixedSize(32, 32);
    m_sendButton->setIcon(QIcon(makeSendIconPixmap()));
    m_sendButton->setIconSize(QSize(20, 20));

    bottomLay->addWidget(m_inputField, 1);
    bottomLay->addWidget(m_sendButton, 0, Qt::AlignVCenter);

    m_typingIndicator = new QWidget(m_messagesWidget);
    auto *typingLay = new QHBoxLayout(m_typingIndicator);
    typingLay->setContentsMargins(0, 0, 0, 0);
    typingLay->setSpacing(4);
    auto *avatar = new QLabel(m_typingIndicator);
    avatar->setFixedSize(28, 28);
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setText(QStringLiteral("IA"));
    avatar->setObjectName(QStringLiteral("aiTypingAvatar"));
    typingLay->addWidget(avatar, 0, Qt::AlignBottom);
    for (int i = 0; i < 3; ++i) {
        m_typingDots[i] = new QLabel(QStringLiteral("•"), m_typingIndicator);
        m_typingDots[i]->setObjectName(QStringLiteral("aiTypingDot"));
        typingLay->addWidget(m_typingDots[i], 0, Qt::AlignBottom);
    }
    typingLay->addStretch(1);
    m_typingIndicator->hide();

    root->addWidget(header);
    root->addWidget(m_scrollArea, 1);
    root->addWidget(suggestions);
    root->addWidget(bottom);

    connect(m_sendButton, &QPushButton::clicked, this, &AiChatWidget::onSendClicked);
    connect(m_inputField, &QLineEdit::returnPressed, this, &AiChatWidget::onSendClicked);
}

void AiChatWidget::applyStyles()
{
    setStyleSheet(QStringLiteral(
        "QWidget#aiChatHeader { background: %1; border-radius: 10px 10px 0 0; padding: 0; }"
        "QLabel#aiChatTitle { color: %2; font-size: 14px; font-weight: bold; }"
        "QLabel#aiChatSubtitle { color: #C8E6C9; font-size: 11px; }"
        "QScrollArea#aiChatScroll { background: %2; border-left: 1px solid #C8E6C9; border-right: 1px solid "
        "#C8E6C9; border-top: none; border-bottom: none; }"
        "QWidget#aiChatMessagesInner { background: %2; }"
        "QWidget#aiChatSuggestions { background: #F9FBE7; border-top: 1px solid %3; }"
        "QPushButton#aiChatChip { background: white; border: 1px solid %4; border-radius: 14px; padding: "
        "4px 10px; font-size: 11px; color: #2E5016; }"
        "QPushButton#aiChatChip:hover { background: %3; }"
        "QWidget#aiChatBottomBar { background: %2; border: 1px solid #C8E6C9; border-radius: 0 0 10px 10px; "
        "}"
        "QLineEdit#aiChatInput { border: 1px solid #C8E6C9; border-radius: 20px; padding: 6px 14px; background: "
        "#F9FBF9; font-size: 13px; color: %5; }"
        "QPushButton#aiChatSend { background: %6; border: none; border-radius: 16px; padding: 0; }"
        "QPushButton#aiChatSend:hover { background: %7; }"
        "QLabel#aiTypingAvatar { background: %4; border-radius: 14px; color: #1B5E20; font-size: 11px; font-weight: bold; }"
        "QLabel#aiTypingDot { color: %6; font-size: 20px; padding: 0 2px; }")
                      .arg(QLatin1String(kVertFonce),
                           QLatin1String(kBlanc),
                           QLatin1String(kVertPale),
                           QLatin1String(kVertClair),
                           QLatin1String(kTexteSombre),
                           QLatin1String(kVertFonce),
                           QLatin1String(kVertMoyen)));
}

void AiChatWidget::addSuggestionChips()
{
    const QStringList chips = {tr("Clients actifs"),
                               tr("Nouveau client ce mois"),
                               tr("Client le plus fidèle"),
                               tr("Commandes en cours")};
    for (const QString &text : chips) {
        auto *b = new QPushButton(text, this);
        b->setObjectName(QStringLiteral("aiChatChip"));
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, text]() { onSuggestionClicked(text); });
        m_suggestionsLayout->addWidget(b);
    }
    m_suggestionsLayout->addStretch(1);
}

void AiChatWidget::addMessage(const QString &text, bool isUser)
{
    auto *row = new QWidget(m_messagesWidget);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);

    auto *bubble = new QLabel(text, row);
    bubble->setWordWrap(true);
    bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bubble->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    if (isUser) {
        bubble->setStyleSheet(QStringLiteral(
            "QLabel { background: %1; color: white; border-radius: 16px 4px 16px 16px; padding: 10px 14px; "
            "font-size: 13px; }")
                                  .arg(QLatin1String(kVertFonce)));
        h->addStretch(1);
        h->addWidget(bubble, 0, Qt::AlignTop);
        m_widthTrackedBubbles.append(bubble);
    } else {
        auto *av = new QLabel(QStringLiteral("IA"), row);
        av->setFixedSize(28, 28);
        av->setAlignment(Qt::AlignCenter);
        av->setStyleSheet(QStringLiteral(
            "QLabel { background: %1; border-radius: 14px; color: #1B5E20; font-size: 11px; font-weight: bold; }")
                              .arg(QLatin1String(kVertClair)));

        bubble->setStyleSheet(QStringLiteral(
            "QLabel { background: #F1F8E9; border: 1px solid #AED581; border-radius: 4px 16px 16px 16px; "
            "padding: 10px 14px; font-size: 13px; color: #2E5016; }"));
        h->addWidget(av, 0, Qt::AlignTop);
        h->addWidget(bubble, 0, Qt::AlignTop);
        h->addStretch(1);
        m_widthTrackedBubbles.append(bubble);
    }

    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, row);
    updateBubbleWidths();
    scrollToBottom();
}

void AiChatWidget::addErrorMessage(const QString &text)
{
    auto *row = new QWidget(m_messagesWidget);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto *bubble = new QLabel(text, row);
    bubble->setWordWrap(true);
    bubble->setStyleSheet(QStringLiteral(
        "QLabel { background: #FFEBEE; border: 1px solid #E57373; border-radius: 8px; padding: 10px 14px; "
        "font-size: 13px; color: #B71C1C; }"));
    h->addWidget(bubble);
    h->addStretch(1);
    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, row);
    scrollToBottom();
}

void AiChatWidget::addScopeRefusalMessage()
{
    addMessage(tr("Je suis uniquement disponible pour les questions "
                  "sur vos clients et leurs commandes."),
               false);
}

void AiChatWidget::addDataCard(const QuerySnapshot &snapshot)
{
    if (snapshot.rows.isEmpty() || snapshot.kind == QuerySnapshot::Kind::None) {
        return;
    }

    auto *card = new QWidget(m_messagesWidget);
    card->setStyleSheet(QStringLiteral(
        "QWidget { background: #F9FBE7; border: 1px solid #C5E1A5; border-radius: 8px; padding: 10px; }"));

    auto *v = new QVBoxLayout(card);
    v->setContentsMargins(10, 10, 10, 10);

    auto *table = new QTableWidget(card);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setStyleSheet(QStringLiteral(
        "QTableWidget { background: #FFFFFF; font-size: 12px; color: #2E5016; gridline-color: transparent; "
        "border: none; }"
        "QTableWidget::item { padding: 6px; border: none; }"
        "QHeaderView::section { background: #E8F5E9; color: %1; font-weight: 600; border: none; padding: 6px; }")
                             .arg(QLatin1String(kTexteMute)));
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);

    const QStringList cols = snapshot.columnNames;

    if (snapshot.kind == QuerySnapshot::Kind::Commandes) {
        const int iIdCmd = columnIndexOf(cols, {QStringLiteral("ID_CMD"),
                                                QStringLiteral("ID_FACTURE"),
                                                QStringLiteral("id_cmd"),
                                                QStringLiteral("id_facture")});
        const int iIdCli = columnIndexOf(cols, {QStringLiteral("ID_CLIENT"), QStringLiteral("id_client")});
        const int iNom = columnIndexOf(cols, {QStringLiteral("NOM"), QStringLiteral("nom")});
        const int iPre = columnIndexOf(cols, {QStringLiteral("PRENOM"), QStringLiteral("prenom")});
        const int iCin = columnIndexOf(cols, {QStringLiteral("CIN"), QStringLiteral("cin")});
        const int iMail = columnIndexOf(cols, {QStringLiteral("EMAIL"), QStringLiteral("email")});
        const int iTel = columnIndexOf(cols, {QStringLiteral("TELEPHONE"), QStringLiteral("telephone")});
        const int iAchat = columnIndexOf(cols, {QStringLiteral("ACHAT"),
                                                 QStringLiteral("TYPE_FACTURE"),
                                                 QStringLiteral("TYPE_CMD"),
                                                 QStringLiteral("achat"),
                                                 QStringLiteral("achats")});
        int iDate = columnIndexOf(cols, {QStringLiteral("DATE_CMD"),
                                         QStringLiteral("DATE_FACTURATION"),
                                         QStringLiteral("date_cmd")});
        if (iDate < 0) {
            for (int i = 0; i < cols.size(); ++i) {
                if (cols.at(i).toUpper().contains(QStringLiteral("DATE"))) {
                    iDate = i;
                    break;
                }
            }
        }

        table->setColumnCount(9);
        table->setHorizontalHeaderLabels({tr("ID Commande"),
                                          tr("ID Client"),
                                          tr("Nom"),
                                          tr("Prenom"),
                                          tr("CIN"),
                                          tr("Email"),
                                          tr("Telephone"),
                                          tr("Achat"),
                                          tr("Date Cmd")});
        table->setRowCount(snapshot.rows.size());
        for (int r = 0; r < snapshot.rows.size(); ++r) {
            const QStringList &row = snapshot.rows.at(r);
            auto val = [&](int idx) -> QString {
                return (idx >= 0 && idx < row.size()) ? row.at(idx) : QString();
            };
            table->setItem(r, 0, new QTableWidgetItem(val(iIdCmd)));
            table->setItem(r, 1, new QTableWidgetItem(val(iIdCli)));
            table->setItem(r, 2, new QTableWidgetItem(val(iNom)));
            table->setItem(r, 3, new QTableWidgetItem(val(iPre)));
            table->setItem(r, 4, new QTableWidgetItem(val(iCin)));
            table->setItem(r, 5, new QTableWidgetItem(val(iMail)));
            table->setItem(r, 6, new QTableWidgetItem(val(iTel)));
            table->setItem(r, 7, new QTableWidgetItem(val(iAchat)));
            table->setItem(r, 8, new QTableWidgetItem(val(iDate)));
        }
    } else {
        const int iId = columnIndexOf(cols, {QStringLiteral("ID_CLIENT"), QStringLiteral("id_client")});
        const int iNom = columnIndexOf(cols, {QStringLiteral("NOM"), QStringLiteral("nom")});
        const int iPre = columnIndexOf(cols, {QStringLiteral("PRENOM"), QStringLiteral("prenom")});
        const int iMail = columnIndexOf(cols, {QStringLiteral("EMAIL"), QStringLiteral("email")});
        const int iTel = columnIndexOf(cols, {QStringLiteral("TELEPHONE"), QStringLiteral("telephone")});
        const int iCin = columnIndexOf(cols, {QStringLiteral("CIN"), QStringLiteral("cin")});
        const int iDate = columnIndexOf(cols, {QStringLiteral("DATE_ENREGISTREMENT"),
                                               QStringLiteral("DATE_ENREG"),
                                               QStringLiteral("date_enregistrement"),
                                               QStringLiteral("date_enreg")});
        const int iAchats = columnIndexOf(cols, {QStringLiteral("ACHATS"), QStringLiteral("achats")});

        table->setColumnCount(8);
        table->setHorizontalHeaderLabels({tr("ID"),
                                          tr("Nom"),
                                          tr("Prénom"),
                                          tr("Email"),
                                          tr("Téléphone"),
                                          tr("CIN"),
                                          tr("Date d'enreg."),
                                          tr("Achats")});
        table->setRowCount(snapshot.rows.size());
        for (int r = 0; r < snapshot.rows.size(); ++r) {
            const QStringList &row = snapshot.rows.at(r);
            auto val = [&](int idx) -> QString {
                return (idx >= 0 && idx < row.size()) ? row.at(idx) : QString();
            };
            table->setItem(r, 0, new QTableWidgetItem(val(iId)));
            table->setItem(r, 1, new QTableWidgetItem(val(iNom)));
            table->setItem(r, 2, new QTableWidgetItem(val(iPre)));
            table->setItem(r, 3, new QTableWidgetItem(val(iMail)));
            table->setItem(r, 4, new QTableWidgetItem(val(iTel)));
            table->setItem(r, 5, new QTableWidgetItem(val(iCin)));
            table->setItem(r, 6, new QTableWidgetItem(val(iDate)));
            table->setItem(r, 7, new QTableWidgetItem(val(iAchats)));
        }
    }

    v->addWidget(table);

    auto *row = new QWidget(m_messagesWidget);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->addStretch(1);
    h->addWidget(card, 0);
    card->setMaximumWidth(m_messagesWidget ? qMax(280, m_messagesWidget->width() * 9 / 10) : 400);

    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, row);
    scrollToBottom();
}

void AiChatWidget::showTyping(bool visible)
{
    if (!m_typingIndicator || !m_messagesLayout) {
        return;
    }

    if (visible) {
        if (m_messagesLayout->indexOf(m_typingIndicator) < 0) {
            m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, m_typingIndicator);
        }
        m_typingIndicator->show();
        startTypingAnimation();
    } else {
        m_typingIndicator->hide();
        stopTypingAnimation();
    }
    scrollToBottom();
}

void AiChatWidget::startTypingAnimation()
{
    for (int i = 0; i < 3; ++i) {
        if (!m_typingDots[i]) {
            continue;
        }
        if (!m_typingEffects[i]) {
            auto *eff = new QGraphicsOpacityEffect(m_typingDots[i]);
            m_typingDots[i]->setGraphicsEffect(eff);
            m_typingEffects[i] = eff;
        }
        if (!m_typingAnimations[i]) {
            auto *anim = new QPropertyAnimation(m_typingEffects[i], "opacity", this);
            anim->setDuration(900);
            anim->setStartValue(0.15);
            anim->setEndValue(1.0);
            anim->setLoopCount(-1);
            anim->setEasingCurve(QEasingCurve::InOutQuad);
            m_typingAnimations[i] = anim;
        }
        m_typingAnimations[i]->stop();
    }
    for (int i = 0; i < 3; ++i) {
        if (m_typingAnimations[i]) {
            QTimer::singleShot(i * 200, this, [this, i]() {
                if (m_typingAnimations[i]) {
                    m_typingAnimations[i]->start();
                }
            });
        }
    }
}

void AiChatWidget::stopTypingAnimation()
{
    for (int i = 0; i < 3; ++i) {
        if (m_typingAnimations[i]) {
            m_typingAnimations[i]->stop();
        }
        if (m_typingEffects[i]) {
            m_typingEffects[i]->setOpacity(1.0);
        }
    }
}

void AiChatWidget::scrollToBottom()
{
    if (!m_scrollArea) {
        return;
    }
    QTimer::singleShot(0, this, [this]() {
        if (!m_scrollArea || !m_scrollArea->verticalScrollBar()) {
            return;
        }
        m_scrollArea->verticalScrollBar()->setValue(m_scrollArea->verticalScrollBar()->maximum());
    });
}

void AiChatWidget::onSendClicked()
{
    const QString t = m_inputField ? m_inputField->text().trimmed() : QString();
    if (t.isEmpty()) {
        return;
    }

    m_inputField->clear();
    m_pendingUserQuestion = t;
    addMessage(t, true);

    showTyping(true);

    m_llmClient->requestSql(t, buildDbContext());
}

void AiChatWidget::onSuggestionClicked(const QString &text)
{
    if (m_inputField) {
        m_inputField->setText(text);
    }
    onSendClicked();
}

void AiChatWidget::onLlmSqlReady(const QString &sql)
{
    QString dbError;
    if (!ensureDatabaseReady(&dbError)) {
        showTyping(false);
        onNetworkError(tr("Connexion base de données indisponible.\n%1").arg(dbError));
        m_pendingSnapshot = {};
        return;
    }

    QString normalized = normalizeSql(sql);
    if (normalized.toLower().contains(QStringLiteral("hors_sujet"))) {
        m_pendingSnapshot = {};
        m_llmClient->requestAnswer(m_pendingUserQuestion,
                                   QStringLiteral("Aucune donnée SQL exploitable: la requête a été marquée hors sujet."));
        return;
    }

    if (!isSqlSafe(normalized)) {
        m_pendingSnapshot = {};
        m_llmClient->requestAnswer(m_pendingUserQuestion,
                                   QStringLiteral("Aucune donnée SQL exploitable: requête rejetée par les règles de sécurité."));
        return;
    }

    QSqlQuery q(m_db);
    if (!q.exec(normalized)) {
        m_pendingSnapshot = {};
        m_llmClient->requestAnswer(m_pendingUserQuestion,
                                   QStringLiteral("Aucune donnée SQL exploitable: exécution de requête invalide."));
        return;
    }

    QString csv;
    QuerySnapshot snap;
    fillSnapshotFromQuery(q, &snap, &csv);
    m_pendingSnapshot = snap;
    m_llmClient->requestAnswer(m_pendingUserQuestion, csv);
}

bool AiChatWidget::tryLocalFallbackQuery(const QString &question, QuerySnapshot *snap, QString *csvOut)
{
    if (!snap || !csvOut) {
        return false;
    }

    const QString qLower = question.toLower();
    const QString cinToken = extractCinToken(question);
    const QString nameToken = extractNameToken(question);

    QSqlQuery q(m_db);
    bool prepared = false;

    if (qLower.contains(QStringLiteral("nouveau")) && qLower.contains(QStringLiteral("mois"))) {
        prepared = q.prepare(QStringLiteral(
            "SELECT * FROM ( "
            "SELECT ID_CLIENT, NOM, PRENOM, EMAIL, TELEPHONE, CIN, "
            "TO_CHAR(DATE_ENREGISTREMENT,'DD/MM/YYYY') AS DATE_ENREG, ACHATS "
            "FROM CLIENT "
            "WHERE TO_CHAR(DATE_ENREGISTREMENT,'YYYY-MM') = TO_CHAR(SYSDATE,'YYYY-MM') "
            "ORDER BY DATE_ENREGISTREMENT DESC "
            ") WHERE ROWNUM <= 100"));
    } else if (qLower.contains(QStringLiteral("plus fidèle"))
               || qLower.contains(QStringLiteral("plus fidele"))) {
        prepared = q.prepare(QStringLiteral(
            "SELECT * FROM ( "
            "SELECT C.ID_CLIENT, C.NOM, C.PRENOM, C.CIN, COUNT(F.ID_FACTURE) AS NB_COMMANDES "
            "FROM CLIENT C LEFT JOIN FACTURE F ON F.ID_CLIENT = C.ID_CLIENT "
            "GROUP BY C.ID_CLIENT, C.NOM, C.PRENOM, C.CIN "
            "ORDER BY NB_COMMANDES DESC, C.ID_CLIENT DESC "
            ") WHERE ROWNUM <= 1"));
    } else if (qLower.contains(QStringLiteral("en cours"))) {
        prepared = q.prepare(QStringLiteral(
            "SELECT * FROM ( "
            "SELECT TO_CHAR(F.ID_FACTURE) AS ID_CMD, C.ID_CLIENT, C.NOM, C.PRENOM, C.CIN, C.EMAIL, "
            "C.TELEPHONE, F.TYPE_FACTURE AS ACHAT, TO_CHAR(F.DATE_FACTURATION,'DD/MM/YYYY') AS DATE_CMD "
            "FROM FACTURE F JOIN CLIENT C ON C.ID_CLIENT = F.ID_CLIENT "
            "WHERE F.DATE_FACTURATION >= ADD_MONTHS(SYSDATE, -2) "
            "ORDER BY F.DATE_FACTURATION DESC "
            ") WHERE ROWNUM <= 100"));
    } else if (qLower.contains(QStringLiteral("combien"))
               || qLower.contains(QStringLiteral("nombre"))) {
        if (!cinToken.isEmpty()) {
            prepared = q.prepare(QStringLiteral(
                "SELECT C.ID_CLIENT, C.NOM, C.PRENOM, C.CIN, COUNT(F.ID_FACTURE) AS NB_COMMANDES "
                "FROM CLIENT C LEFT JOIN FACTURE F ON F.ID_CLIENT = C.ID_CLIENT "
                "WHERE C.CIN = :cin "
                "GROUP BY C.ID_CLIENT, C.NOM, C.PRENOM, C.CIN "
                "ORDER BY NB_COMMANDES DESC"));
            q.bindValue(QStringLiteral(":cin"), cinToken);
        } else if (!nameToken.isEmpty()) {
            prepared = q.prepare(QStringLiteral(
                "SELECT C.ID_CLIENT, C.NOM, C.PRENOM, C.CIN, COUNT(F.ID_FACTURE) AS NB_COMMANDES "
                "FROM CLIENT C LEFT JOIN FACTURE F ON F.ID_CLIENT = C.ID_CLIENT "
                "WHERE LOWER(C.NOM) LIKE LOWER(:nom) OR LOWER(C.PRENOM) LIKE LOWER(:nom) "
                "GROUP BY C.ID_CLIENT, C.NOM, C.PRENOM, C.CIN "
                "ORDER BY NB_COMMANDES DESC"));
            q.bindValue(QStringLiteral(":nom"), QStringLiteral("%%1%").arg(nameToken));
        }
    }

    if (!prepared) {
        if (!cinToken.isEmpty()) {
            prepared = q.prepare(QStringLiteral(
                "SELECT * FROM ( "
                "SELECT TO_CHAR(F.ID_FACTURE) AS ID_CMD, C.ID_CLIENT, C.NOM, C.PRENOM, C.CIN, C.EMAIL, "
                "C.TELEPHONE, NVL(F.TYPE_FACTURE, C.ACHATS) AS ACHAT, "
                "TO_CHAR(NVL(F.DATE_FACTURATION, C.DATE_ENREGISTREMENT),'DD/MM/YYYY') AS DATE_CMD "
                "FROM CLIENT C LEFT JOIN FACTURE F ON F.ID_CLIENT = C.ID_CLIENT "
                "WHERE C.CIN = :cin "
                "ORDER BY NVL(F.DATE_FACTURATION, C.DATE_ENREGISTREMENT) DESC "
                ") WHERE ROWNUM <= 100"));
            q.bindValue(QStringLiteral(":cin"), cinToken);
        } else if (!nameToken.isEmpty()) {
            prepared = q.prepare(QStringLiteral(
                "SELECT * FROM ( "
                "SELECT TO_CHAR(F.ID_FACTURE) AS ID_CMD, C.ID_CLIENT, C.NOM, C.PRENOM, C.CIN, C.EMAIL, "
                "C.TELEPHONE, NVL(F.TYPE_FACTURE, C.ACHATS) AS ACHAT, "
                "TO_CHAR(NVL(F.DATE_FACTURATION, C.DATE_ENREGISTREMENT),'DD/MM/YYYY') AS DATE_CMD "
                "FROM CLIENT C LEFT JOIN FACTURE F ON F.ID_CLIENT = C.ID_CLIENT "
                "WHERE LOWER(C.NOM) LIKE LOWER(:nom) OR LOWER(C.PRENOM) LIKE LOWER(:nom) "
                "ORDER BY NVL(F.DATE_FACTURATION, C.DATE_ENREGISTREMENT) DESC "
                ") WHERE ROWNUM <= 100"));
            q.bindValue(QStringLiteral(":nom"), QStringLiteral("%%1%").arg(nameToken));
        }
    }

    if (!prepared || !q.exec()) {
        return false;
    }

    fillSnapshotFromQuery(q, snap, csvOut);
    return !snap->rows.isEmpty();
}

bool AiChatWidget::ensureDatabaseReady(QString *errorText)
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

void AiChatWidget::onLlmAnswerReady(const QString &answer)
{
    showTyping(false);
    addMessage(answer, false);

    if (!m_pendingSnapshot.rows.isEmpty() && m_pendingSnapshot.kind != QuerySnapshot::Kind::None) {
        addDataCard(m_pendingSnapshot);
    }
    m_pendingSnapshot = {};
}

void AiChatWidget::onNetworkError(const QString &error)
{
    showTyping(false);

    auto *row = new QWidget(m_messagesWidget);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 4, 0, 4);

    auto *bubble = new QLabel(error, row);
    bubble->setWordWrap(true);
    bubble->setMaximumWidth(500);
    bubble->setStyleSheet(QStringLiteral(
        "background-color: #FFEBEE;"
        "color: #B71C1C;"
        "border: 1px solid #EF9A9A;"
        "border-radius: 4px 16px 16px 16px;"
        "padding: 10px 14px;"
        "font-size: 12px;"));

    rowLayout->addWidget(bubble);
    rowLayout->addStretch();

    if (m_messagesLayout) {
        m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, row);
    }
    scrollToBottom();
}
