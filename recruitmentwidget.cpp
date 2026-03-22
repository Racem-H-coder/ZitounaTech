#include "recruitmentwidget.h"

#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QCoreApplication>
#include <QDir>
#include <QtGlobal>

namespace {

static QString colorForRecommendation(const QString &rec)
{
    if (rec.compare(QStringLiteral("Accepter"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("#2e7d32"); // vert
    if (rec.compare(QStringLiteral("Peut-être"), Qt::CaseInsensitive) == 0
        || rec.compare(QStringLiteral("Peut-etre"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("#f59e0b"); // jaune
    if (rec.compare(QStringLiteral("Rejeter"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("#dc2626"); // rouge
    return QStringLiteral("#0f172a");
}

static QString safeFileName(const QString &path)
{
    const int slash = qMax(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
    return slash >= 0 ? path.mid(slash + 1) : path;
}

} // namespace

RecruitmentWidget::RecruitmentWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    m_net = new QNetworkAccessManager(this);
}

void RecruitmentWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    // Conteneur "carte" (même style que les autres sections)
    auto *card = new QFrame(this);
    card->setStyleSheet(QStringLiteral(
        "QFrame { background: #ffffff; border: 1px solid #dbe4ea; border-radius: 12px; }"
        "QLabel { color: #0f172a; }"
        "QPushButton { border-radius: 10px; padding: 10px 14px; font-weight: 700; }"
        "QPushButton#primaryBtn { background: #556B2F; color: white; }"
        "QPushButton#primaryBtn:hover { background: #4e612b; }"
        "QPushButton#secondaryBtn { background: #f8fafc; border: 1px solid #dbe4ea; color: #0f172a; }"
        "QPushButton#secondaryBtn:hover { background: #eef2f7; }"
        "QPlainTextEdit, QTextEdit { background: #ffffff; border: 1px solid #dbe4ea; border-radius: 10px; padding: 8px; }"
        "QListWidget { background: #ffffff; border: 1px solid #dbe4ea; border-radius: 10px; }"));

    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(16, 14, 16, 16);
    lay->setSpacing(10);

    auto *header = new QHBoxLayout();
    m_title = new QLabel(tr("<b>Recrutement Intelligent</b>"), card);
    QFont tf = m_title->font();
    tf.setPointSizeF(tf.pointSizeF() + 2.0);
    m_title->setFont(tf);
    header->addWidget(m_title);
    header->addStretch();
    lay->addLayout(header);

    m_hint = new QLabel(tr("Ajoutez des CV (PDF texte ou .txt) puis décrivez le poste. L'IA calcule un score, les compétences, les manques et une recommandation."), card);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("QLabel { color: #475569; font-size: 12px; }"));
    lay->addWidget(m_hint);

    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(12);

    auto *leftCol = new QVBoxLayout();
    leftCol->setSpacing(8);

    m_addCvBtn = new QPushButton(tr("Ajouter CV"), card);
    m_addCvBtn->setObjectName(QStringLiteral("secondaryBtn"));
    m_addCvBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addCvBtn, &QPushButton::clicked, this, &RecruitmentWidget::onAddCvClicked);

    m_candidatesList = new QListWidget(card);
    m_candidatesList->setMinimumWidth(320);
    m_candidatesList->setMinimumHeight(160);
    connect(m_candidatesList, &QListWidget::itemSelectionChanged, this, &RecruitmentWidget::onCandidateSelectionChanged);

    leftCol->addWidget(m_addCvBtn);
    leftCol->addWidget(new QLabel(tr("<b>Candidats</b>"), card));
    leftCol->addWidget(m_candidatesList, 1);

    auto *rightCol = new QVBoxLayout();
    rightCol->setSpacing(8);

    rightCol->addWidget(new QLabel(tr("<b>Exigences du poste</b>"), card));
    m_jobReqEdit = new QPlainTextEdit(card);
    m_jobReqEdit->setPlaceholderText(tr(
        "Ex: Opérateur / Technicien de presse d’olive (2+ ans)\n"
        "- Maîtrise extraction (presse/centrifuge), réglages et contrôle des paramètres\n"
        "- Qualité: suivi acidité, filtration, traçabilité lots, respect HACCP\n"
        "- Maintenance 1er niveau: graissage, vérifs, détection fuites/pannes\n"
        "- Hygiène & sécurité: nettoyage CIP, EPI, procédures de sécurité\n"
        "- Organisation: travail en équipe, gestion des pics de production, reporting\n"
        "- Atout: connaissance normes agroalimentaires, conduite chariot, ERP/Excel"));
    m_jobReqEdit->setMinimumHeight(120);
    rightCol->addWidget(m_jobReqEdit);

    auto *actions = new QHBoxLayout();
    actions->addStretch();
    m_analyzeBtn = new QPushButton(tr("Analyser avec IA"), card);
    m_analyzeBtn->setObjectName(QStringLiteral("primaryBtn"));
    m_analyzeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_analyzeBtn, &QPushButton::clicked, this, &RecruitmentWidget::onAnalyzeClicked);
    actions->addWidget(m_analyzeBtn);
    rightCol->addLayout(actions);

    topRow->addLayout(leftCol, 1);
    topRow->addLayout(rightCol, 2);
    lay->addLayout(topRow);

    // Busy / loading
    auto *busyRow = new QHBoxLayout();
    busyRow->setSpacing(10);
    m_busyBar = new QProgressBar(card);
    m_busyBar->setRange(0, 0); // indéterminé
    m_busyBar->setVisible(false);
    m_busyBar->setFixedHeight(10);
    m_busyLabel = new QLabel(card);
    m_busyLabel->setVisible(false);
    m_busyLabel->setStyleSheet(QStringLiteral("QLabel { color: #475569; font-size: 12px; }"));
    busyRow->addWidget(m_busyBar, 1);
    busyRow->addWidget(m_busyLabel);
    lay->addLayout(busyRow);

    lay->addWidget(new QLabel(tr("<b>Résultats</b>"), card));
    m_resultsView = new QTextEdit(card);
    m_resultsView->setReadOnly(true);
    m_resultsView->setMinimumHeight(180);
    lay->addWidget(m_resultsView);

    root->addWidget(card);
}

void RecruitmentWidget::setBusy(bool busy, const QString &message)
{
    if (m_busyBar)
        m_busyBar->setVisible(busy);
    if (m_busyLabel) {
        m_busyLabel->setVisible(busy || !message.isEmpty());
        m_busyLabel->setText(message);
    }
    if (m_addCvBtn)
        m_addCvBtn->setEnabled(!busy);
    if (m_analyzeBtn)
        m_analyzeBtn->setEnabled(!busy);
}

void RecruitmentWidget::onAddCvClicked()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this,
        tr("Sélectionner des CV"),
        QString(),
        tr("CV (*.pdf *.txt *.doc *.docx);;Tous les fichiers (*.*)"));

    if (paths.isEmpty())
        return;

    for (const QString &p : paths) {
        Candidate c = makeCandidateFromFile(p);
        m_candidates.push_back(c);
        m_candidatesList->addItem(c.displayName);
    }

    if (m_candidatesList->count() > 0 && m_candidatesList->selectedItems().isEmpty())
        m_candidatesList->setCurrentRow(0);

    refreshRankingUi();
    onCandidateSelectionChanged();
}

RecruitmentWidget::Candidate RecruitmentWidget::makeCandidateFromFile(const QString &path)
{
    Candidate c;
    c.filePath = path;
    c.displayName = safeFileName(path);

    QString err;
    const QString raw = extractTextFromFile(path, &err);
    if (!err.isEmpty()) {
        c.error = err;
        c.rawModelText = err;
        c.cvText.clear();
        return c;
    }
    c.cvText = normalizeTextForAi(raw);
    if (c.cvText.trimmed().isEmpty()) {
        c.error = tr("CV vide ou non lisible en texte. Essayez un PDF texte ou un fichier .txt.");
        c.rawModelText = c.error;
    }

    // Stockage en base (si la table existe). Non bloquant.
    QString dberr;
    const int id = insertCvToDb(c, &dberr);
    if (id > 0)
        c.cvId = id;
    return c;
}

bool RecruitmentWidget::dbAvailable() const
{
    QSqlDatabase db = QSqlDatabase::database();
    return db.isValid() && db.isOpen();
}

int RecruitmentWidget::insertCvToDb(const Candidate &c, QString *err) const
{
    if (err)
        err->clear();
    if (!dbAvailable())
        return 0;

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "INSERT INTO CANDIDATE_CV (ID_CV, FILE_NAME, FILE_PATH, CV_TEXT, CREATED_AT) "
        "VALUES (NULL, :fn, :fp, :txt, SYSDATE)"));
    q.bindValue(QStringLiteral(":fn"), c.displayName);
    q.bindValue(QStringLiteral(":fp"), c.filePath);
    q.bindValue(QStringLiteral(":txt"), c.cvText);
    if (!q.exec()) {
        if (err)
            *err = q.lastError().text();
        return 0;
    }

    // Récup ID (simple): le dernier ID pour ce file_path.
    QSqlQuery q2(QSqlDatabase::database());
    q2.prepare(QStringLiteral("SELECT MAX(ID_CV) FROM CANDIDATE_CV WHERE FILE_PATH = :fp"));
    q2.bindValue(QStringLiteral(":fp"), c.filePath);
    if (q2.exec() && q2.next())
        return q2.value(0).toInt();
    return 0;
}

bool RecruitmentWidget::updateCvAnalysisInDb(const Candidate &c,
                                            const QString &jobReq,
                                            const QString &modelIdUsed,
                                            QString *err) const
{
    if (err)
        err->clear();
    if (!dbAvailable() || c.cvId <= 0)
        return false;

    QSqlQuery q(QSqlDatabase::database());
    q.prepare(QStringLiteral(
        "UPDATE CANDIDATE_CV SET "
        "JOB_REQUIREMENTS = :jr, MODEL_ID = :mid, "
        "SCORE = :sc, RECOMMENDATION = :rec, "
        "EXTRACTED_SKILLS = :sk, MISSING_SKILLS = :ms, "
        "AI_RAW = :raw, ANALYZED_AT = SYSDATE "
        "WHERE ID_CV = :id"));
    q.bindValue(QStringLiteral(":jr"), jobReq);
    q.bindValue(QStringLiteral(":mid"), modelIdUsed);
    q.bindValue(QStringLiteral(":sc"), c.score >= 0 ? c.score : QVariant());
    q.bindValue(QStringLiteral(":rec"), c.recommendation);
    q.bindValue(QStringLiteral(":sk"), c.extractedSkills);
    q.bindValue(QStringLiteral(":ms"), c.missingSkills);
    q.bindValue(QStringLiteral(":raw"), c.rawModelText);
    q.bindValue(QStringLiteral(":id"), c.cvId);
    if (!q.exec()) {
        if (err)
            *err = q.lastError().text();
        return false;
    }
    return true;
}

QString RecruitmentWidget::extractTextFromFile(const QString &path, QString *err) const
{
    if (err)
        err->clear();

    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".txt"))) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (err)
                *err = tr("Impossible d'ouvrir le fichier.");
            return QString();
        }
        const QByteArray data = f.readAll();
        return QString::fromUtf8(data);
    }

    // DOCX: extraction via Python standard library (zipfile + XML parsing).
    // Aucun package externe requis si Python est disponible.
    if (lower.endsWith(QStringLiteral(".docx"))) {
        QProcess p;
        p.setProgram(QStringLiteral("python"));
        QProcessEnvironment penv = QProcessEnvironment::systemEnvironment();
        penv.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
        p.setProcessEnvironment(penv);
        const QString pyScript = QStringLiteral(
            "import sys, zipfile, xml.etree.ElementTree as ET\n"
            "path = sys.argv[1]\n"
            "with zipfile.ZipFile(path) as z:\n"
            "    xml = z.read('word/document.xml').decode('utf-8', 'ignore')\n"
            "root = ET.fromstring(xml)\n"
            "ns = '{http://schemas.openxmlformats.org/wordprocessingml/2006/main}'\n"
            "parts = []\n"
            "for pnode in root.iter(ns + 'p'):\n"
            "    runs = [t.text or '' for t in pnode.iter(ns + 't')]\n"
            "    line = ''.join(runs).strip()\n"
            "    if line:\n"
            "        parts.append(line)\n"
            "text = '\\n'.join(parts)\n"
            "sys.stdout.buffer.write(text.encode('utf-8', 'ignore'))\n");
        p.setArguments({
            QStringLiteral("-c"),
            pyScript,
            path
        });
        p.start();
        if (!p.waitForFinished(10000)) {
            p.kill();
            if (err)
                *err = tr("DOCX détecté mais extraction impossible (timeout).");
            return QString();
        }
        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        const QString perr = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0 && !out.trimmed().isEmpty())
            return out;
        if (err) {
            *err = tr("DOCX détecté mais extraction impossible. Vérifiez Python installé.%1")
                       .arg(perr.isEmpty() ? QString() : QStringLiteral("\n") + perr);
        }
        return QString();
    }

    // PDF: tentative via pdftotext (si installé). Sinon on explique clairement.
    if (lower.endsWith(QStringLiteral(".pdf"))) {
        QProcess p;
        p.setProgram(QStringLiteral("pdftotext"));
        p.setArguments({QStringLiteral("-layout"), path, QStringLiteral("-")});
        p.start();
        if (!p.waitForFinished(8000)) {
            p.kill();
            if (err)
                *err = tr("Impossible d'extraire le texte du PDF (timeout).");
            return QString();
        }
        const QString stderrText = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        const QString stdoutText = QString::fromUtf8(p.readAllStandardOutput());
        if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
            if (err) {
                *err = tr("Extraction PDF impossible. Installez 'pdftotext' (Poppler) ou exportez le CV en .txt.%1")
                           .arg(stderrText.isEmpty() ? QString() : QStringLiteral("\n") + stderrText);
            }
            return QString();
        }
        if (stdoutText.trimmed().isEmpty()) {
            if (err)
                *err = tr("PDF détecté, mais aucun texte n'a été extrait (probablement un scan/image).");
            return QString();
        }
        return stdoutText;
    }

    // DOC (Word binaire): tentative via antiword puis catdoc.
    if (lower.endsWith(QStringLiteral(".doc"))) {
        auto tryTool = [&](const QString &tool, const QStringList &args, QString *output, QString *toolErr) -> bool {
            QProcess p;
            p.setProgram(tool);
            p.setArguments(args);
            p.start();
            if (!p.waitForFinished(8000)) {
                p.kill();
                if (toolErr)
                    *toolErr = QStringLiteral("timeout");
                return false;
            }
            const QString out = QString::fromUtf8(p.readAllStandardOutput());
            const QString errTxt = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
            if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0 && !out.trimmed().isEmpty()) {
                if (output)
                    *output = out;
                return true;
            }
            if (toolErr)
                *toolErr = errTxt;
            return false;
        };

        QString out;
        QString e1;
        if (tryTool(QStringLiteral("antiword"), {path}, &out, &e1))
            return out;
        QString e2;
        if (tryTool(QStringLiteral("catdoc"), {path}, &out, &e2))
            return out;

        if (err) {
            *err = tr("DOC détecté mais extraction impossible. Installez antiword/catdoc, "
                      "ou convertissez en DOCX/TXT.%1%2")
                       .arg(e1.isEmpty() ? QString() : QStringLiteral("\nantiword: ") + e1)
                       .arg(e2.isEmpty() ? QString() : QStringLiteral("\ncatdoc: ") + e2);
        }
        return QString();
    }

    // DOC/DOCX: extraction "propre" nécessite un outil. On garde une consigne simple.
    if (err) {
        *err = tr("Extraction texte non disponible pour ce format (%1). "
                  "Solution rapide: exportez le CV en PDF texte ou en .txt.")
                   .arg(lower.endsWith(QStringLiteral(".pdf")) ? QStringLiteral("PDF") :
                        lower.endsWith(QStringLiteral(".doc")) ? QStringLiteral("DOC") : QStringLiteral("DOCX"));
    }
    return QString();
}

