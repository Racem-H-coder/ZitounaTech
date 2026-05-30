#ifndef LLMCLIENT_H
#define LLMCLIENT_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QSslConfiguration>
#include <QString>
#include <QUrl>

class LlmClient : public QObject
{
    Q_OBJECT
public:
    explicit LlmClient(QObject *parent = nullptr);

    void requestSql(const QString &userQuestion, const QString &dbSchema);
    void requestAnswer(const QString &userQuestion, const QString &rawData);

    /** Vrai si la clé dans LlmClient.cpp est encore le placeholder ou vide. */
    static bool anthropicApiKeyNeedsConfiguration();

signals:
    void sqlReady(const QString &sql);
    void answerReady(const QString &answer);
    void errorOccurred(const QString &message);

private:
    enum class Provider { Anthropic, Groq, HuggingFace };

    void postRequest(const QJsonObject &body);
    void handleResponse(QNetworkReply *reply);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_apiKey;
    QString m_currentMode;
    QString m_modelId;
    Provider m_provider = Provider::Anthropic;
};

#endif // LLMCLIENT_H
