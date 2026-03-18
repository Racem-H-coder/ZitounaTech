#ifndef EMPLOYEESTATISTICSWIDGET_H
#define EMPLOYEESTATISTICSWIDGET_H

#include <QDate>
#include <QMap>
#include <QVector>
#include <QWidget>
#include <QtCharts/QChartView>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QComboBox;
class QListWidget;
QT_END_NAMESPACE

/** Charge les statistiques employés (Oracle) et affiche indicateurs + graphiques Qt Charts. */
class EmployeeStatisticsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit EmployeeStatisticsWidget(QWidget *parent = nullptr);

public slots:
    void refreshStatistics();

private:
    // --- Données & IA (simulation légère / fallback si DB incomplète) ---
    struct EmployeeBase {
        int id = 0;
        QString nom;
        QString depart;
        QString poste;
        double salaire = 0.0;
        QDate dateDebut;
    };

    struct MonthlySignal {
        double attendance = 0.0;      // 0..1
        double tasksDone = 0.0;       // >=0
        double delays = 0.0;          // >=0
        double rating = 0.0;          // 1..5
        double workingHours = 0.0;    // ~140..240 / month
    };

    struct EmployeeAnalytics {
        EmployeeBase base;
        QVector<MonthlySignal> history;   // oldest -> newest (last N months)
        double productivityScore = 0.0;   // 0..100
    };

    struct PredictionResult {
        double predictedScore = 0.0;      // 0..100
        double confidence = 0.0;          // 0..1
        bool risk = false;
        QString classification;           // High Performer | متوسط | At Risk
        QString message;
    };

    void buildUi();
    void clearCharts();

    QVector<EmployeeBase> loadEmployeesBaseFromDb(bool *ok);
    QMap<int, EmployeeAnalytics> buildOrSimulateAnalytics(const QVector<EmployeeBase> &employees);
    double computeProductivityScore(const MonthlySignal &m) const;
    double computeCompositeScoreFromHistory(const QVector<MonthlySignal> &hist) const;
    PredictionResult predictPerformance(const QVector<double> &scores) const;
    QString classifyScore(double score, bool risk) const;
    QColor classColor(const QString &classification) const;

    bool queryAggregates(int *total, double *avg, double *minSal, double *maxSal);
    bool queryCountsByDepartment(QVector<QString> *labels, QVector<qreal> *values);
    bool queryCountsByPoste(QVector<QString> *labels, QVector<qreal> *values);
    bool queryCountsByYear(QVector<QString> *years, QVector<qreal> *counts);

    void updateSummaryLabels(int total, double avg, double minSal, double maxSal, bool hasRows);
    void rebuildBarChart(const QVector<QString> &labels, const QVector<qreal> &values);
    void rebuildPieChart(const QVector<QString> &labels, const QVector<qreal> &values);
    void rebuildLineChart(const QVector<QString> &years, const QVector<qreal> &counts);

    // --- UI update (IA) ---
    void rebuildPerformanceBarChart(const QVector<EmployeeAnalytics> &ranked,
                                    const QMap<int, PredictionResult> &preds);
    void rebuildPerformanceTrendChart(const EmployeeAnalytics &a,
                                      const PredictionResult &pred);
    void rebuildInsightsPanel(const QVector<EmployeeAnalytics> &ranked,
                              const QMap<int, PredictionResult> &preds);
    void repopulateEmployeePicker(const QVector<EmployeeAnalytics> &ranked, int preferredId);

    QLabel *m_titleLabel = nullptr;
    QLabel *m_totalLabel = nullptr;
    QLabel *m_avgLabel = nullptr;
    QLabel *m_minLabel = nullptr;
    QLabel *m_maxLabel = nullptr;
    QLabel *m_emptyHint = nullptr;

    QChartView *m_barChartView = nullptr;
    QChartView *m_pieChartView = nullptr;
    QChartView *m_lineChartView = nullptr;

    QPushButton *m_refreshBtn = nullptr;

    // --- IA widgets ---
    QLabel *m_aiTitleLabel = nullptr;
    QLabel *m_aiSubtitleLabel = nullptr;
    QLabel *m_topPerformerLabel = nullptr;
    QLabel *m_riskCountLabel = nullptr;
    QComboBox *m_employeePicker = nullptr;
    QChartView *m_perfBarChartView = nullptr;
    QChartView *m_perfTrendChartView = nullptr;
    QListWidget *m_insightsList = nullptr;

    QMap<int, EmployeeAnalytics> m_cachedAnalytics;
};

#endif // EMPLOYEESTATISTICSWIDGET_H
