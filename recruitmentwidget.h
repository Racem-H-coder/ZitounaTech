#ifndef RECRUITMENTWIDGET_H
#define RECRUITMENTWIDGET_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTextEdit;
QT_END_NAMESPACE

class RecruitmentWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RecruitmentWidget(QWidget *parent = nullptr);

private slots:
    void onAddCvClicked();
    void onAnalyzeClicked();
    void onCandidateSelectionChanged();

    void onApiReplyFinished();

private:
    struct Candidate {
        int cvId = 0;             // ID en base (CANDIDATE_CV.ID_CV), 0 si non stocké
        QString filePath;
        QString displayName;
        QString cvText;           // texte extrait/nettoyé

        int score = -1;           // 0..100
        QString recommendation;   // Accepter | Peut-être | Rejeter
        QString extractedSkills;
        QString missingSkills;
        QString rawModelText;     // réponse brute modèle
        QString error;            // erreur locale ou API
    };

    // --- UI ---
    void buildUi();
    void setBusy(bool busy, const QString &message = QString());
    void showResult(const Candidate &c);
    void refreshRankingUi();

    // --- CV processing ---
    Candidate makeCandidateFromFile(const QString &path);
    QString extractTextFromFile(const QString &path, QString *err) const;
    QString normalizeTextForAi(QString text) const;

    // --- DB (optionnel) ---
    bool dbAvailable() const;
    int insertCvToDb(const Candidate &c, QString *err) const;
    bool updateCvAnalysisInDb(const Candidate &c,
                              const QString &jobReq,
                              const QString &modelIdUsed,
                              QString *err) const;

    // --- HF API ---
    QString apiKeyFromEnv() const; // HUGGINGFACE_API_KEY
    QString modelId() const;       // modèle par défaut (modifiable plus tard)
    QByteArray buildRequestPayload(const QString &prompt) const;
    QString buildPrompt(const QString &cvText, const QString &jobReq) const;
    bool parseModelOutput(const QString &modelText, Candidate *c, QString *err) const;

    void startAnalyzeCandidate(int index);
    void startAnalyzeAll();

private:
    // Widgets
    QLabel *m_title = nullptr;
    QLabel *m_hint = nullptr;

    QPushButton *m_addCvBtn = nullptr;
    QListWidget *m_candidatesList = nullptr;

    QPlainTextEdit *m_jobReqEdit = nullptr;
    QPushButton *m_analyzeBtn = nullptr;

    QProgressBar *m_busyBar = nullptr;
    QLabel *m_busyLabel = nullptr;

    QTextEdit *m_resultsView = nullptr;

    // State
    QList<Candidate> m_candidates;
    int m_queueIndex = -1; // pour "analyser tout"

    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_inFlight;
};

#endif // RECRUITMENTWIDGET_H

