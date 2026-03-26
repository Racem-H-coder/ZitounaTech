#ifndef SCHEDULINGENGINE_H
#define SCHEDULINGENGINE_H

#include <QDate>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

class LeaveManager;

// Algorithme "intelligent" léger:
// - scoring employé↔tâche basé sur compétences + charge
// - évite congés et surcharge
class SchedulingEngine
{
public:
    struct Employee {
        int id = 0;
        QString name;
        QString depart;
        QString poste;
        QStringList skills;
        int weeklyCapacityHours = 40;
    };

    struct Task {
        QString name;
        QStringList requiredSkills;
        int durationHours = 2;
        int priority = 1; // 1..5
    };

    struct Assignment {
        int employeeId = 0;
        QString employeeName;
        QDate day;
        Task task;
        int startHour = 8; // simplifié
        int endHour = 10;

        double matchScore = 0.0;   // 0..1
        bool overload = false;
        bool onLeave = false;
        QString warning;           // message si conflit/skill faible
    };

    struct ScheduleResult {
        QVector<Employee> employees;
        QVector<QDate> days;                 // colonnes
        QVector<Assignment> assignments;      // lignes logiques
        QStringList globalWarnings;
        QMap<int, int> weeklyLoadHours;       // employeeId -> heures affectées
    };

    SchedulingEngine();

    QVector<Employee> inferEmployeesFromDb(bool *ok) const;
    QVector<Task> defaultTasksForOlivePress() const;

    ScheduleResult generateWeekSchedule(const QVector<Employee> &employees,
                                        const QVector<Task> &tasks,
                                        const QDate &weekStart,
                                        const LeaveManager *leaveManager) const;

    ScheduleResult optimizeSchedule(const ScheduleResult &current,
                                   const LeaveManager *leaveManager) const;

private:
    QStringList simulateSkills(const QString &depart, const QString &poste, int employeeId) const;
    double matchScore(const Employee &e, const Task &t) const;
};

#endif // SCHEDULINGENGINE_H

