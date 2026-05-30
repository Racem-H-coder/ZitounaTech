#ifndef FOURNISSEURCHATWIDGET_H
#define FOURNISSEURCHATWIDGET_H

#include <QPropertyAnimation>
#include <QSqlQuery>
#include <QWidget>

class LlmClientFournisseurs;
class QGraphicsOpacityEffect;
class QHBoxLayout;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSqlDatabase;
class QVBoxLayout;
class QLabel;

class FournisseurChatWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FournisseurChatWidget(const QSqlDatabase &db, QWidget *parent = nullptr);

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
    LlmClientFournisseurs *m_llm = nullptr;
    QString m_pendingQuestion;
    QuerySnapshot m_pendingSnapshot;

    void setupUi();
    void applyStyles();
    void addMessage(const QString &text, bool isUser);
    void addDataCard(const QuerySnapshot &snapshot);
    void addSuggestions();
    void showTyping(bool visible);
    void scrollToBottom();
    bool isSqlSafe(const QString &sql) const;
    QString buildDbContext() const;
    bool tryLocalFallbackQuery(const QString &question, QuerySnapshot *snap, QString *csvOut);
    bool ensureDatabaseReady(QString *errorText = nullptr);
    static QString normalizeSql(QString sql);
    static void fillSnapshotAndCsv(QSqlQuery &query, QuerySnapshot *snap, QString *csvOut);
};

#endif // FOURNISSEURCHATWIDGET_H
