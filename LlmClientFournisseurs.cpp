#include "LlmClientFournisseurs.h"

#include <QJsonValue>
#include <QSslError>

namespace {

QString resolveFournisseurApiKey()
{
    const QString key = QString::fromUtf8(qgetenv("FOURNISSEUR_AI_API_KEY")).trimmed();
    if (!key.isEmpty()) {
        return key;
    }
    const QString shared = QString::fromUtf8(qgetenv("AI_CHAT_API_KEY")).trimmed();
    if (!shared.isEmpty()) {
        return shared;
    }
    const QString hf = QString::fromUtf8(qgetenv("HUGGINGFACE_API_KEY")).trimmed();
    if (!hf.isEmpty()) {
        return hf;
    }
    return QString::fromUtf8(qgetenv("GROQ_API_KEY")).trimmed();
}

QString sqlPrompt(const QString &schema)
{
    return QStringLiteral(
               "Tu es un expert SQL Oracle pour la gestion des fournisseurs.\n\n"
               "Schéma AUTORISÉ (UNE SEULE table) :\n"
               "%1\n"
               "Règles absolues:\n"
               "1) Réponds uniquement avec du SQL brut Oracle.\n"
               "2) Interdiction d'utiliser une table autre que FOURNISSEUR.\n"
               "3) Si hors sujet, retourne exactement: SELECT 'hors_sujet' AS message FROM DUAL\n"
               "4) Utilise LOWER(...) LIKE LOWER('%%mot%%') pour la recherche texte.\n"
               "5) Limite les résultats avec ROWNUM <= 100.\n"
               "6) Formate les dates avec TO_CHAR(DATE_AJOUT,'DD/MM/YYYY').\n"
               "7) Colonnes de référence: ID_FOURNISSEUR, NOM, REGION, EMAIL, NATURE, DATE_AJOUT, "
               "STATUS_FOURNISSEUR, TELEPHONE, TYPE.\n"
               "8) N'utilise TYPE que s'il existe; privilégie NATURE.\n"
               "9) Pas de markdown, pas de backticks, pas de point-virgule.")
        .arg(schema);
}

QString answerPrompt()
{
    return QStringLiteral(
        "Tu es l'assistant fournisseurs d'une huilerie tunisienne.\n"
        "Réponds en français, clair et concis.\n"
        "Reste centré sur les fournisseurs, mais évite les refus automatiques figés.\n"
        "Si la question est hors périmètre, redirige poliment vers un sujet fournisseur pertinent.\n"
        "Si la question demande une procédure, donne des étapes concrètes dans l'application.\n"
        "Si données vides: indiquer qu'aucun résultat n'a été trouvé.");
}

QString errorMessageFromObj(const QJsonObject &obj)
{
    const QJsonValue e = obj.value(QStringLiteral("error"));
    if (e.isObject()) {
        return e.toObject().value(QStringLiteral("message")).toString();
    }
    if (e.isString()) {
        return e.toString();
    }
    return QString();
}

} // namespace

LlmClientFournisseurs::LlmClientFournisseurs(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_apiKey(resolveFournisseurApiKey())
    , m_modelId(QString::fromUtf8(qgetenv("HUGGINGFACE_MODEL_ID")).trimmed())
{
    if (m_modelId.isEmpty()) {
        m_modelId = QStringLiteral("Qwen/Qwen2.5-7B-Instruct");
    }
    if (m_apiKey.startsWith(QStringLiteral("hf_"))) {
        m_provider = Provider::HuggingFace;
    } else {
        m_provider = Provider::Groq;
    }
}

void LlmClientFournisseurs::requestSql(const QString &userQuestion, const QString &dbSchema)
{
    m_mode = QStringLiteral("sql");

    QJsonObject body;
    body[QStringLiteral("model")] =
        (m_provider == Provider::HuggingFace) ? m_modelId : QStringLiteral("llama-3.3-70b-versatile");
    body[QStringLiteral("max_tokens")] = 1024;
    body[QStringLiteral("temperature")] = 0.2;

    QJsonArray messages;
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), sqlPrompt(dbSchema)},
    });
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), userQuestion},
    });
    body[QStringLiteral("messages")] = messages;

    postRequest(body);
}