QString RecruitmentWidget::normalizeTextForAi(QString text) const
{
    // Nettoyage simple: enlever espaces multiples + limiter la taille (API inference a des limites).
    text.replace(QRegularExpression(QStringLiteral("[\\t\\r]+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("[ ]{2,}")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));

    const int maxChars = 12000; // garde une marge
    if (text.size() > maxChars)
        text = text.left(maxChars) + QStringLiteral("\n\n[...CV tronqué...]\n");
    return text.trimmed();
}

QString RecruitmentWidget::apiKeyFromEnv() const
{
    return qEnvironmentVariable("HUGGINGFACE_API_KEY").trimmed();
}

QString RecruitmentWidget::modelId() const
{
    // Modèle instruct généralement supporté via le router chat-completions.
    return QStringLiteral("Qwen/Qwen2.5-7B-Instruct");
}

QString RecruitmentWidget::buildPrompt(const QString &cvText, const QString &jobReq) const
{
    // Prompt orienté "sortie structurée" pour faciliter le parsing côté Qt.
    return QStringLiteral(
        "Tu es un recruteur technique. Analyse le CV selon les exigences du poste.\n"
        "Réponds STRICTEMENT en JSON valide (pas de markdown), avec les clés:\n"
        "score (0-100), competences (liste courte), competences_manquantes (liste courte), "
        "recommandation (Accepter|Peut-être|Rejeter), justification (2-4 phrases).\n\n"
        "EXIGENCES DU POSTE:\n%1\n\n"
        "CV:\n%2\n").arg(jobReq.trimmed(), cvText.trimmed());
}

QByteArray RecruitmentWidget::buildRequestPayload(const QString &prompt) const
{
    QJsonArray messages;
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), QStringLiteral("Tu es un assistant RH. Réponds de façon fiable et concise.")}});
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), prompt}});

    QJsonObject root;
    root.insert(QStringLiteral("model"), modelId());
    root.insert(QStringLiteral("messages"), messages);
    root.insert(QStringLiteral("temperature"), 0.2);
    root.insert(QStringLiteral("max_tokens"), 420);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

