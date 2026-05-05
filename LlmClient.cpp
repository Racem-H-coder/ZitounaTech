#include "LlmClient.h"

#include <QSslError>

/* ÉTAPES MANUELLES REQUISES :
   1. Copier dans le dossier de l'exe :
      - libssl-3-x64.dll
      - libcrypto-3-x64.dll
      Depuis : C:/Qt/6.x.x/mingw_64/bin/

   2. Remplacer la clé API ci-dessous par votre clé Anthropic (console.anthropic.com → API Keys).

   3. Si supportsSsl() est false au démarrage, l'application quitte avec un message explicite (voir main.cpp).
*/

namespace {

QString resolveApiKey()
{
    const QString chatKey = QString::fromUtf8(qgetenv("AI_CHAT_API_KEY")).trimmed();
    if (!chatKey.isEmpty()) {
        return chatKey;
    }
    const QString groqKey = QString::fromUtf8(qgetenv("GROQ_API_KEY")).trimmed();
    if (!groqKey.isEmpty()) {
        return groqKey;
    }
    const QString hfKey = QString::fromUtf8(qgetenv("HUGGINGFACE_API_KEY")).trimmed();
    if (!hfKey.isEmpty()) {
        return hfKey;
    }
    return QString::fromUtf8(qgetenv("ANTHROPIC_API_KEY")).trimmed();
}

QString sqlSystemPrompt(const QString &dbSchema)
{
    return QStringLiteral(
               "Tu es un expert SQL Oracle pour une application de gestion de clients "
               "d'une huilerie d'olives.\n\n"
               "Schéma AUTORISÉ (tables UNIQUEMENT) :\n"
               "%1\n"
               "Règles ABSOLUES :\n"
               "1. Réponds UNIQUEMENT avec la requête SQL brute Oracle, rien d'autre.\n"
               "2. Pas de markdown, pas d'explication, pas de backticks.\n"
               "3. Tu n'as le droit d'interroger QUE les tables CLIENT et FACTURE "
               "(aliases C et F autorisés). Toute autre table est interdite.\n"
               "4. Pour les recherches de noms : LOWER(C.NOM) LIKE LOWER('%mot%') "
               "ou LOWER(C.PRENOM) LIKE LOWER('%mot%').\n"
               "5. Limite toujours à 100 lignes : ajoute FETCH FIRST 100 ROWS ONLY "
               "à la fin (Oracle 12+) ou encapsule avec SELECT * FROM (…) WHERE ROWNUM <= 100.\n"
               "6. Formate les dates affichées avec TO_CHAR(d,'DD/MM/YYYY').\n"
               "7. Si la question ne concerne pas les clients ou leurs commandes/factures, "
               "retourne exactement : SELECT 'hors_sujet' AS message FROM DUAL\n"
               "8. Si un nom de client est mentionné pour des commandes : "
               "JOIN FACTURE F ON F.ID_CLIENT = C.ID_CLIENT "
               "JOIN CLIENT C ON … (même jointure), filtre sur C.NOM / C.PRENOM.\n"
               "9. Pour lister des commandes : affiche au minimum "
               "F.ID_FACTURE, TO_CHAR(F.DATE_FACTURATION,'DD/MM/YYYY'), "
               "F.TYPE_FACTURE, F.QUANTITE, C.NOM, C.PRENOM, C.ID_CLIENT.\n"
               "10. N'inclus pas PRIX_UNITAIRE ni montants dans le SELECT sauf si "
               "l'utilisateur demande explicitement un volume ou un type ; "
               "évite d'afficher des prix dans les colonnes renvoyées.\n"
               "11. Pas de point-virgule en fin de requête.")
        .arg(dbSchema);
}

QString answerSystemPrompt()
{
    return QStringLiteral(
        "Tu es l'assistant IA d'une huilerie d'olives tunisienne, "
        "spécialisé dans la gestion des clients.\n\n"
        "Règles de réponse :\n"
        "1. Réponds en français, de façon concise et professionnelle.\n"
        "2. Tu restes centré sur les clients et leurs commandes / factures, "
        "mais évite les refus automatiques figés.\n"
        "3. Si la question demande une procédure (ex: comment ajouter/modifier/supprimer un client), "
        "donne des étapes pratiques dans l'application (module Clients, onglet Ajouter / Liste, "
        "remplir formulaire, enregistrer, vérifier la liste).\n"
        "4. N'invente jamais un service commercial externe ni un renvoi hors application.\n"
        "5. Si la question est hors périmètre, redirige poliment en une phrase vers un angle client pertinent.\n"
        "6. Si des données sont fournies, commence par un court résumé, "
        "puis des lignes numérotées si pertinent.\n"
        "7. Format ligne commande :\n"
        "Commande #[ID_FACTURE] — [date] — [TYPE_FACTURE] — Qté : [QUANTITE]\n"
        "8. Si les données sont vides, indique qu'aucun résultat n'a été trouvé "
        "et propose de vérifier l'orthographe du nom.\n"
        "9. Ne cite pas de montants en dinars ni prix unitaires, sauf demande explicite.");
}

static QString anthropicErrorDetail(const QJsonObject &obj)
{
    const QJsonValue errVal = obj.value(QStringLiteral("error"));
    if (errVal.isObject()) {
        const QString msg = errVal.toObject().value(QStringLiteral("message")).toString();
        if (!msg.isEmpty()) {
            return msg;
        }
    }
    if (errVal.isString()) {
        return errVal.toString();
    }
    return QString();
}

} // namespace

