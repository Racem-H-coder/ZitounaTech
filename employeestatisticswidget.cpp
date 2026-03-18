#include "employeestatisticswidget.h"

#include <QColor>
#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QComboBox>
#include <QChart>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineSeries>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPieSeries>
#include <QPieSlice>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QValueAxis>
#include <QVBoxLayout>
#include <QtCharts/QChartView>

#include <numeric>

#include <QDebug>

namespace {

static constexpr QRgb kGreenDark = qRgb(46, 125, 50);
static constexpr QRgb kGreenLight = qRgb(129, 199, 132);
static constexpr QRgb kYellow = qRgb(245, 158, 11);
static constexpr QRgb kRed = qRgb(220, 38, 38);

static double clamp01(double x)
{
    if (x < 0.0)
        return 0.0;
    if (x > 1.0)
        return 1.0;
    return x;
}

static double clamp(double x, double lo, double hi)
{
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

static QChartView *makeChartView()
{
    auto *v = new QChartView();
    v->setRenderHint(QPainter::Antialiasing);
    v->setMinimumHeight(260);
    v->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
    return v;
}

} // namespace

EmployeeStatisticsWidget::EmployeeStatisticsWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    refreshStatistics();
}

void EmployeeStatisticsWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(16, 12, 16, 16);

    auto *header = new QHBoxLayout();
    m_titleLabel = new QLabel(tr("<b>Statistiques employés</b>"), this);
    QFont f = m_titleLabel->font();
    f.setPointSizeF(f.pointSizeF() + 2.0);
    m_titleLabel->setFont(f);
    m_refreshBtn = new QPushButton(tr("Actualiser"), this);
    m_refreshBtn->setMinimumHeight(36);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &EmployeeStatisticsWidget::refreshStatistics);

    header->addWidget(m_titleLabel);
    header->addStretch();
    header->addWidget(m_refreshBtn);
    root->addLayout(header);

    auto *summaryGrid = new QGridLayout();
    summaryGrid->setHorizontalSpacing(24);
    summaryGrid->setVerticalSpacing(8);

    auto makeStatLabel = [this](const QString &title) {
        auto *w = new QLabel(this);
        w->setWordWrap(true);
        w->setTextInteractionFlags(Qt::TextSelectableByMouse);
        w->setStyleSheet(QStringLiteral("QLabel { color: #1f2937; padding: 8px; background: #F1F8F4; border-radius: 8px; border: 1px solid #C8E6C9; }"));
        Q_UNUSED(title);
        return w;
    };

    m_totalLabel = makeStatLabel(QString());
    m_avgLabel = makeStatLabel(QString());
    m_minLabel = makeStatLabel(QString());
    m_maxLabel = makeStatLabel(QString());

    summaryGrid->addWidget(new QLabel(tr("<b>Effectif total</b>"), this), 0, 0);
    summaryGrid->addWidget(new QLabel(tr("<b>Salaire moyen</b>"), this), 0, 1);
    summaryGrid->addWidget(new QLabel(tr("<b>Salaire min.</b>"), this), 0, 2);
    summaryGrid->addWidget(new QLabel(tr("<b>Salaire max.</b>"), this), 0, 3);
    summaryGrid->addWidget(m_totalLabel, 1, 0);
    summaryGrid->addWidget(m_avgLabel, 1, 1);
    summaryGrid->addWidget(m_minLabel, 1, 2);
    summaryGrid->addWidget(m_maxLabel, 1, 3);

    root->addLayout(summaryGrid);

    m_emptyHint = new QLabel(this);
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setStyleSheet(QStringLiteral("QLabel { color: #6c757d; padding: 16px; }"));
    m_emptyHint->hide();
    root->addWidget(m_emptyHint);

    auto *chartsHost = new QWidget();
    auto *chartsLay = new QGridLayout(chartsHost);
    chartsLay->setContentsMargins(0, 0, 0, 0);
    chartsLay->setSpacing(16);

    m_barChartView = makeChartView();
    m_pieChartView = makeChartView();
    m_lineChartView = makeChartView();
    m_lineChartView->setMinimumHeight(280);

    chartsLay->addWidget(new QLabel(tr("<b>Employés par département</b>"), chartsHost), 0, 0);
    chartsLay->addWidget(new QLabel(tr("<b>Répartition par poste</b>"), chartsHost), 0, 1);
    chartsLay->addWidget(m_barChartView, 1, 0);
    chartsLay->addWidget(m_pieChartView, 1, 1);
    chartsLay->addWidget(new QLabel(tr("<b>Arrivées par année (date de début)</b>"), chartsHost), 2, 0, 1, 2);
    chartsLay->addWidget(m_lineChartView, 3, 0, 1, 2);

    // --- IA / performance (extension: prédiction légère) ---
    auto *aiFrame = new QFrame(chartsHost);
    // Harmoniser avec le reste de l'UI: cartes claires + bordures douces.
    aiFrame->setStyleSheet(QStringLiteral(
        "QFrame { background: #ffffff; border: 1px solid #dbe4ea; border-radius: 12px; }"
        "QLabel { color: #0f172a; }"
        "QComboBox { background: #ffffff; color: #0f172a; border: 1px solid #dbe4ea; border-radius: 8px; padding: 6px 10px; min-height: 34px; }"
        "QListWidget { background: #ffffff; color: #0f172a; border: 1px solid #dbe4ea; border-radius: 10px; padding: 6px; }"));

    auto *aiLayout = new QVBoxLayout(aiFrame);
    aiLayout->setContentsMargins(14, 14, 14, 14);
    aiLayout->setSpacing(10);

    auto *aiHeader = new QHBoxLayout();
    m_aiTitleLabel = new QLabel(tr("<b>Prévision IA — Performance</b>"), aiFrame);
    QFont af = m_aiTitleLabel->font();
    af.setPointSizeF(af.pointSizeF() + 1.5);
    m_aiTitleLabel->setFont(af);

    m_employeePicker = new QComboBox(aiFrame);
    m_employeePicker->setMinimumWidth(260);
    m_employeePicker->setCursor(Qt::PointingHandCursor);
    connect(m_employeePicker, &QComboBox::currentIndexChanged, this, [this](int) {
        // Mettre à jour uniquement la courbe + titre, sans recalcul global.
        const int selId = m_employeePicker ? m_employeePicker->currentData().toInt() : -1;
        if (selId >= 0 && m_cachedAnalytics.contains(selId)) {
            QVector<double> s;
            for (const MonthlySignal &m : m_cachedAnalytics.value(selId).history)
                s.push_back(computeProductivityScore(m));
            PredictionResult pr = predictPerformance(s);
            pr.classification = classifyScore(pr.predictedScore, pr.risk);
            rebuildPerformanceTrendChart(m_cachedAnalytics.value(selId), pr);
        }
    });

    aiHeader->addWidget(m_aiTitleLabel);
    aiHeader->addStretch();
    aiHeader->addWidget(new QLabel(tr("Employé :"), aiFrame));
    aiHeader->addWidget(m_employeePicker);
    aiLayout->addLayout(aiHeader);

    m_aiSubtitleLabel = new QLabel(tr("Scores 0–100, tendances mensuelles, prédiction simple et niveau de confiance."), aiFrame);
    m_aiSubtitleLabel->setStyleSheet(QStringLiteral("QLabel { color: #475569; font-size: 12px; }"));
    aiLayout->addWidget(m_aiSubtitleLabel);

    auto *kpiRow = new QHBoxLayout();
    kpiRow->setSpacing(12);
    auto makeKpi = [aiFrame](const QString &title) {
        auto *kpi = new QFrame(aiFrame);
        kpi->setStyleSheet(QStringLiteral("QFrame { background: #ffffff; border: 1px solid #dbe4ea; border-radius: 12px; }"));
        auto *lay = new QVBoxLayout(kpi);
        lay->setContentsMargins(12, 10, 12, 10);
        lay->setSpacing(4);
        auto *t = new QLabel(title, kpi);
        t->setStyleSheet(QStringLiteral("QLabel { color: #475569; font-size: 12px; }"));
        auto *v = new QLabel(QStringLiteral("—"), kpi);
        v->setStyleSheet(QStringLiteral("QLabel { color: #0f172a; font-size: 18px; font-weight: 800; }"));
        lay->addWidget(t);
        lay->addWidget(v);
        lay->addStretch();
        return qMakePair(kpi, v);
    };
    auto topKpi = makeKpi(tr("Meilleur performer"));
    auto riskKpi = makeKpi(tr("Employés à risque"));
    m_topPerformerLabel = topKpi.second;
    m_riskCountLabel = riskKpi.second;
    kpiRow->addWidget(topKpi.first, 1);
    kpiRow->addWidget(riskKpi.first, 1);
    kpiRow->addStretch(2);
    aiLayout->addLayout(kpiRow);

    auto *aiChartsRow = new QHBoxLayout();
    aiChartsRow->setSpacing(12);
    m_perfBarChartView = makeChartView();
    m_perfTrendChartView = makeChartView();
    m_perfBarChartView->setMinimumHeight(310);
    m_perfTrendChartView->setMinimumHeight(310);
    m_perfBarChartView->setStyleSheet(QStringLiteral("background: #ffffff; border: 1px solid #dbe4ea; border-radius: 12px;"));
    m_perfTrendChartView->setStyleSheet(QStringLiteral("background: #ffffff; border: 1px solid #dbe4ea; border-radius: 12px;"));
    aiChartsRow->addWidget(m_perfBarChartView, 1);
    aiChartsRow->addWidget(m_perfTrendChartView, 1);
    aiLayout->addLayout(aiChartsRow);

    auto *insightsTitle = new QLabel(tr("<b>Insights automatiques</b>"), aiFrame);
    insightsTitle->setStyleSheet(QStringLiteral("QLabel { color: #0f172a; }"));
    aiLayout->addWidget(insightsTitle);

    m_insightsList = new QListWidget(aiFrame);
    m_insightsList->setMinimumHeight(140);
    m_insightsList->setWordWrap(true);
    aiLayout->addWidget(m_insightsList);

    chartsLay->addWidget(new QLabel(tr("<b>IA — Productivité, risque & prédiction</b>"), chartsHost), 4, 0, 1, 2);
    chartsLay->addWidget(aiFrame, 5, 0, 1, 2);

    // Important: pas de scroll interne ici.
    // Le scroll unique est géré au niveau de l'onglet "Statistiques" (MainWindow).
    root->addWidget(chartsHost, 1);
}