void RecruitmentWidget::onAnalyzeClicked()
{
    if (m_candidates.isEmpty()) {
        m_resultsView->setHtml(tr("<b style='color:#dc2626'>Ajoutez au moins un CV.</b>"));
        return;
    }
    const QString jobReq = m_jobReqEdit ? m_jobReqEdit->toPlainText().trimmed() : QString();
    if (jobReq.isEmpty()) {
        m_resultsView->setHtml(tr("<b style='color:#dc2626'>Veuillez saisir les exigences du poste.</b>"));
        return;
    }
    const QString key = apiKeyFromEnv();
    if (key.isEmpty()) {
        const bool isSet = qEnvironmentVariableIsSet("HUGGINGFACE_API_KEY");
        const int klen = qEnvironmentVariable("HUGGINGFACE_API_KEY").trimmed().size();
        const QString cwd = QDir::currentPath();
        const QString exe = QCoreApplication::applicationFilePath();
        m_resultsView->setHtml(tr("<b style='color:#dc2626'>Clé API manquante.</b><br/>"
                                  "Définissez <code>HUGGINGFACE_API_KEY</code> dans la variable d'environnement Windows, "
                                  "ou dans un fichier <code>.env</code> (dossier du projet / dossier de l'exécutable), "
                                  "puis relancez l'application.<br/><br/>"
                                  "<span style='color:#475569'>Diagnostic: variable présente=%1, longueur=%2<br/>"
                                  "cwd=%3<br/>exe=%4</span>")
                                  .arg(isSet ? tr("oui") : tr("non"))
                                  .arg(QString::number(klen))
                                  .arg(cwd.toHtmlEscaped())
                                  .arg(exe.toHtmlEscaped()));
        return;
    }

    // Bonus: analyse + classement de tous les candidats (rank automatique).
    startAnalyzeAll();
}

