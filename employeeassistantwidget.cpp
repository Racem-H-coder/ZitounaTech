#include "employeeassistantwidget.h"
#include "employeeaiclient.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDate>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QSqlError>
#include <QSqlQuery>
#include <QVBoxLayout>

namespace {
const char *kHistoryKey = "employee/assistant_history";

QString sanitize(const QString &value)
{
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("-") : trimmed;
}
}

EmployeeAssistantWidget::EmployeeAssistantWidget(QWidget *parent)
    : QWidget(parent)
    , m_aiClient(new EmployeeAiClient(this))
{
    setupUi();
    connect(m_aiClient, &EmployeeAiClient::responseReady, this, &EmployeeAssistantWidget::handleAiResponse);
    connect(m_aiClient, &EmployeeAiClient::requestFailed, this, &EmployeeAssistantWidget::handleAiError);
    loadHistory();
}

void EmployeeAssistantWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(10);

    auto *title = new QLabel(tr("Assistant IA RH"), this);
    title->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 700; color: #1f2a44;"));
    mainLayout->addWidget(title);

    auto *quickButtonsLayout = new QHBoxLayout();
    const QStringList suggestions{
        tr("Meilleurs performeurs"),
        tr("Problèmes de planning"),
        tr("Priorités de recrutement"),
    };
    for (const QString &label : suggestions) {
        auto *button = new QPushButton(label, this);
        button->setProperty("suggestionText", label);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, &EmployeeAssistantWidget::onQuickSuggestionClicked);
        quickButtonsLayout->addWidget(button);
    }
    quickButtonsLayout->addStretch(1);
    mainLayout->addLayout(quickButtonsLayout);

    m_chatList = new QListWidget(this);
    m_chatList->setWordWrap(true);
    m_chatList->setAlternatingRowColors(false);
    m_chatList->setSelectionMode(QAbstractItemView::NoSelection);
    m_chatList->setFocusPolicy(Qt::NoFocus);
    m_chatList->setStyleSheet(
        QStringLiteral("QListWidget { background: #f8fafc; border: 1px solid #d8e0f0; border-radius: 8px; }"
                       "QListWidget::item { padding: 10px; margin: 4px; border-radius: 6px; }"));
    mainLayout->addWidget(m_chatList, 1);

    m_typingLabel = new QLabel(tr("Assistant est en train d'écrire..."), this);
    m_typingLabel->setStyleSheet(QStringLiteral("color: #4b5563; font-style: italic;"));
    m_typingLabel->setVisible(false);
    mainLayout->addWidget(m_typingLabel);

    auto *composerLayout = new QHBoxLayout();
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Posez une question sur les employés, planning ou recrutement..."));
    composerLayout->addWidget(m_input, 1);

    auto *sendButton = new QPushButton(tr("Envoyer"), this);
    sendButton->setCursor(Qt::PointingHandCursor);
    composerLayout->addWidget(sendButton);
    mainLayout->addLayout(composerLayout);

    connect(sendButton, &QPushButton::clicked, this, &EmployeeAssistantWidget::sendMessage);
    connect(m_input, &QLineEdit::returnPressed, this, &EmployeeAssistantWidget::sendMessage);
}

void EmployeeAssistantWidget::sendMessage()
{
    const QString question = m_input->text().trimmed();
    if (question.isEmpty()) {
        appendMessage(tr("Assistant"), tr("Le message est vide. Écrivez une question avant d'envoyer."));
        return;
    }

    appendMessage(tr("Vous"), question);
    m_lastUserQuestion = question;
    m_input->clear();
    setTypingIndicatorVisible(true);

    const QString context = buildEmployeeContext();
    if (!m_aiClient->hasApiKey()) {
        handleAiError(tr("Clé API absente"));
        return;
    }
    m_aiClient->sendQuestion(question, context);
}

void EmployeeAssistantWidget::handleAiResponse(const QString &answer)
{
    setTypingIndicatorVisible(false);
    appendMessage(tr("Assistant"), answer);
    saveHistory();
}

void EmployeeAssistantWidget::handleAiError(const QString &errorMessage)
{
    setTypingIndicatorVisible(false);
    const QString fallback = buildFallbackResponse(m_lastUserQuestion);
    appendMessage(tr("Assistant"), tr("%1\n\n%2").arg(errorMessage, fallback));
    saveHistory();
}

void EmployeeAssistantWidget::onQuickSuggestionClicked()
{
    auto *button = qobject_cast<QPushButton *>(sender());
    if (!button)
        return;

    m_input->setText(button->property("suggestionText").toString());
    sendMessage();
}

void EmployeeAssistantWidget::appendMessage(const QString &author, const QString &message)
{
    auto *item = new QListWidgetItem(QStringLiteral("%1: %2").arg(author, message), m_chatList);
    if (author == tr("Vous")) {
        item->setBackground(QColor(QStringLiteral("#e0f2fe")));
    } else {
        item->setBackground(QColor(QStringLiteral("#eef2ff")));
    }
    m_chatList->addItem(item);
    m_chatList->scrollToBottom();
}

