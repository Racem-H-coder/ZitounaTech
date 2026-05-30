#ifndef LLMCLIENTFOURNISSEURS_H
#define LLMCLIENTFOURNISSEURS_H

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

class LlmClientFournisseurs : public QObject
{
    Q_OBJECT
public:
    explicit LlmClientFournisseurs(QObject *parent = nullptr);

    void requestSql(const QString &userQuestion, const QString &dbSchema);
    void requestAnswer(const QString &userQuestion, const QString &rawData);

signals:
    void sqlReady(const QString &sql);
    void answerReady(const QString &answer);
    void errorOccurred(const QString &message);

private:
    enum class Provider { Groq, HuggingFace };

    void postRequest(const QJsonObject &body);
    void handleResponse(QNetworkReply *reply);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_apiKey;
    QString m_mode;
    QString m_modelId;
    Provider m_provider = Provider::Groq;
};

#endif // LLMCLIENTFOURNISSEURS_H