void EmployeeStatisticsWidget::clearCharts()
{
    m_barChartView->setChart(new QChart());
    m_pieChartView->setChart(new QChart());
    m_lineChartView->setChart(new QChart());

    if (m_perfBarChartView)
        m_perfBarChartView->setChart(new QChart());
    if (m_perfTrendChartView)
        m_perfTrendChartView->setChart(new QChart());
}

void EmployeeStatisticsWidget::refreshStatistics()
{
    QSqlDatabase db = QSqlDatabase::database();

    // --- (A) Statistiques "classiques" (SQL) ---
    if (!db.isOpen()) {
        m_emptyHint->setText(tr("Base de données non connectée. Les statistiques IA sont simulées."));
        m_emptyHint->show();
        updateSummaryLabels(0, 0.0, 0.0, 0.0, false);
        clearCharts();
    } else {
        int total = 0;
        double avg = 0.0, minSal = 0.0, maxSal = 0.0;
        if (queryAggregates(&total, &avg, &minSal, &maxSal)) {
            const bool hasRows = total > 0;
            updateSummaryLabels(total, avg, minSal, maxSal, hasRows);

            if (!hasRows) {
                m_emptyHint->setText(tr("Aucun employé en base — ajoutez des fiches pour afficher les graphiques."));
                m_emptyHint->show();
                clearCharts();
            } else {
                m_emptyHint->hide();

                QVector<QString> depLabels, postLabels, years;
                QVector<qreal> depVals, postVals, yearCounts;

                if (!queryCountsByDepartment(&depLabels, &depVals))
                    qWarning() << "Statistiques départements:" << QSqlDatabase::database().lastError().text();
                if (!queryCountsByPoste(&postLabels, &postVals))
                    qWarning() << "Statistiques postes:" << QSqlDatabase::database().lastError().text();
                if (!queryCountsByYear(&years, &yearCounts))
                    qWarning() << "Statistiques années:" << QSqlDatabase::database().lastError().text();

                rebuildBarChart(depLabels, depVals);
                rebuildPieChart(postLabels, postVals);
                rebuildLineChart(years, yearCounts);
            }
        } else {
            // Keep the rest functional even if aggregates fail.
            qWarning() << "Statistiques: agrégats indisponibles";
        }
    }

    // --- (B) IA / performance (données DB si possible, sinon simulation) ---
    bool ok = false;
    QVector<EmployeeBase> employees = loadEmployeesBaseFromDb(&ok);
    if (!ok || employees.isEmpty()) {
        // fallback: simulate a small team if base is empty / DB down
        employees.clear();
        const QStringList names{QStringLiteral("Employé A"),
                                QStringLiteral("Employé B"),
                                QStringLiteral("Employé C"),
                                QStringLiteral("Employé D"),
                                QStringLiteral("Employé E")};
        for (int i = 0; i < names.size(); ++i) {
            EmployeeBase e;
            e.id = 1000 + i;
            e.nom = names.at(i);
            e.depart = (i % 2 == 0) ? QStringLiteral("IT") : QStringLiteral("RH");
            e.poste = (i % 3 == 0) ? QStringLiteral("Dév") : QStringLiteral("Analyste");
            e.salaire = 1200.0 + 350.0 * i;
            e.dateDebut = QDate::currentDate().addMonths(-(10 + i * 6));
            employees.push_back(e);
        }
    }

    m_cachedAnalytics = buildOrSimulateAnalytics(employees);

    QVector<EmployeeAnalytics> ranked;
    ranked.reserve(m_cachedAnalytics.size());
    for (const auto &a : m_cachedAnalytics)
        ranked.push_back(a);
    std::sort(ranked.begin(), ranked.end(), [](const EmployeeAnalytics &x, const EmployeeAnalytics &y) {
        return x.productivityScore > y.productivityScore;
    });

    QMap<int, PredictionResult> preds;
    for (const auto &a : ranked) {
        QVector<double> s;
        s.reserve(a.history.size());
        for (const MonthlySignal &m : a.history)
            s.push_back(computeProductivityScore(m));
        PredictionResult pr = predictPerformance(s);
        pr.classification = classifyScore(pr.predictedScore, pr.risk);
        preds.insert(a.base.id, pr);
    }

    const int preferredId = m_employeePicker ? m_employeePicker->currentData().toInt() : -1;
    repopulateEmployeePicker(ranked, preferredId);

    if (!ranked.isEmpty()) {
        const EmployeeAnalytics &top = ranked.first();
        m_topPerformerLabel->setText(QStringLiteral("%1 (%2)")
                                         .arg(top.base.nom)
                                         .arg(QString::number(top.productivityScore, 'f', 1)));
    } else {
        m_topPerformerLabel->setText(QStringLiteral("—"));
    }

    int riskCount = 0;
    for (auto it = preds.constBegin(); it != preds.constEnd(); ++it) {
        if (it.value().classification == QStringLiteral("At Risk"))
            riskCount++;
    }
    m_riskCountLabel->setText(QString::number(riskCount));

    rebuildPerformanceBarChart(ranked, preds);
    rebuildInsightsPanel(ranked, preds);

    // Trend chart for selected employee (or top as default)
    int selId = -1;
    if (m_employeePicker && m_employeePicker->currentIndex() >= 0)
        selId = m_employeePicker->currentData().toInt();
    if (selId < 0 && !ranked.isEmpty())
        selId = ranked.first().base.id;
    if (selId >= 0 && m_cachedAnalytics.contains(selId)) {
        rebuildPerformanceTrendChart(m_cachedAnalytics.value(selId), preds.value(selId));
    } else if (m_perfTrendChartView) {
        m_perfTrendChartView->setChart(new QChart());
    }
}