bool LlmClient::anthropicApiKeyNeedsConfiguration()
{
    return resolveApiKey().isEmpty();
}

LlmClient::LlmClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_apiKey(resolveApiKey())
    , m_modelId(QString::fromUtf8(qgetenv("HUGGINGFACE_MODEL_ID")).trimmed())
{
    if (m_modelId.isEmpty()) {
        m_modelId = QStringLiteral("Qwen/Qwen2.5-7B-Instruct");
    }

    // Détection provider par préfixe de clé.
    // - sk-ant-... : Anthropic
    // - gsk_...    : Groq (API compatible OpenAI)
    if (m_apiKey.trimmed().startsWith(QStringLiteral("gsk_"))) {
        m_provider = Provider::Groq;
    } else if (m_apiKey.trimmed().startsWith(QStringLiteral("hf_"))) {
        m_provider = Provider::HuggingFace;
    } else {
        m_provider = Provider::Anthropic;
    }
}

void LlmClient::requestSql(const QString &userQuestion, const QString &dbSchema)
{
    m_currentMode = QStringLiteral("sql");

    const QString systemPrompt = sqlSystemPrompt(dbSchema);

    QJsonObject body;
    if (m_provider == Provider::Anthropic) {
        body[QStringLiteral("model")] = QStringLiteral("claude-sonnet-4-20250514");
        body[QStringLiteral("max_tokens")] = 1024;
        body[QStringLiteral("system")] = systemPrompt;

        QJsonArray messages;
        QJsonObject userMsg;
        userMsg[QStringLiteral("role")] = QStringLiteral("user");
        userMsg[QStringLiteral("content")] = userQuestion;
        messages.append(userMsg);
        body[QStringLiteral("messages")] = messages;
    } else {
        // Groq / Hugging Face Router (OpenAI-compatible)
        body[QStringLiteral("model")] = QStringLiteral("llama-3.3-70b-versatile");
        if (m_provider == Provider::HuggingFace) {
            body[QStringLiteral("model")] = m_modelId;
        }
        body[QStringLiteral("max_tokens")] = 1024;
        body[QStringLiteral("temperature")] = 0.2;

        QJsonArray messages;
        messages.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("system")},
            {QStringLiteral("content"), systemPrompt},
        });
        messages.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("user")},
            {QStringLiteral("content"), userQuestion},
        });
        body[QStringLiteral("messages")] = messages;
    }

    postRequest(body);
}

