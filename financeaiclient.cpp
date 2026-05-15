#include "financeaiclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
const QUrl kDefaultModelUrl(QStringLiteral("https://router.huggingface.co/v1/chat/completions"));
const QString kDefaultModelId = QStringLiteral("Qwen/Qwen2.5-7B-Instruct");
const QUrl kInferenceBaseUrl(QStringLiteral("https://api-inference.huggingface.co/models/"));
constexpr auto kAttrRetryAttempt = QNetworkRequest::User;
constexpr auto kAttrRequestKind = static_cast<QNetworkRequest::Attribute>(QNetworkRequest::User + 1);

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

FinanceAiClient::FinanceAiClient(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_modelUrl(kDefaultModelUrl)
    , m_modelId(kDefaultModelId)
{
    const QString customModelUrl = QString::fromUtf8(qgetenv("HUGGINGFACE_MODEL_URL")).trimmed();
    if (!customModelUrl.isEmpty()) {
        const QUrl url(customModelUrl);
        if (url.isValid() && !url.isEmpty())
            m_modelUrl = url;
    }
    const QString customModelId = QString::fromUtf8(qgetenv("HUGGINGFACE_MODEL_ID")).trimmed();
    if (!customModelId.isEmpty())
        m_modelId = customModelId;
    connect(m_networkManager, &QNetworkAccessManager::finished, this, &FinanceAiClient::onReplyFinished);
}

bool FinanceAiClient::hasApiKey() const
{
    return !qgetenv("HUGGINGFACE_API_KEY").trimmed().isEmpty();
}

void FinanceAiClient::sendQuestion(const QString &question, const QString &contextData)
{
    const QString apiKey = QString::fromUtf8(qgetenv("HUGGINGFACE_API_KEY")).trimmed();
    if (apiKey.isEmpty()) {
        emit requestFailed(tr("Cle API Hugging Face introuvable. Configurez HUGGINGFACE_API_KEY."));
        return;
    }

    const QString prompt = buildPrompt(question, contextData);
    postRouterRequest(apiKey, prompt, false);
}

void FinanceAiClient::onReplyFinished(QNetworkReply *reply)
{
    const QByteArray responseBytes = reply->readAll();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool retryAttempt = reply->request().attribute(kAttrRetryAttempt).toBool();
    if (reply->error() != QNetworkReply::NoError) {
        const QString apiError = extractErrorMessage(responseBytes);
        if (shouldRetryWithInference(statusCode, apiError, retryAttempt)) {
            const QString apiKey = QString::fromUtf8(qgetenv("HUGGINGFACE_API_KEY")).trimmed();
            const QByteArray requestPayload = reply->request().attribute(QNetworkRequest::UserMax).toByteArray();
            postInferenceRequest(apiKey, QString::fromUtf8(requestPayload), true);
            reply->deleteLater();
            return;
        }
        emit requestFailed(toUserFriendlyError(statusCode, apiError, reply->errorString()));
        reply->deleteLater();
        return;
    }

    const QString answer = extractAnswerFromPayload(responseBytes);
    if (answer.isEmpty()) {
        emit requestFailed(tr("Reponse IA vide ou invalide."));
    } else {
        emit responseReady(answer);
    }

    reply->deleteLater();
}

void FinanceAiClient::postRouterRequest(const QString &apiKey, const QString &prompt, bool retryAttempt)
{
    QNetworkRequest request(m_modelUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
    request.setAttribute(kAttrRetryAttempt, retryAttempt);
    request.setAttribute(kAttrRequestKind, QStringLiteral("router"));
    request.setAttribute(QNetworkRequest::UserMax, prompt.toUtf8());

    QJsonArray messages;
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"),
         QStringLiteral("Tu es un assistant financier francophone. Reponds toujours en francais, "
                        "de facon claire, concise, et orientee action. "
                        "Tu reponds uniquement aux sujets de gestion financiere "
                        "(factures, budget, tresorerie, paiements, clients, fournisseurs, marge, rentabilite). "
                        "Si la question est hors finance, redirige poliment sans utiliser de phrase de refus figee.")}});
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), prompt}});

    QJsonObject payload{
        {QStringLiteral("model"), m_modelId},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("temperature"), 0.2},
        {QStringLiteral("max_tokens"), 380},
    };
    m_networkManager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

void FinanceAiClient::postInferenceRequest(const QString &apiKey, const QString &prompt, bool retryAttempt)
{
    const QString encodedModelId = QString::fromUtf8(QUrl::toPercentEncoding(m_modelId));
    const QUrl endpoint = QUrl(kInferenceBaseUrl.toString() + encodedModelId);
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey.toUtf8());
    request.setAttribute(kAttrRetryAttempt, retryAttempt);
    request.setAttribute(kAttrRequestKind, QStringLiteral("inference"));
    request.setAttribute(QNetworkRequest::UserMax, prompt.toUtf8());

    QJsonObject payload{
        {QStringLiteral("inputs"), prompt},
        {QStringLiteral("parameters"),
         QJsonObject{
             {QStringLiteral("max_new_tokens"), 380},
             {QStringLiteral("temperature"), 0.2}
         }},
        {QStringLiteral("options"),
         QJsonObject{
             {QStringLiteral("wait_for_model"), true}
         }}
    };
    m_networkManager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
}

bool FinanceAiClient::shouldRetryWithInference(int statusCode, const QString &apiError, bool retryAttempt) const
{
    if (retryAttempt)
        return false;
    if (statusCode != 401 && statusCode != 403)
        return false;
    const QString err = apiError.toLower();
    return err.contains(QStringLiteral("insufficient permissions"))
           || err.contains(QStringLiteral("authentication method"))
           || err.contains(QStringLiteral("inference providers"));
}

QString FinanceAiClient::toUserFriendlyError(int statusCode, const QString &apiError, const QString &networkError) const
{
    const QString err = apiError.toLower();
    if (statusCode == 401 || statusCode == 403 || err.contains(QStringLiteral("insufficient permissions"))) {
        return tr("L'authentification Hugging Face est refusee pour ce modele/endpoint. "
                  "Utilisez une cle API avec permission Inference, ou changez de modele via HUGGINGFACE_MODEL_ID.");
    }
    if (statusCode == 404) {
        return tr("Le modele IA '%1' est introuvable ou indisponible sur cet endpoint Hugging Face. "
                  "Definissez HUGGINGFACE_MODEL_ID vers un modele disponible.").arg(m_modelId);
    }
    return tr("Impossible de contacter le service IA (HTTP %1). %2").arg(statusCode).arg(networkError);
}

QString FinanceAiClient::buildPrompt(const QString &question, const QString &contextData) const
{
    return QStringLiteral(
               "Tu es un assistant de gestion financiere. Reponds uniquement en francais "
               "en t'appuyant sur les donnees factures, clients et fournisseurs. "
               "Donne des recommandations concretes, courtes et applicables. "
               "Si la question ne concerne pas la gestion financiere, redirige poliment "
               "vers une question financiere pertinente, sans phrase de refus figee.\n"
               "Contexte:\n%1\n\nQuestion: %2")
        .arg(contextData, question);
}

QString FinanceAiClient::extractAnswerFromPayload(const QByteArray &payload) const
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return QString::fromUtf8(payload).trimmed();

    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        if (!arr.isEmpty() && arr.first().isObject()) {
            const QString generated = arr.first().toObject().value(QStringLiteral("generated_text")).toString().trimmed();
            if (!generated.isEmpty())
                return generated;
        }
    } else if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        if (obj.contains(QStringLiteral("error")))
            return QString();

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