QVector<EmployeeStatisticsWidget::EmployeeBase> EmployeeStatisticsWidget::loadEmployeesBaseFromDb(bool *ok)
{
    if (ok)
        *ok = false;
    QVector<EmployeeBase> out;

    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isOpen())
        return out;

    QSqlQuery q(db);
    const QString sql = QStringLiteral("SELECT ID, NOM, NVL(DEPART,'') AS DEPART, NVL(POSTE,'') AS POSTE, NVL(SALAIRE,0) AS SALAIRE, DATE_DEBUT FROM EMPLOYEE");
    if (!q.exec(sql)) {
        qWarning() << "loadEmployeesBaseFromDb:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        EmployeeBase e;
        e.id = q.value(QStringLiteral("ID")).toInt();
        e.nom = q.value(QStringLiteral("NOM")).toString();
        e.depart = q.value(QStringLiteral("DEPART")).toString();
        e.poste = q.value(QStringLiteral("POSTE")).toString();
        e.salaire = q.value(QStringLiteral("SALAIRE")).toDouble();
        e.dateDebut = q.value(QStringLiteral("DATE_DEBUT")).toDate();
        out.push_back(e);
    }
    if (ok)
        *ok = true;
    return out;
}

QMap<int, EmployeeStatisticsWidget::EmployeeAnalytics>
EmployeeStatisticsWidget::buildOrSimulateAnalytics(const QVector<EmployeeBase> &employees)
{
    // Note: dans la BD actuelle on n'a (souvent) que les infos RH (nom, poste, salaire...).
    // Pour quand même faire des "stats avancées", je génère une petite série mensuelle réaliste
    // (présence, tâches, retards, note, heures). Le but n'est pas d'être "vrai",
    // mais d'avoir une base cohérente pour les graphiques + la logique de prédiction.
    QMap<int, EmployeeAnalytics> res;
    const int months = 6;
    // Seed stable par jour: l'UI ne "change pas à chaque refresh", mais bouge d'un jour à l'autre.
    const quint32 seed = QDateTime::currentDateTime().date().toJulianDay();

    for (const EmployeeBase &e : employees) {
        EmployeeAnalytics a;
        a.base = e;
        a.history.reserve(months);

        // Quelques "priors" simples pour varier les profils:
        // - ancienneté -> un peu plus stable
        // - salaire -> proxy grossier (pas parfait) d'expérience / responsabilité
        const double tenureMonths = e.dateDebut.isValid() ? (e.dateDebut.daysTo(QDate::currentDate()) / 30.0) : 12.0;
        const double seniority = clamp(tenureMonths / 36.0, 0.0, 1.0);
        const double salaryFactor = clamp((e.salaire - 800.0) / 4000.0, 0.0, 1.0);
        double baselineRating = 2.8 + 1.5 * seniority + 0.7 * salaryFactor;
        baselineRating = clamp(baselineRating, 1.2, 4.8);

        QRandomGenerator rng(seed ^ (quint32(e.id) * 2654435761u));
        // Fatigue = variable interne qui monte si on "tire trop" (beaucoup d'heures + retards).
        // C'est elle qui déclenche le côté "burnout risk" de façon crédible.
        double fatigue = rng.generateDouble() * 0.25; // 0..0.25 au départ

        for (int i = 0; i < months; ++i) {
            const double seasonal = 0.08 * std::sin((i / double(months)) * 3.14159 * 2.0);
            const double noise = (rng.generateDouble() - 0.5) * 0.18;

            MonthlySignal m;
            m.workingHours = clamp(168.0 + 18.0 * (rng.generateDouble() - 0.5) + 40.0 * fatigue, 140.0, 235.0);
            m.attendance = clamp01(0.92 - 0.25 * fatigue + seasonal + (rng.generateDouble() - 0.5) * 0.08);
            m.tasksDone = clamp(18.0 + 22.0 * seniority + 8.0 * salaryFactor + 6.0 * seasonal + 10.0 * noise, 6.0, 75.0);
            m.delays = clamp(2.0 + 6.0 * fatigue + 2.0 * (rng.generateDouble()), 0.0, 25.0);
            m.rating = clamp(baselineRating - 0.9 * fatigue + 0.6 * seasonal + 0.6 * noise, 1.0, 5.0);

            // Petite marche aléatoire: si heures hautes + retards -> fatigue monte,
            // sinon la présence "récupère" un peu.
            const double fatiguePush = clamp01((m.workingHours - 195.0) / 60.0) * 0.10 + clamp01(m.delays / 18.0) * 0.08;
            fatigue = clamp(fatigue + fatiguePush - 0.05 * clamp01(m.attendance), 0.0, 1.0);

            a.history.push_back(m);
        }

        a.productivityScore = computeCompositeScoreFromHistory(a.history);
        res.insert(e.id, a);
    }
    return res;
}