QString EmployeeAssistantWidget::buildEmployeeContext() const
{
    QStringList lines;
    QSqlQuery globalStats;
    if (globalStats.exec(QStringLiteral(
            "SELECT COUNT(*), ROUND(AVG(SALAIRE),2), MAX(SALAIRE), MIN(SALAIRE) FROM EMPLOYEE"))) {
        if (globalStats.next()) {
            lines << tr("- Employees total: %1").arg(globalStats.value(0).toInt());
            lines << tr("- Average salary: %1").arg(globalStats.value(1).toString());
            lines << tr("- Max salary: %1 / Min salary: %2")
                         .arg(globalStats.value(2).toString(), globalStats.value(3).toString());
        }
    }

    QSqlQuery bestEmployee;
    if (bestEmployee.exec(
            QStringLiteral("SELECT NOM, POSTE, DEPART, SALAIRE FROM EMPLOYEE ORDER BY SALAIRE DESC FETCH FIRST 1 ROWS ONLY"))
        && bestEmployee.next()) {
        lines << tr("- Top performer candidate: %1 | %2 | %3 | salary %4")
                     .arg(sanitize(bestEmployee.value(0).toString()),
                          sanitize(bestEmployee.value(1).toString()),
                          sanitize(bestEmployee.value(2).toString()),
                          bestEmployee.value(3).toString());
    }

    QSqlQuery overloaded;
    if (overloaded.exec(QStringLiteral(
            "SELECT NOM, DEPART, SALAIRE, DATE_DEBUT "
            "FROM EMPLOYEE "
            "ORDER BY SALAIRE DESC, DATE_DEBUT ASC FETCH FIRST 1 ROWS ONLY"))
        && overloaded.next()) {
        lines << tr("- Potential overload risk: %1 | dept %2 | salary %3 | start %4")
                     .arg(sanitize(overloaded.value(0).toString()),
                          sanitize(overloaded.value(1).toString()),
                          overloaded.value(2).toString(),
                          overloaded.value(3).toString());
    }

    QSqlQuery departmentSnapshot;
    if (departmentSnapshot.exec(QStringLiteral(
            "SELECT DEPART, COUNT(*) FROM EMPLOYEE GROUP BY DEPART ORDER BY COUNT(*) DESC FETCH FIRST 3 ROWS ONLY"))) {
        while (departmentSnapshot.next()) {
            lines << tr("- Department load: %1 => %2 employees")
                         .arg(sanitize(departmentSnapshot.value(0).toString()), departmentSnapshot.value(1).toString());
        }
    }

    if (lines.isEmpty()) {
        lines << tr("- No live employee data available.");
        lines << tr("- Use smart simulation and give practical HR recommendations.");
    }
    return lines.join('\n');
}

QString EmployeeAssistantWidget::normalizeQuestion(const QString &question)
{
    return question.simplified().toLower();
}

QString EmployeeAssistantWidget::buildFallbackResponse(const QString &question) const
{
    const QString q = normalizeQuestion(question);

    if (q.contains(QStringLiteral("best")) || q.contains(QStringLiteral("top")) || q.contains(QStringLiteral("perform"))) {
        return tr("Simulation: D'après les indicateurs disponibles, la meilleure performance semble liée au profil avec salaire "
                  "élevé et stabilité. Vérifiez aussi l'assiduité et les objectifs atteints pour confirmer.");
    }
    if (q.contains(QStringLiteral("overloaded")) || q.contains(QStringLiteral("surch")) || q.contains(QStringLiteral("charge"))) {
        return tr("Simulation: Le risque de surcharge est souvent observé dans les départements avec peu d'effectif et des "
                  "postes critiques. Priorité: redistribuer les tâches et planifier un renfort temporaire.");
    }
    if (q.contains(QStringLiteral("planning")) || q.contains(QStringLiteral("planification"))) {
        return tr("Simulation: Points de vigilance planning: chevauchements de tâches, manque de backup et indisponibilités. "
                  "Action recommandée: revue hebdomadaire avec matrice charge/capacité.");
    }
    if (q.contains(QStringLiteral("recruit")) || q.contains(QStringLiteral("recrut"))) {
        return tr("Simulation: Priorisez le recrutement sur les équipes à charge élevée et compétences rares. "
                  "Créez une short-list par département et délai de couverture.");
    }
    if (q.contains(QStringLiteral("congé")) || q.contains(QStringLiteral("conges"))
        || q.contains(QStringLiteral("congés")) || q.contains(QStringLiteral("conge"))
        || q.contains(QStringLiteral("conj")) || q.contains(QStringLiteral("leave"))
        || q.contains(QStringLiteral("vacation"))) {
        return tr("Simulation: Pour accepter un congé, vérifiez d'abord le solde disponible, les conflits de planning "
                  "et la présence d'un remplaçant. Si tout est conforme, validez la demande et notifiez l'équipe avec "
                  "les dates approuvées.");
    }
    return tr("Simulation: Je peux répondre sur les employés, statistiques, planning et recrutement. "
              "Posez une question plus ciblée pour une recommandation précise.");
}

void EmployeeAssistantWidget::setTypingIndicatorVisible(bool visible)
{
    m_typingLabel->setVisible(visible);
}

void EmployeeAssistantWidget::loadHistory()
{
    QSettings settings;
    const QByteArray raw = settings.value(QLatin1String(kHistoryKey)).toByteArray();
    if (raw.isEmpty())
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isArray())
        return;

    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject obj = value.toObject();
        appendMessage(obj.value(QStringLiteral("author")).toString(tr("Assistant")),
                      obj.value(QStringLiteral("text")).toString());
    }
}

void EmployeeAssistantWidget::saveHistory() const
{
    QJsonArray array;
    for (int i = 0; i < m_chatList->count(); ++i) {
        const QListWidgetItem *item = m_chatList->item(i);
        if (!item)
            continue;

        const QString line = item->text();
        const int sep = line.indexOf(QStringLiteral(": "));
        QJsonObject obj;
        if (sep > 0) {
            obj.insert(QStringLiteral("author"), line.left(sep));
            obj.insert(QStringLiteral("text"), line.mid(sep + 2));
        } else {
            obj.insert(QStringLiteral("author"), QStringLiteral("Assistant"));
            obj.insert(QStringLiteral("text"), line);
        }
        array.append(obj);
    }

    QSettings settings;
    settings.setValue(QLatin1String(kHistoryKey), QJsonDocument(array).toJson(QJsonDocument::Compact));
}