void RecruitmentWidget::startAnalyzeAll()
{
    m_queueIndex = 0;
    setBusy(true, tr("Analyse IA en cours…"));
    if (m_resultsView) {
        m_resultsView->setHtml(tr("<span style='color:#475569'>Analyse en cours… "
                                  "Vérifiez que le CV est en <b>.txt</b> ou en <b>PDF texte</b> (pas un scan), "
                                  "sinon l'extraction sera vide.</span>"));
    }
    startAnalyzeCandidate(m_queueIndex);
}

void RecruitmentWidget::startAnalyzeCandidate(int index)
{
    if (index < 0 || index >= m_candidates.size()) {
        setBusy(false);
        refreshRankingUi();
        return;
    }

    Candidate &c = m_candidates[index];
    c.error.clear();
    c.rawModelText.clear();
    c.score = -1;
    c.recommendation.clear();
    c.extractedSkills.clear();
    c.missingSkills.clear();

    if (c.cvText.trimmed().isEmpty()) {
        c.error = tr("CV vide/non lisible: impossible d'analyser. "
                     "Astuce: exportez le CV en .txt ou en PDF texte (pas une image/scanner).");
        refreshRankingUi();
        if (m_candidatesList && m_candidatesList->currentRow() == index)
            showResult(c);
        m_queueIndex++;
        startAnalyzeCandidate(m_queueIndex);
        return;
    }

    const QString jobReq = m_jobReqEdit ? m_jobReqEdit->toPlainText().trimmed() : QString();
    const QString prompt = buildPrompt(c.cvText, jobReq);

    const QString url = QStringLiteral("https://router.huggingface.co/v1/chat/completions");
    // Évite le "most vexing parse" sous MinGW: on force une vraie variable avec {}
    const QNetworkRequest req{QUrl(url)};
    QNetworkRequest mutableReq = req;
    mutableReq.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    mutableReq.setRawHeader("Authorization", QByteArray("Bearer ") + apiKeyFromEnv().toUtf8());

    const QByteArray payload = buildRequestPayload(prompt);
    m_inFlight = m_net->post(mutableReq, payload);
    connect(m_inFlight, &QNetworkReply::finished, this, &RecruitmentWidget::onApiReplyFinished);

    if (m_busyLabel)
        m_busyLabel->setText(tr("Analyse de %1…").arg(c.displayName));
    if (m_resultsView) {
        m_resultsView->setHtml(tr("<span style='color:#475569'>Analyse de <b>%1</b> en cours…</span>")
                                   .arg(c.displayName.toHtmlEscaped()));
    }
}

