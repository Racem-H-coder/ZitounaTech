#ifndef EMPLOYEEASSISTANTWIDGET_H
#define EMPLOYEEASSISTANTWIDGET_H

#include <QWidget>

class EmployeeAiClient;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class EmployeeAssistantWidget : public QWidget
{
    Q_OBJECT
public:
    explicit EmployeeAssistantWidget(QWidget *parent = nullptr);

private slots:
    void sendMessage();
    void handleAiResponse(const QString &answer);
    void handleAiError(const QString &errorMessage);
    void onQuickSuggestionClicked();

private:
    void setupUi();
    void appendMessage(const QString &author, const QString &message);
    QString buildEmployeeContext() const;
    QString buildFallbackResponse(const QString &question) const;
    void setTypingIndicatorVisible(bool visible);
    void loadHistory();
    void saveHistory() const;
    static QString normalizeQuestion(const QString &question);

    QListWidget *m_chatList = nullptr;
    QLineEdit *m_input = nullptr;
    QLabel *m_typingLabel = nullptr;
    EmployeeAiClient *m_aiClient = nullptr;
    QString m_lastUserQuestion;
};

#endif // EMPLOYEEASSISTANTWIDGET_H