double EmployeeStatisticsWidget::computeProductivityScore(const MonthlySignal &m) const
{
    // Score 0..100 (simple mais lisible):
    // - présence, tâches, note -> boost
    // - retards -> pénalité
    // - heures -> petit bonus (mais si trop d'heures, on met une pénalité "burnout")
    const double tasksN = clamp(m.tasksDone / 55.0, 0.0, 1.0);
    const double delaysN = clamp(m.delays / 18.0, 0.0, 1.0);
    const double ratingN = clamp((m.rating - 1.0) / 4.0, 0.0, 1.0);
    const double hoursN = clamp((m.workingHours - 150.0) / 80.0, 0.0, 1.0);

    // À partir de ~210h/mois, je considère que ça sent le surmenage => on enlève des points.
    const double burnoutPenalty = clamp01((m.workingHours - 210.0) / 40.0) * 0.18;
    const double score01 = 0.26 * clamp01(m.attendance)
        + 0.30 * tasksN
        + 0.30 * ratingN
        + 0.14 * (1.0 - delaysN)
        + 0.10 * hoursN
        - burnoutPenalty;

    return clamp(score01, 0.0, 1.0) * 100.0;
}

double EmployeeStatisticsWidget::computeCompositeScoreFromHistory(const QVector<MonthlySignal> &hist) const
{
    if (hist.isEmpty())
        return 0.0;
    double sum = 0.0;
    double wsum = 0.0;
    for (int i = 0; i < hist.size(); ++i) {
        const double w = 0.8 + 0.2 * (double(i) / double(qMax(1, hist.size() - 1))); // slight emphasis on recent
        sum += w * computeProductivityScore(hist.at(i));
        wsum += w;
    }
    return wsum > 0.0 ? (sum / wsum) : 0.0;
}