void RecruitmentWidget::onApiReplyFinished()
{
    if (!m_inFlight)
        return;

    const QByteArray bytes = m_inFlight->readAll();
    const QString body = QString::fromUtf8(bytes);
    const int idx = m_queueIndex;

    if (idx < 0 || idx >= m_candidates.size()) {
        setBusy(false);
        return;
    }

    Candidate &c = m_candidates[idx];

    if (m_inFlight->error() != QNetworkReply::NoError) {
        c.error = tr("Erreur API: %1").arg(m_inFlight->errorString());
        if (body.trimmed().isEmpty()) {
            // Cas fréquent: SSL/connexion. On garde un hint utile.
            c.rawModelText = tr("Aucune réponse reçue.\n"
                                "Si vous êtes sur Windows, vérifiez que Qt a accès à OpenSSL (dll) "
                                "et que le réseau autorise https://router.huggingface.co");
        } else {
            c.rawModelText = body;
        }
    } else {
        // Router v1: { choices:[{message:{content:"..."}}] }
        QString generated;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes);
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            if (obj.contains(QStringLiteral("error")))
                c.error = tr("API: %1").arg(obj.value(QStringLiteral("error")).toString());

            const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
            if (!choices.isEmpty()) {
                const QJsonObject c0 = choices.first().toObject();
                const QJsonObject msg = c0.value(QStringLiteral("message")).toObject();
                generated = msg.value(QStringLiteral("content")).toString();
            }
            if (generated.trimmed().isEmpty())
                generated = obj.value(QStringLiteral("generated_text")).toString();
        } else if (doc.isArray()) {
            // Ancien format fallback
            const QJsonArray arr = doc.array();
            if (!arr.isEmpty() && arr.first().isObject())
                generated = arr.first().toObject().value(QStringLiteral("generated_text")).toString();
        }

        if (generated.trimmed().isEmpty())
            generated = body;

        c.rawModelText = generated;
        QString perr;
        if (!parseModelOutput(generated, &c, &perr)) {
            // On garde la réponse brute dans l'UI pour debug.
            c.error = perr.isEmpty() ? tr("Réponse IA non interprétable.") : perr;
        }
    }

    // Mise à jour UI
    refreshRankingUi();
    if (m_candidatesList && m_candidatesList->currentRow() == idx)
        showResult(c);

    // Persist en base (si dispo)
    QString dberr;
    (void)updateCvAnalysisInDb(c,
                              (m_jobReqEdit ? m_jobReqEdit->toPlainText().trimmed() : QString()),
                              modelId(),
                              &dberr);

    // Continue queue
    m_queueIndex++;
    if (m_queueIndex >= m_candidates.size()) {
        setBusy(false, tr("Analyse terminée."));
        m_queueIndex = -1;
        refreshRankingUi();
        onCandidateSelectionChanged();
        return;
    }
    startAnalyzeCandidate(m_queueIndex);
}

