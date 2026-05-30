#ifndef FINANCEAICLIENT_H
#define FINANCEAICLIENT_H

#include <QObject>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

class FinanceAiClient : public QObject
{
    Q_OBJECT
public:
    explicit FinanceAiClient(QObject *parent = nullptr);
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
    void postRouterRequest(const QString &apiKey, const QString &prompt, bool retryAttempt);
    void postInferenceRequest(const QString &apiKey, const QString &prompt, bool retryAttempt);
    bool shouldRetryWithInference(int statusCode, const QString &apiError, bool retryAttempt) const;
    QString toUserFriendlyError(int statusCode, const QString &apiError, const QString &networkError) const;

    QNetworkAccessManager *m_networkManager = nullptr;
    QUrl m_modelUrl;
    QString m_modelId;
};

#endif // FINANCEAICLIENT_H