EmployeeStatisticsWidget::PredictionResult EmployeeStatisticsWidget::predictPerformance(const QVector<double> &scores) const
{
    // "Pseudo-ML" volontairement léger:
    // Régression linéaire sur les derniers mois (y = a + b*x) puis prédiction du mois prochain.
    // Suffisant pour donner une tendance sans dépendances ML.
    PredictionResult r;
    if (scores.isEmpty()) {
        r.predictedScore = 0.0;
        r.confidence = 0.0;
        r.risk = true;
        r.message = tr("Aucune donnée disponible.");
        return r;
    }

    const int n = scores.size();
    double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumXY = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = double(i);
        const double y = scores.at(i);
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
    }
    const double denom = n * sumXX - sumX * sumX;
    const double b = (denom != 0.0) ? (n * sumXY - sumX * sumY) / denom : 0.0;
    const double a = (sumY - b * sumX) / double(n);
    const double nextX = double(n);
    const double pred = clamp(a + b * nextX, 0.0, 100.0);
    r.predictedScore = pred;

    // Confiance: mélange de "qualité d'ajustement" (type R²) + stabilité de pente.
    // Plus la tendance est erratique, plus la confiance baisse.
    double ssTot = 0.0;
    double ssRes = 0.0;
    const double meanY = sumY / double(n);
    for (int i = 0; i < n; ++i) {
        const double y = scores.at(i);
        const double yhat = a + b * double(i);
        ssTot += (y - meanY) * (y - meanY);
        ssRes += (y - yhat) * (y - yhat);
    }
    const double r2 = (ssTot > 0.0) ? clamp01(1.0 - (ssRes / ssTot)) : 0.5;
    const double slopeConf = clamp01(1.0 - (std::abs(b) / 18.0)); // very steep trends -> less stable
    r.confidence = clamp01(0.35 + 0.50 * r2 + 0.15 * slopeConf);

    // "À risque" si:
    // - score prévu bas
    // - ou pente négative marquée + niveau actuel pas très haut
    const double last = scores.last();
    const bool declining = (b < -2.5);
    r.risk = (pred < 50.0) || (declining && last < 65.0);

    if (b > 2.0)
        r.message = tr("La performance est attendue en hausse.");
    else if (b < -2.0)
        r.message = tr("Des signes de déclin sont détectés.");
    else
        r.message = tr("La performance semble stable à court terme.");

    return r;
}

QString EmployeeStatisticsWidget::classifyScore(double score, bool risk) const
{
    if (risk || score < 55.0)
        return QStringLiteral("À risque");
    if (score >= 75.0)
        return QStringLiteral("Top performer");
    return QStringLiteral("متوسط");
}

