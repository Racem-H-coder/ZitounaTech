#ifndef PLANNINGWIDGET_H
#define PLANNINGWIDGET_H

#include <QDate>
#include <QPointer>
#include <QWidget>

#include "leavemanager.h"
#include "schedulingengine.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QDateEdit;
class QFrame;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTextEdit;
QT_END_NAMESPACE

class PlanningWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlanningWidget(QWidget *parent = nullptr);

private slots:
    void onGenerateClicked();
    void onOptimizeClicked();

    void onAddLeaveClicked();
    void onApproveLeaveClicked();
    void onRejectLeaveClicked();
    void onLeaveSelectionChanged();

private:
    void buildUi();
    void loadEmployees();
    void renderSchedule(const SchedulingEngine::ScheduleResult &result);
    void renderLeaveHistory();
    void renderWarnings(const QStringList &warnings);

    // Recommandation "IA" (heuristique) pour la validation d'un congé.
    struct LeaveAiRecommendation {
        QString decision;     // Accepter | Peut-être | Refuser
        double confidence = 0.0; // 0..1
        QString reason;
    };
    LeaveAiRecommendation recommendLeave(const LeaveManager::LeaveRequest &req) const;
    void updateLeaveAiPanel(const LeaveManager::LeaveRequest *req);

    int selectedEmployeeId() const;
    QString selectedEmployeeName() const;
    int selectedLeaveRequestId() const;

private:
    // UI
    QLabel *m_title = nullptr;
    QLabel *m_hint = nullptr;

    QPushButton *m_generateBtn = nullptr;
    QPushButton *m_optimizeBtn = nullptr;

    QDateEdit *m_weekStartEdit = nullptr;
    QTableWidget *m_scheduleTable = nullptr;
    QTextEdit *m_warningsView = nullptr;

    // Congés
    QComboBox *m_leaveEmployeeCombo = nullptr;
    QDateEdit *m_leaveStartEdit = nullptr;
    QDateEdit *m_leaveEndEdit = nullptr;
    QPushButton *m_addLeaveBtn = nullptr;

    QPushButton *m_approveBtn = nullptr;
    QPushButton *m_rejectBtn = nullptr;
    QTableWidget *m_leaveHistoryTable = nullptr;

    QLabel *m_leaveAiLabel = nullptr;
    QTextEdit *m_leaveAiDetail = nullptr;

    // Logic
    LeaveManager *m_leaveMgr = nullptr;
    SchedulingEngine m_engine;
    SchedulingEngine::ScheduleResult m_lastSchedule;
};

#endif // PLANNINGWIDGET_H