void LlmClient::requestAnswer(const QString &userQuestion, const QString &rawData)
{
    m_currentMode = QStringLiteral("answer");

    const QString userContent = QStringLiteral("Question : %1\n\nDonnées brutes (CSV) :\n%2")
                                    .arg(userQuestion, rawData);

    const QString systemPrompt = answerSystemPrompt();

    QJsonObject body;
    if (m_provider == Provider::Anthropic) {
        body[QStringLiteral("model")] = QStringLiteral("claude-sonnet-4-20250514");
        body[QStringLiteral("max_tokens")] = 1024;
        body[QStringLiteral("system")] = systemPrompt;

        QJsonArray messages;
        QJsonObject userMsg;
        userMsg[QStringLiteral("role")] = QStringLiteral("user");
        userMsg[QStringLiteral("content")] = userContent;
        messages.append(userMsg);
        body[QStringLiteral("messages")] = messages;
    } else {
        body[QStringLiteral("model")] = QStringLiteral("llama-3.3-70b-versatile");
        if (m_provider == Provider::HuggingFace) {
            body[QStringLiteral("model")] = m_modelId;
        }
        body[QStringLiteral("max_tokens")] = 1024;
        body[QStringLiteral("temperature")] = 0.2;

        QJsonArray messages;
        messages.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("system")},
            {QStringLiteral("content"), systemPrompt},
        });
        messages.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("user")},
            {QStringLiteral("content"), userContent},
        });
        body[QStringLiteral("messages")] = messages;
    }

    postRequest(body);
}

void LlmClient::postRequest(const QJsonObject &body)
{
    if (m_apiKey.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral(
            "Clé API IA introuvable. Configurez AI_CHAT_API_KEY (ou GROQ_API_KEY / ANTHROPIC_API_KEY)."));
        return;
    }

    const QUrl url = (m_provider == Provider::Anthropic)
                         ? QUrl(QStringLiteral("https://api.anthropic.com/v1/messages"))
                         : (m_provider == Provider::HuggingFace
                                ? QUrl(QStringLiteral("https://router.huggingface.co/v1/chat/completions"))
                                : QUrl(QStringLiteral("https://api.groq.com/openai/v1/chat/completions")));
    QNetworkRequest request(url);

    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", QByteArrayLiteral("application/json"));
    request.setRawHeader("User-Agent", QByteArrayLiteral("ZitounaTech-Qt/6"));

    if (m_provider == Provider::Anthropic) {
        request.setRawHeader("x-api-key", m_apiKey.toUtf8());
        request.setRawHeader("anthropic-version", QByteArrayLiteral("2023-06-01"));
    } else {
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
    }

    // Certains environnements (proxy/inspection) renvoient des redirects HTTP.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    request.setTransferTimeout(20000);

    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    request.setSslConfiguration(sslConfig);

    const QByteArray jsonData = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_nam->post(request, jsonData);

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