QColor EmployeeStatisticsWidget::classColor(const QString &classification) const
{
    if (classification == QStringLiteral("Top performer"))
        return QColor(kGreenDark);
    if (classification == QStringLiteral("À risque"))
        return QColor(kRed);
    return QColor(kYellow);
}

void EmployeeStatisticsWidget::repopulateEmployeePicker(const QVector<EmployeeAnalytics> &ranked, int preferredId)
{
    if (!m_employeePicker)
        return;

    const QSignalBlocker blocker(m_employeePicker);
    m_employeePicker->clear();

    int selectIndex = -1;
    for (int i = 0; i < ranked.size(); ++i) {
        const EmployeeAnalytics &a = ranked.at(i);
        const PredictionResult pr = predictPerformance([&]() {
            QVector<double> s;
            for (const MonthlySignal &m : a.history)
                s.push_back(computeProductivityScore(m));
            return s;
        }());
        const QString cls = classifyScore(pr.predictedScore, pr.risk);
        const QString badge = (cls == QStringLiteral("Top performer")) ? QStringLiteral("↑")
            : (cls == QStringLiteral("À risque")) ? QStringLiteral("⚠") : QStringLiteral("•");
        const QString label = QStringLiteral("%1 %2 — %3")
                                  .arg(badge, a.base.nom,
                                       QString::number(a.productivityScore, 'f', 1));
        m_employeePicker->addItem(label, a.base.id);
        if (a.base.id == preferredId)
            selectIndex = i;
    }
    if (selectIndex >= 0)
        m_employeePicker->setCurrentIndex(selectIndex);
    else if (m_employeePicker->count() > 0)
        m_employeePicker->setCurrentIndex(0);
}

void EmployeeStatisticsWidget::rebuildPerformanceBarChart(const QVector<EmployeeAnalytics> &ranked,
                                                         const QMap<int, PredictionResult> &preds)
{
    if (!m_perfBarChartView)
        return;

    auto *chart = new QChart();
    chart->setBackgroundVisible(false);
    chart->setTitle(tr("Comparaison de productivité (Top) — couleurs : vert / jaune / rouge"));
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->legend()->setVisible(false);

    if (ranked.isEmpty()) {
        m_perfBarChartView->setChart(chart);
        return;
    }

    const int topN = qMin(8, ranked.size());
    QStringList categories;
    categories.reserve(topN);

    auto *series = new QBarSeries();

    for (int i = 0; i < topN; ++i) {
        const EmployeeAnalytics &a = ranked.at(i);
        const PredictionResult pr = preds.value(a.base.id);
        auto *set = new QBarSet(a.base.nom);
        set->append(a.productivityScore);
        set->setColor(classColor(pr.classification));
        series->append(set);
        categories << QStringLiteral("#%1").arg(i + 1);
    }

    chart->addSeries(series);

    auto *axisX = new QBarCategoryAxis();
    axisX->append(categories);
    axisX->setTitleText(tr("Classement"));
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    auto *axisY = new QValueAxis();
    axisY->setTitleText(tr("Score (0–100)"));
    axisY->setRange(0.0, 100.0);
    axisY->setTickCount(6);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    m_perfBarChartView->setChart(chart);
}

void EmployeeStatisticsWidget::rebuildPerformanceTrendChart(const EmployeeAnalytics &a,
                                                           const PredictionResult &pred)
{
    if (!m_perfTrendChartView)
        return;

    auto *chart = new QChart();
    chart->setBackgroundVisible(false);
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setTitle(tr("Évolution & prédiction — %1").arg(a.base.nom));

    auto *actual = new QLineSeries();
    actual->setName(tr("Score réel"));
    QPen ap(classColor(pred.classification));
    ap.setWidth(3);
    actual->setPen(ap);

    QVector<double> scores;
    for (int i = 0; i < a.history.size(); ++i) {
        const double s = computeProductivityScore(a.history.at(i));
        scores.push_back(s);
        actual->append(i, s);
    }

    auto *forecast = new QLineSeries();
    forecast->setName(tr("Prévision"));
    QPen fp(QColor("#93c5fd"));
    fp.setWidth(2);
    fp.setStyle(Qt::DashLine);
    forecast->setPen(fp);
    if (!scores.isEmpty()) {
        const int lastX = scores.size() - 1;
        forecast->append(lastX, scores.last());
        forecast->append(lastX + 1, pred.predictedScore);
    }

    chart->addSeries(actual);
    chart->addSeries(forecast);

    auto *axisX = new QValueAxis();
    axisX->setTitleText(tr("Mois (historique → futur)"));
    axisX->setLabelFormat(QStringLiteral("%d"));
    axisX->setTickCount(qMax(3, scores.size() + 2));
    axisX->setRange(0.0, qMax(2.0, double(scores.size())));
    chart->addAxis(axisX, Qt::AlignBottom);
    actual->attachAxis(axisX);
    forecast->attachAxis(axisX);

    auto *axisY = new QValueAxis();
    axisY->setTitleText(tr("Score (0–100)"));
    axisY->setRange(0.0, 100.0);
    axisY->setTickCount(6);
    chart->addAxis(axisY, Qt::AlignLeft);
    actual->attachAxis(axisY);
    forecast->attachAxis(axisY);

    chart->legend()->setAlignment(Qt::AlignBottom);

    // Subtitle-like info via small note in title
        const QString extra = tr("%1  |  Prévision : %2  |  Confiance : %3%")
                              .arg(pred.message,
                                   QString::number(pred.predictedScore, 'f', 1),
                                   QString::number(pred.confidence * 100.0, 'f', 0));
    chart->setTitle(chart->title() + QStringLiteral("\n") + extra);

    m_perfTrendChartView->setChart(chart);
}

