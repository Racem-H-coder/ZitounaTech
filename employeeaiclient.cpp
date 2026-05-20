#include "employeeaiclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
const QUrl kDefaultModelUrl(QStringLiteral("https://router.huggingface.co/v1/chat/completions"));
const QString kDefaultModelId = QStringLiteral("Qwen/Qwen2.5-7B-Instruct");

QString extractErrorMessage(const QByteArray &payload)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return QString::fromUtf8(payload).trimmed();

    const QJsonObject obj = doc.object();
    const QString error = obj.value(QStringLiteral("error")).toString().trimmed();
    if (!error.isEmpty())
        return error;
    return QString::fromUtf8(payload).trimmed();
}
}

EmployeeAiClient::EmployeeAiClient(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_modelUrl(kDefaultModelUrl)
{
    const QString customModelUrl = QString::fromUtf8(qgetenv("HUGGINGFACE_MODEL_URL")).trimmed();
    if (!customModelUrl.isEmpty()) {
        const QUrl url(customModelUrl);
        if (url.isValid() && !url.isEmpty())
            m_modelUrl = url;
    }
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &EmployeeAiClient::onReplyFinished);
}

bool EmployeeAiClient::hasApiKey() const
{
    return !qgetenv("HUGGINGFACE_API_KEY").trimmed().isEmpty();
}

void EmployeeAiClient::sendQuestion(const QString &question, const QString &contextData)
{
    const QString apiKey = QString::fromUtf8(qgetenv("HUGGINGFACE_API_KEY")).trimmed();
    if (apiKey.isEmpty()) {
        emit requestFailed(tr("Clé API Hugging Face introuvable. Configurez HUGGINGFACE_API_KEY."));
        return;
    }

    QNetworkRequest request(m_modelUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());

    const QString prompt = buildPrompt(question, contextData);
    QJsonArray messages;
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"),
         QStringLiteral("Tu es un assistant RH francophone. Réponds toujours en français, de façon claire, "
                        "professionnelle et concise.")}});
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), prompt}});

    QJsonObject payload{
        {QStringLiteral("model"), kDefaultModelId},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("temperature"), 0.2},
        {QStringLiteral("max_tokens"), 320},
    };

    const QByteArray data = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    m_networkManager->post(request, data);
}

void EmployeeAiClient::onReplyFinished(QNetworkReply *reply)
{
    const QByteArray responseBytes = reply->readAll();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        const QString apiError = extractErrorMessage(responseBytes);
        QString error = tr("Erreur réseau (HTTP %1): %2").arg(statusCode).arg(reply->errorString());
        if (!apiError.isEmpty())
            error += tr("\nDétail API: %1").arg(apiError);
        emit requestFailed(error);
        reply->deleteLater();
        return;
    }

    const QString answer = extractAnswerFromPayload(responseBytes);
    if (answer.isEmpty()) {
        emit requestFailed(tr("Réponse IA vide ou invalide."));
    } else {
        emit responseReady(answer);
    }

    reply->deleteLater();
}

QString EmployeeAiClient::buildPrompt(const QString &question, const QString &contextData) const
{
    return QStringLiteral(
               "Tu es un assistant RH. Réponds uniquement en français en t'appuyant sur les données de gestion "
               "des employés. Utilise des puces courtes et actionnables si pertinent.\n"
               "Contexte:\n%1\n\nQuestion: %2")
        .arg(contextData, question);
}

QString EmployeeAiClient::extractAnswerFromPayload(const QByteArray &payload) const
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return QString::fromUtf8(payload).trimmed();
    }
    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        if (!arr.isEmpty() && arr.first().isObject()) {
            const QString generated = arr.first().toObject().value(QStringLiteral("generated_text")).toString().trimmed();
            if (!generated.isEmpty())
                return generated;
        }
    } else if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("error"))) {
            return QString();
        }
        const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
        if (!choices.isEmpty()) {
            const QJsonObject c0 = choices.first().toObject();
            const QJsonObject msg = c0.value(QStringLiteral("message")).toObject();
            const QString content = msg.value(QStringLiteral("content")).toString().trimmed();
            if (!content.isEmpty())
                return content;
        }
        const QString generated = obj.value(QStringLiteral("generated_text")).toString().trimmed();
        if (!generated.isEmpty())
            return generated;
    }

    return QString::fromUtf8(payload).trimmed();
}