void LlmClient::handleResponse(QNetworkReply *reply)
{
    if (!reply) {
        return;
    }

    const int httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString httpReason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString();

    if (reply->error() != QNetworkReply::NoError) {
        QString detail;
        switch (reply->error()) {
        case QNetworkReply::ConnectionRefusedError:
            detail = QStringLiteral("Connexion refusée par le serveur.");
            break;
        case QNetworkReply::RemoteHostClosedError:
            detail = QStringLiteral("Le serveur a fermé la connexion.");
            break;
        case QNetworkReply::HostNotFoundError:
            detail = QStringLiteral("Serveur IA introuvable.\n"
                                    "Vérifiez votre connexion internet.");
            break;
        case QNetworkReply::TimeoutError:
            detail = QStringLiteral("Délai dépassé (20 s).\n"
                                    "Vérifiez votre connexion internet.");
            break;
        case QNetworkReply::SslHandshakeFailedError:
            detail = QStringLiteral("Erreur SSL — OpenSSL manquant.\n"
                                    "Copiez libssl-3-x64.dll et "
                                    "libcrypto-3-x64.dll près de l'exe.");
            break;
        case QNetworkReply::AuthenticationRequiredError:
            detail = QStringLiteral("Clé API invalide ou expirée (401).");
            break;
        case QNetworkReply::ContentAccessDenied:
            detail = QStringLiteral("Accès refusé (403).\n"
                                    "Vérifiez votre clé API.");
            break;
        case QNetworkReply::ContentNotFoundError:
            detail = QStringLiteral("Endpoint introuvable (404).\n"
                                    "URL API incorrecte.");
            break;
        case QNetworkReply::UnknownContentError:
            if (httpCode == 429) {
                detail = QStringLiteral("Trop de requêtes (429).\n"
                                        "Attendez quelques secondes.");
            } else if (httpCode == 529) {
                detail = QStringLiteral("Serveur IA surchargé (529).\n"
                                        "Réessayez dans quelques instants.");
            } else {
                detail = QStringLiteral("Erreur HTTP %1.").arg(httpCode);
            }
            break;
        default:
            detail = QStringLiteral("Erreur réseau : %1\nCode : %2")
                         .arg(reply->errorString())
                         .arg(httpCode);
            break;
        }

        if (!httpReason.trimmed().isEmpty()) {
            detail += QStringLiteral("\nRaison : ") + httpReason.trimmed();
        }

        const QVariant sslProp = reply->property("sslErrorMsg");
        if (sslProp.isValid() && !sslProp.toString().trimmed().isEmpty()) {
            detail += QStringLiteral("\n\nSSL : ") + sslProp.toString();
        }

        const QByteArray errBody = reply->readAll();
        if (!errBody.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(errBody);
            if (doc.isObject()) {
                const QString apiMsg = anthropicErrorDetail(doc.object());
                if (!apiMsg.isEmpty()) {
                    detail += QStringLiteral("\n\nDétail API : ") + apiMsg;
                }
            }
        }

        emit errorOccurred(detail);
        reply->deleteLater();
        return;
    }

    const QByteArray responseData = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(responseData);

    if (doc.isNull() || !doc.isObject()) {
        QString msg = QStringLiteral("Réponse JSON invalide du serveur.");
        if (httpCode != 0 && httpCode != 200) {
            msg += QStringLiteral("\nHTTP %1").arg(httpCode);
            if (!httpReason.trimmed().isEmpty())
                msg += QStringLiteral(" — ") + httpReason.trimmed();
        }
        msg += QStringLiteral("\nRéponse brute : ") + QString::fromUtf8(responseData.left(300));
        emit errorOccurred(msg);
        reply->deleteLater();
        return;
    }

    QJsonObject obj = doc.object();

    if (m_provider == Provider::Anthropic) {
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("error")
            || obj.contains(QStringLiteral("error"))) {
            QString apiError = anthropicErrorDetail(obj);
            if (apiError.isEmpty()) {
                apiError = QStringLiteral("(message d'erreur non parsé)");
            }
            emit errorOccurred(QStringLiteral("Erreur API Anthropic :\n") + apiError);
            reply->deleteLater();
            return;
        }
    } else {
        const QJsonValue errVal = obj.value(QStringLiteral("error"));
        if (errVal.isObject()) {
            const QString msg = errVal.toObject().value(QStringLiteral("message")).toString();
            if (!msg.isEmpty()) {
                emit errorOccurred(QStringLiteral("Erreur API Groq :\n") + msg);
                reply->deleteLater();
                return;
            }
        }
    }

    if (httpCode != 0 && httpCode != 200) {
        const QString apiMsg = anthropicErrorDetail(obj);
        emit errorOccurred(QStringLiteral("Réponse HTTP %1.\n%2")
                               .arg(QString::number(httpCode),
                                    apiMsg.isEmpty() ? QString::fromUtf8(responseData.left(300))
                                                     : apiMsg));
        reply->deleteLater();
        return;
    }

    QString text;
    if (m_provider == Provider::Anthropic) {
        const QJsonArray content = obj.value(QStringLiteral("content")).toArray();
        if (content.isEmpty()) {
            emit errorOccurred(QStringLiteral("Réponse vide reçue de l'API."));
            reply->deleteLater();
            return;
        }
        text = content.at(0).toObject().value(QStringLiteral("text")).toString().trimmed();
    } else {
        const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            emit errorOccurred(QStringLiteral("Réponse vide reçue de l'API."));
            reply->deleteLater();
            return;
        }
        const QJsonObject msgObj =
            choices.at(0).toObject().value(QStringLiteral("message")).toObject();
        text = msgObj.value(QStringLiteral("content")).toString().trimmed();
    }
    if (text.isEmpty()) {
        emit errorOccurred(QStringLiteral("Texte vide dans la réponse API."));
        reply->deleteLater();
        return;
    }

    if (m_currentMode == QStringLiteral("sql")) {
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
