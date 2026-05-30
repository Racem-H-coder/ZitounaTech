#ifndef EMPLOYEEAICLIENT_H
#define EMPLOYEEAICLIENT_H

#include <QObject>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

class EmployeeAiClient : public QObject
{
    Q_OBJECT
public:
    explicit EmployeeAiClient(QObject *parent = nullptr);
    void sendQuestion(const QString &question, const QString &contextData);
    bool hasApiKey() const;

signals:
    void responseReady(const QString &answer);
    void requestFailed(const QString &errorMessage);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    QString buildPrompt(const QString &question, const QString &contextData) const;
    QString extractAnswerFromPayload(const QByteArray &payload) const;

    QNetworkAccessManager *m_networkManager = nullptr;
    QUrl m_modelUrl;
};

#endif // EMPLOYEEAICLIENT_H