bool RecruitmentWidget::parseModelOutput(const QString &modelText, Candidate *c, QString *err) const
{
    if (err)
        err->clear();
    if (!c)
        return false;

    // On cherche un JSON {...} dans la réponse (le modèle peut ajouter du texte).
    const QRegularExpression re(QStringLiteral("(\\{[\\s\\S]*\\})"));
    const QRegularExpressionMatch m = re.match(modelText);
    const QString jsonText = m.hasMatch() ? m.captured(1) : modelText.trimmed();

    QJsonParseError jerr;
    const QJsonDocument jd = QJsonDocument::fromJson(jsonText.toUtf8(), &jerr);
    if (jerr.error != QJsonParseError::NoError || !jd.isObject()) {
        // Fallback permissif: certains modèles ne respectent pas toujours le JSON strict.
        int scoreFallback = -1;
        QString recFallback;
        QString skillsFallback;
        QString missingFallback;

        QRegularExpression reScore(QStringLiteral("(?i)score\\s*[:=\\-]?\\s*(\\d{1,3})"));
        auto mScore = reScore.match(modelText);
        if (mScore.hasMatch()) {
            scoreFallback = mScore.captured(1).toInt();
            if (scoreFallback < 0 || scoreFallback > 100)
                scoreFallback = -1;
        }

        const QString t = modelText.toLower();
        if (t.contains(QStringLiteral("accepter")) || t.contains(QStringLiteral("accept")))
            recFallback = QStringLiteral("Accepter");
        else if (t.contains(QStringLiteral("rejeter")) || t.contains(QStringLiteral("reject")))
            recFallback = QStringLiteral("Rejeter");
        else if (t.contains(QStringLiteral("peut-être")) || t.contains(QStringLiteral("maybe")))
            recFallback = QStringLiteral("Peut-être");

        QRegularExpression reSkills(QStringLiteral("(?i)(comp[ée]tences?[^:\\n]*):\\s*([^\\n]+)"));
        auto mSkills = reSkills.match(modelText);
        if (mSkills.hasMatch())
            skillsFallback = mSkills.captured(2).trimmed();

        QRegularExpression reMissing(QStringLiteral("(?i)(manquantes?|missing[^:\\n]*):\\s*([^\\n]+)"));
        auto mMissing = reMissing.match(modelText);
        if (mMissing.hasMatch())
            missingFallback = mMissing.captured(2).trimmed();

        if (scoreFallback >= 0 || !recFallback.isEmpty()) {
            c->score = (scoreFallback >= 0) ? scoreFallback : 50;
            c->recommendation = recFallback.isEmpty() ? QStringLiteral("Peut-être") : recFallback;
            c->extractedSkills = skillsFallback;
            c->missingSkills = missingFallback;
            if (c->rawModelText.trimmed().isEmpty())
                c->rawModelText = modelText;
            return true;
        }

        if (err)
            *err = tr("Réponse IA reçue mais non interprétable (ni JSON ni format texte reconnu).");
        return false;
    }

    const QJsonObject o = jd.object();
    const int score = o.value(QStringLiteral("score")).toInt(-1);
    const QString rec = o.value(QStringLiteral("recommandation")).toString().trimmed();
    const QString just = o.value(QStringLiteral("justification")).toString().trimmed();

    // compétences: peut être string ou array
    auto listToString = [](const QJsonValue &v) -> QString {
        if (v.isArray()) {
            QStringList out;
            for (const QJsonValue &x : v.toArray())
                out << x.toString();
            return out.join(QStringLiteral(", "));
        }
        return v.toString();
    };

    const QString skills = listToString(o.value(QStringLiteral("competences"))).trimmed();
    const QString missing = listToString(o.value(QStringLiteral("competences_manquantes"))).trimmed();

    if (score < 0 || score > 100) {
        if (err)
            *err = tr("Score invalide (attendu 0..100).");
        return false;
    }

    c->score = score;
    c->recommendation = rec.isEmpty() ? tr("Peut-être") : rec;
    c->extractedSkills = skills;
    c->missingSkills = missing;

    // On garde la justification dans rawModelText (et on l'affiche).
    if (!just.isEmpty())
        c->rawModelText = jsonText + QStringLiteral("\n") + just;

    return true;
}

