#ifndef AICHATWIDGET_H
#define AICHATWIDGET_H

#include <QWidget>

#include <QPropertyAnimation>
#include <QStringList>

#include <QSqlQuery>

class QHBoxLayout;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSqlDatabase;
class QVBoxLayout;
class QLabel;
class QGraphicsOpacityEffect;

class LlmClient;

class AiChatWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AiChatWidget(const QSqlDatabase &db, QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSendClicked();
    void onSuggestionClicked(const QString &text);
    void onLlmSqlReady(const QString &sql);
    void onLlmAnswerReady(const QString &answer);
    void onNetworkError(const QString &error);

private:
    struct QuerySnapshot {
        QStringList columnNames;
        QList<QStringList> rows;
        enum class Kind { None, Commandes, Clients } kind = Kind::None;
    };

    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_messagesWidget = nullptr;
    QVBoxLayout *m_messagesLayout = nullptr;
    QLineEdit *m_inputField = nullptr;
    QPushButton *m_sendButton = nullptr;
    QWidget *m_typingIndicator = nullptr;
    QLabel *m_typingDots[3]{};
    QGraphicsOpacityEffect *m_typingEffects[3]{};
    QPropertyAnimation *m_typingAnimations[3]{};
    QHBoxLayout *m_suggestionsLayout = nullptr;

    QSqlDatabase m_db;
    LlmClient *m_llmClient = nullptr;
    QString m_pendingUserQuestion;
    QuerySnapshot m_pendingSnapshot;

    QVector<QWidget *> m_widthTrackedBubbles;

    void setupUI();
    void applyStyles();
    void addMessage(const QString &text, bool isUser);
    void addErrorMessage(const QString &text);
    void addScopeRefusalMessage();
    void addDataCard(const QuerySnapshot &snapshot);
    void addSuggestionChips();
    void showTyping(bool visible);
    void scrollToBottom();
    QString buildDbContext();
    bool isSqlSafe(const QString &sql);
    void updateBubbleWidths();
    void startTypingAnimation();
    void stopTypingAnimation();
    static QString normalizeSql(QString sql);
    static void fillSnapshotFromQuery(QSqlQuery &query, QuerySnapshot *snap, QString *csvOut);
    static QuerySnapshot::Kind detectSnapshotKind(const QStringList &columnNamesLower);
    bool tryLocalFallbackQuery(const QString &question, QuerySnapshot *snap, QString *csvOut);
    bool ensureDatabaseReady(QString *errorText = nullptr);
};

#endif // AICHATWIDGET_H