void EmployeeStatisticsWidget::rebuildInsightsPanel(const QVector<EmployeeAnalytics> &ranked,
                                                   const QMap<int, PredictionResult> &preds)
{
    if (!m_insightsList)
        return;

    m_insightsList->clear();
    if (ranked.isEmpty()) {
        m_insightsList->addItem(tr("Aucune donnée employé."));
        return;
    }

    const int showN = qMin(10, ranked.size());
    for (int i = 0; i < showN; ++i) {
        const EmployeeAnalytics &a = ranked.at(i);
        const PredictionResult pr = preds.value(a.base.id);

        QString line = QStringLiteral("%1 — %2 | actuel %3 → prévu %4 | confiance %5%")
                           .arg(a.base.nom,
                                pr.classification,
                                QString::number(a.productivityScore, 'f', 1),
                                QString::number(pr.predictedScore, 'f', 1),
                                QString::number(pr.confidence * 100.0, 'f', 0));
        if (pr.risk)
            line += tr(" | Risque : burnout / baisse");

        auto *item = new QListWidgetItem(line);
        item->setForeground(QBrush(classColor(pr.classification)));
        m_insightsList->addItem(item);

        // Smart message (separate)
        QString msg = QStringLiteral("• %1: %2").arg(a.base.nom, pr.message);
        auto *msgItem = new QListWidgetItem(msg);
        msgItem->setForeground(QBrush(QColor("#475569")));
        m_insightsList->addItem(msgItem);
    }
}

bool EmployeeStatisticsWidget::queryAggregates(int *total, double *avg, double *minSal, double *maxSal)
{
    QSqlQuery q(QSqlDatabase::database());
    const QString sql = QStringLiteral(
        "SELECT COUNT(*), AVG(SALAIRE), MIN(SALAIRE), MAX(SALAIRE) FROM EMPLOYEE");
    if (!q.exec(sql) || !q.next()) {
        qWarning() << "queryAggregates:" << q.lastError().text();
        return false;
    }
    *total = q.value(0).toInt();
    *avg = q.value(1).toDouble();
    *minSal = q.value(2).toDouble();
    *maxSal = q.value(3).toDouble();
    return true;
}

bool EmployeeStatisticsWidget::queryCountsByDepartment(QVector<QString> *labels, QVector<qreal> *values)
{
    labels->clear();
    values->clear();
    QSqlQuery q(QSqlDatabase::database());
    const QString sql = QStringLiteral(
        "SELECT NVL(NULLIF(TRIM(DEPART), ''), '(Non défini)') AS DEPT, COUNT(*) AS CNT "
        "FROM EMPLOYEE GROUP BY NVL(NULLIF(TRIM(DEPART), ''), '(Non défini)') "
        "ORDER BY COUNT(*) DESC");
    if (!q.exec(sql))
        return false;
    while (q.next()) {
        labels->append(q.value(0).toString());
        values->append(q.value(1).toReal());
    }
    return true;
}

bool EmployeeStatisticsWidget::queryCountsByPoste(QVector<QString> *labels, QVector<qreal> *values)
{
    labels->clear();
    values->clear();
    QSqlQuery q(QSqlDatabase::database());
    const QString sql = QStringLiteral(
        "SELECT NVL(NULLIF(TRIM(POSTE), ''), '(Non défini)') AS P, COUNT(*) AS CNT "
        "FROM EMPLOYEE GROUP BY NVL(NULLIF(TRIM(POSTE), ''), '(Non défini)') "
        "ORDER BY COUNT(*) DESC");
    if (!q.exec(sql))
        return false;
    while (q.next()) {
        labels->append(q.value(0).toString());
        values->append(q.value(1).toReal());
    }
    return true;
}

bool EmployeeStatisticsWidget::queryCountsByYear(QVector<QString> *years, QVector<qreal> *counts)
{
    years->clear();
    counts->clear();
    QSqlQuery q(QSqlDatabase::database());
    const QString sql = QStringLiteral(
        "SELECT TO_CHAR(DATE_DEBUT, 'YYYY') AS YR, COUNT(*) AS CNT "
        "FROM EMPLOYEE WHERE DATE_DEBUT IS NOT NULL "
        "GROUP BY TO_CHAR(DATE_DEBUT, 'YYYY') "
        "ORDER BY TO_CHAR(DATE_DEBUT, 'YYYY')");
    if (!q.exec(sql))
        return false;
    while (q.next()) {
        years->append(q.value(0).toString());
        counts->append(q.value(1).toReal());
    }
    return true;
}