void RecruitmentWidget::onCandidateSelectionChanged()
{
    const int row = m_candidatesList ? m_candidatesList->currentRow() : -1;
    if (row < 0 || row >= m_candidates.size()) {
        if (m_resultsView)
            m_resultsView->setHtml(tr("<span style='color:#475569'>Sélectionnez un candidat pour voir le détail.</span>"));
        return;
    }
    showResult(m_candidates.at(row));
}

void RecruitmentWidget::showResult(const Candidate &c)
{
    if (!m_resultsView)
        return;

    if (!c.error.isEmpty()) {
        m_resultsView->setHtml(tr("<b style='color:#dc2626'>%1</b><br/><br/><pre>%2</pre>")
                                   .arg(c.displayName.toHtmlEscaped(), c.rawModelText.toHtmlEscaped()));
        return;
    }

    if (c.score < 0) {
        m_resultsView->setHtml(tr("<b style='color:#f59e0b'>Analyse incomplète pour %1</b><br/><br/>"
                                  "<pre>%2</pre>")
                                   .arg(c.displayName.toHtmlEscaped(),
                                        (c.rawModelText.isEmpty() ? tr("Aucun contenu retourné.") : c.rawModelText.toHtmlEscaped())));
        return;
    }

    const QString col = colorForRecommendation(c.recommendation);
    const QString html = QStringLiteral(
        "<div style='font-family:Segoe UI, Arial; line-height:1.35'>"
        "<div><b>Candidat :</b> %1</div>"
        "<div><b>Score :</b> <span style='color:%2; font-weight:800'>%3/100</span></div>"
        "<div><b>Recommandation :</b> <span style='color:%2; font-weight:800'>%4</span></div>"
        "<hr/>"
        "<div><b>Compétences détectées :</b> %5</div>"
        "<div><b>Compétences manquantes :</b> %6</div>"
        "<hr/>"
        "<div><b>Détails / justification :</b></div>"
        "<pre style='white-space:pre-wrap; background:#f8fafc; padding:10px; border:1px solid #e2e8f0; border-radius:10px'>%7</pre>"
        "</div>")
                            .arg(c.displayName.toHtmlEscaped(),
                                 col,
                                 QString::number(c.score),
                                 c.recommendation.toHtmlEscaped(),
                                 (c.extractedSkills.isEmpty() ? tr("—") : c.extractedSkills.toHtmlEscaped()),
                                 (c.missingSkills.isEmpty() ? tr("—") : c.missingSkills.toHtmlEscaped()),
                                 c.rawModelText.toHtmlEscaped());

    m_resultsView->setHtml(html);
}