void LlmClientFournisseurs::requestAnswer(const QString &userQuestion, const QString &rawData)
{
    m_mode = QStringLiteral("answer");

    const QString userContent = QStringLiteral("Question : %1\n\nDonnées brutes :\n%2")
                                    .arg(userQuestion, rawData);

    QJsonObject body;
    body[QStringLiteral("model")] =
        (m_provider == Provider::HuggingFace) ? m_modelId : QStringLiteral("llama-3.3-70b-versatile");
    body[QStringLiteral("max_tokens")] = 1024;
    body[QStringLiteral("temperature")] = 0.2;

    QJsonArray messages;
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), answerPrompt()},
    });
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), userContent},
    });
    body[QStringLiteral("messages")] = messages;

    postRequest(body);
}

void LlmClientFournisseurs::postRequest(const QJsonObject &body)
{
    if (m_apiKey.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral(
            "Clé API fournisseur introuvable. Configurez FOURNISSEUR_AI_API_KEY (ou AI_CHAT_API_KEY / GROQ_API_KEY)."));
        return;
    }

    const QUrl endpoint =
        (m_provider == Provider::HuggingFace)
            ? QUrl(QStringLiteral("https://router.huggingface.co/v1/chat/completions"))
            : QUrl(QStringLiteral("https://api.groq.com/openai/v1/chat/completions"));
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
    request.setRawHeader("Accept", QByteArrayLiteral("application/json"));
    request.setRawHeader("User-Agent", QByteArrayLiteral("ZitounaTech-Qt/6"));
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    request.setSslConfiguration(sslConfig);

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &errors) {
        QString sslMsg;
        for (const QSslError &e : errors) {
            sslMsg += e.errorString() + QLatin1Char('\n');
        }
        reply->setProperty("sslErrorMsg", sslMsg);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleResponse(reply);
    });
}

void LlmClientFournisseurs::handleResponse(QNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        QString detail = QStringLiteral("Erreur réseau : %1\nCode : %2")
                             .arg(reply->errorString())
                             .arg(httpCode);
        const QByteArray errBody = reply->readAll();
        if (!errBody.isEmpty()) {
            const QJsonDocument d = QJsonDocument::fromJson(errBody);
            if (d.isObject()) {
                const QString api = errorMessageFromObj(d.object());
                if (!api.isEmpty()) {
                    detail += QStringLiteral("\n\nDétail API : ") + api;
                }
            }
        }
        emit errorOccurred(detail);
        reply->deleteLater();
        return;
    }

    const QByteArray data = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit errorOccurred(QStringLiteral("Réponse JSON invalide."));
        reply->deleteLater();
        return;
    }

    const QJsonObject obj = doc.object();
    const QString errMsg = errorMessageFromObj(obj);
    if (!errMsg.isEmpty()) {
        emit errorOccurred(QStringLiteral("Erreur API Groq :\n") + errMsg);
        reply->deleteLater();
        return;
    }

    const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        emit errorOccurred(QStringLiteral("Réponse vide reçue de l'API."));
        reply->deleteLater();
        return;
    }

    QString text = choices.at(0).toObject().value(QStringLiteral("message")).toObject().value(
        QStringLiteral("content")).toString().trimmed();
    if (text.isEmpty()) {
        emit errorOccurred(QStringLiteral("Texte vide dans la réponse API."));
        reply->deleteLater();
        return;
    }

    if (m_mode == QStringLiteral("sql")) {
        text.remove(QStringLiteral("```sql"));
        text.remove(QStringLiteral("```"));
        text.remove(QLatin1Char('`'));
        text = text.trimmed();
        emit sqlReady(text);
    } else {
        emit answerReady(text);
    }
    reply->deleteLater();
}