void EmployeeStatisticsWidget::updateSummaryLabels(int total, double avg, double minSal, double maxSal, bool hasRows)
{
    const QLocale loc = QLocale::system();
    if (!hasRows) {
        m_totalLabel->setText(QStringLiteral("0"));
        m_avgLabel->setText(QStringLiteral("—"));
        m_minLabel->setText(QStringLiteral("—"));
        m_maxLabel->setText(QStringLiteral("—"));
        return;
    }
    m_totalLabel->setText(loc.toString(total));
    m_avgLabel->setText(loc.toString(avg, 'f', 2));
    m_minLabel->setText(loc.toString(minSal, 'f', 2));
    m_maxLabel->setText(loc.toString(maxSal, 'f', 2));
}

void EmployeeStatisticsWidget::rebuildBarChart(const QVector<QString> &labels, const QVector<qreal> &values)
{
    auto *chart = new QChart();
    chart->setTitle(tr("Effectifs par département"));
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->legend()->setVisible(false);

    if (labels.isEmpty()) {
        chart->setTitle(tr("Aucune donnée département"));
        m_barChartView->setChart(chart);
        return;
    }

    auto *series = new QBarSeries();
    auto *barSet = new QBarSet(tr("Employés"));
    barSet->setColor(QColor(kGreenDark));
    for (qreal v : values)
        *barSet << v;
    series->append(barSet);

    chart->addSeries(series);

    auto *axisX = new QBarCategoryAxis();
    for (const QString &s : labels)
        axisX->append(s);
    axisX->setLabelsAngle(-35);
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    auto *axisY = new QValueAxis();
    axisY->setLabelFormat(QStringLiteral("%d"));
    axisY->setTitleText(tr("Nombre"));
    qreal maxY = 1.0;
    for (qreal v : values)
        maxY = qMax(maxY, v);
    axisY->setRange(0, maxY * 1.15);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    m_barChartView->setChart(chart);
}

void EmployeeStatisticsWidget::rebuildPieChart(const QVector<QString> &labels, const QVector<qreal> &values)
{
    auto *chart = new QChart();
    chart->setTitle(tr("Part des postes"));
    chart->setAnimationOptions(QChart::SeriesAnimations);

    auto *series = new QPieSeries();
    if (labels.isEmpty()) {
        chart->setTitle(tr("Aucune donnée poste"));
        m_pieChartView->setChart(chart);
        return;
    }

    const int n = labels.size();
    const qreal sum = std::accumulate(values.begin(), values.end(), 0.0);

    for (int i = 0; i < n; ++i) {
        series->append(labels.at(i), values.at(i));
        QPieSlice *slice = series->slices().last();
        const qreal t = n > 1 ? qreal(i) / qreal(n - 1) : 0.0;
        const QColor ca(kGreenDark);
        const QColor cb(kGreenLight);
        const QColor c = QColor::fromRgb(
            qRound(ca.red() + (cb.red() - ca.red()) * t),
            qRound(ca.green() + (cb.green() - ca.green()) * t),
            qRound(ca.blue() + (cb.blue() - ca.blue()) * t));
        slice->setBrush(c);
        const qreal pct = sum > 0.0 ? 100.0 * values.at(i) / sum : 0.0;
        slice->setLabel(QStringLiteral("%1: %2%").arg(labels.at(i)).arg(pct, 0, 'f', 1));
    }
    series->setLabelsVisible(true);
    series->setLabelsPosition(QPieSlice::LabelOutside);

    chart->addSeries(series);
    chart->legend()->setAlignment(Qt::AlignRight);

    m_pieChartView->setChart(chart);
}

void EmployeeStatisticsWidget::rebuildLineChart(const QVector<QString> &years, const QVector<qreal> &counts)
{
    auto *chart = new QChart();
    chart->setTitle(tr("Effectifs par année d'embauche"));
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->legend()->setVisible(false);

    auto *series = new QLineSeries();
    series->setName(tr("Employés"));
    series->setColor(QColor(kGreenDark));

    if (years.isEmpty()) {
        chart->setTitle(tr("Pas d'année (dates de début vides)"));
        m_lineChartView->setChart(chart);
        return;
    }

    bool firstX = true;
    double minX = 0.0;
    double maxX = 0.0;
    for (int i = 0; i < years.size(); ++i) {
        bool ok = false;
        double xv = years.at(i).toDouble(&ok);
        if (!ok)
            xv = double(i);
        series->append(xv, counts.at(i));
        if (firstX) {
            minX = maxX = xv;
            firstX = false;
        } else {
            minX = qMin(minX, xv);
            maxX = qMax(maxX, xv);
        }
    }

    chart->addSeries(series);

    auto *axisX = new QValueAxis();
    axisX->setLabelFormat(QStringLiteral("%d"));
    axisX->setTitleText(tr("Année"));
    axisX->setRange(minX - 0.5, maxX + 0.5);
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    auto *axisY = new QValueAxis();
    axisY->setLabelFormat(QStringLiteral("%d"));
    axisY->setTitleText(tr("Nombre"));
    qreal maxY = 1.0;
    for (qreal v : counts)
        maxY = qMax(maxY, v);
    axisY->setRange(0, maxY * 1.15);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    m_lineChartView->setChart(chart);
}