void RecruitmentWidget::refreshRankingUi()
{
    if (!m_candidatesList)
        return;

    // Update list labels with score + rec
    for (int i = 0; i < m_candidates.size() && i < m_candidatesList->count(); ++i) {
        const Candidate &c = m_candidates.at(i);
        QString label = c.displayName;
        if (c.score >= 0 && c.error.isEmpty()) {
            label += QStringLiteral(" — %1/100 (%2)").arg(QString::number(c.score), c.recommendation);
        } else if (!c.error.isEmpty()) {
            label += tr(" — erreur");
        }
        m_candidatesList->item(i)->setText(label);
    }

    // Rank candidates by score (bonus) - display in results header area if selection empty.
    QList<int> idxs;
    idxs.reserve(m_candidates.size());
    for (int i = 0; i < m_candidates.size(); ++i)
        idxs.push_back(i);
    std::sort(idxs.begin(), idxs.end(), [this](int a, int b) {
        return m_candidates.at(a).score > m_candidates.at(b).score;
    });

    if (m_resultsView && (m_candidatesList->currentRow() < 0) && !m_candidates.isEmpty()) {
        QStringList lines;
        lines << tr("<b>Classement (score)</b>");
        int shown = 0;
        for (int id : idxs) {
            const Candidate &c = m_candidates.at(id);
            if (c.score < 0 || !c.error.isEmpty())
                continue;
            lines << tr("%1) %2 — %3/100 (%4)")
                        .arg(++shown)
                        .arg(c.displayName.toHtmlEscaped())
                        .arg(QString::number(c.score))
                        .arg(c.recommendation.toHtmlEscaped());
            if (shown >= 8)
                break;
        }
        if (shown == 0)
            lines << tr("<span style='color:#475569'>Analyse non disponible pour le moment.</span>");
        m_resultsView->setHtml(lines.join(QStringLiteral("<br/>")));
    }
}

