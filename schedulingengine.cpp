#include "schedulingengine.h"

#include "leavemanager.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

SchedulingEngine::SchedulingEngine() {}

QVector<SchedulingEngine::Employee> SchedulingEngine::inferEmployeesFromDb(bool *ok) const
{
    if (ok)
        *ok = false;
    QVector<Employee> out;

    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isOpen())
        return out;

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT ID, NOM, NVL(DEPART,''), NVL(POSTE,'') FROM EMPLOYEE"))) {
        return out;
    }
    while (q.next()) {
        Employee e;
        e.id = q.value(0).toInt();
        e.name = q.value(1).toString();
        e.depart = q.value(2).toString();
        e.poste = q.value(3).toString();
        e.skills = simulateSkills(e.depart, e.poste, e.id);
        out.push_back(e);
    }
    if (ok)
        *ok = true;
    return out;
}

QStringList SchedulingEngine::simulateSkills(const QString &depart, const QString &poste, int employeeId) const
{
    // Tant qu'on n'a pas une table COMPETENCE, on infère des compétences "plausibles" via poste/département.
    // Seed stable par jour pour garder une certaine cohérence visuelle.
    const quint32 seed = QDate::currentDate().toJulianDay() ^ (quint32(employeeId) * 2654435761u);
    QRandomGenerator rng(seed);

    QStringList s;
    const QString d = depart.toLower();
    const QString p = poste.toLower();

    // Base agro / presse d'olive
    if (d.contains(QStringLiteral("production")) || p.contains(QStringLiteral("op")) || p.contains(QStringLiteral("techn")))
        s << QStringLiteral("Conduite presse") << QStringLiteral("Réglage paramètres") << QStringLiteral("Sécurité machine");
    if (d.contains(QStringLiteral("qual")) || p.contains(QStringLiteral("qual")))
        s << QStringLiteral("HACCP") << QStringLiteral("Traçabilité") << QStringLiteral("Contrôle qualité");
    if (p.contains(QStringLiteral("maint")) || d.contains(QStringLiteral("maint")))
        s << QStringLiteral("Maintenance 1er niveau") << QStringLiteral("Diagnostic") << QStringLiteral("Lubrification");

    // Quelques compétences transverses
    const QStringList pool{
        QStringLiteral("Nettoyage/CIP"),
        QStringLiteral("Filtration"),
        QStringLiteral("Conditionnement"),
        QStringLiteral("ERP/Excel"),
        QStringLiteral("Gestion stocks"),
        QStringLiteral("Reporting"),
        QStringLiteral("Teamwork")
    };
    for (const QString &x : pool) {
        if (rng.generateDouble() < 0.35)
            s << x;
    }

    s.removeDuplicates();
    if (s.isEmpty())
        s << QStringLiteral("Polyvalence");
    return s;
}

QVector<SchedulingEngine::Task> SchedulingEngine::defaultTasksForOlivePress() const
{
    // Tâches types sur une semaine pour une société de presse d'olive.
    return {
        {QStringLiteral("Réception olives & tri"), {QStringLiteral("Teamwork"), QStringLiteral("Sécurité machine")}, 3, 3},
        {QStringLiteral("Extraction / Presse"), {QStringLiteral("Conduite presse"), QStringLiteral("Réglage paramètres")}, 4, 5},
        {QStringLiteral("Centrifugation"), {QStringLiteral("Réglage paramètres")}, 3, 4},
        {QStringLiteral("Filtration"), {QStringLiteral("Filtration")}, 2, 3},
        {QStringLiteral("Contrôle qualité (acidité/lot)"), {QStringLiteral("Contrôle qualité"), QStringLiteral("Traçabilité")}, 2, 4},
        {QStringLiteral("Nettoyage & CIP"), {QStringLiteral("Nettoyage/CIP"), QStringLiteral("HACCP")}, 2, 4},
        {QStringLiteral("Maintenance 1er niveau"), {QStringLiteral("Maintenance 1er niveau")}, 2, 3},
        {QStringLiteral("Conditionnement & étiquetage"), {QStringLiteral("Conditionnement"), QStringLiteral("Traçabilité")}, 3, 3},
        {QStringLiteral("Reporting fin de journée"), {QStringLiteral("Reporting")}, 1, 2},
    };
}

double SchedulingEngine::matchScore(const Employee &e, const Task &t) const
{
    if (t.requiredSkills.isEmpty())
        return 0.7;
    int hit = 0;
    for (const QString &req : t.requiredSkills) {
        if (e.skills.contains(req, Qt::CaseInsensitive))
            hit++;
    }
    return double(hit) / double(t.requiredSkills.size());
}

SchedulingEngine::ScheduleResult SchedulingEngine::generateWeekSchedule(const QVector<Employee> &employees,
                                                                        const QVector<Task> &tasks,
                                                                        const QDate &weekStart,
                                                                        const LeaveManager *leaveManager) const
{
    ScheduleResult r;
    r.employees = employees;

    // 5 jours ouvrés (lun→ven)
    QDate start = weekStart;
    if (start.dayOfWeek() != 1)
        start = start.addDays(1 - start.dayOfWeek());
    for (int i = 0; i < 5; ++i)
        r.days.push_back(start.addDays(i));

    // Charge actuelle par employé
    for (const Employee &e : employees)
        r.weeklyLoadHours.insert(e.id, 0);

    if (employees.isEmpty()) {
        r.globalWarnings << QStringLiteral("Aucun employé disponible.");
        return r;
    }
    if (tasks.isEmpty()) {
        r.globalWarnings << QStringLiteral("Aucune tâche à planifier.");
        return r;
    }

    // On planifie chaque jour une "liste" de tâches (priorité haute d'abord).
    QVector<Task> sortedTasks = tasks;
    std::sort(sortedTasks.begin(), sortedTasks.end(), [](const Task &a, const Task &b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.durationHours > b.durationHours;
    });

    for (const QDate &day : r.days) {
        // on affecte 4 tâches par jour max (simple visuel)
        const int dailyTasks = qMin(4, sortedTasks.size());
        for (int ti = 0; ti < dailyTasks; ++ti) {
            const Task &t = sortedTasks.at(ti);

            // Choisir le meilleur employé: skills + disponibilité + charge faible
            int bestId = -1;
            double bestScore = -1.0;
            QString bestWarn;
            for (const Employee &e : employees) {
                const bool onLeave = leaveManager ? leaveManager->isOnApprovedLeave(e.id, day) : false;
                if (onLeave)
                    continue;
                const double ms = matchScore(e, t);
                const int load = r.weeklyLoadHours.value(e.id, 0);
                const double loadPenalty = double(load) / double(qMax(1, e.weeklyCapacityHours));
                const double total = 0.70 * ms + 0.30 * (1.0 - loadPenalty);
                if (total > bestScore) {
                    bestScore = total;
                    bestId = e.id;
                    bestWarn = (ms < 0.5) ? QStringLiteral("Match compétences faible") : QString();
                }
            }

            Assignment a;
            a.day = day;
            a.task = t;
            a.matchScore = qMax(0.0, bestScore);

            if (bestId < 0) {
                a.employeeId = 0;
                a.employeeName = QStringLiteral("Non assigné");
                a.warning = QStringLiteral("Aucun employé disponible (congés / charge)");
                r.globalWarnings << QStringLiteral("Tâche '%1' non assignée le %2.")
                                        .arg(t.name, day.toString(QStringLiteral("dd/MM/yyyy")));
            } else {
                const Employee *ep = nullptr;
                for (const Employee &e : employees) {
                    if (e.id == bestId) {
                        ep = &e;
                        break;
                    }
                }
                a.employeeId = bestId;
                a.employeeName = ep ? ep->name : QStringLiteral("Employé");
                a.warning = bestWarn;
                const int newLoad = r.weeklyLoadHours.value(bestId, 0) + t.durationHours;
                r.weeklyLoadHours[bestId] = newLoad;
            }

            // Surcharge hebdo simple
            if (a.employeeId > 0) {
                const int load = r.weeklyLoadHours.value(a.employeeId, 0);
                const Employee *ep = nullptr;
                for (const Employee &e : employees) {
                    if (e.id == a.employeeId) {
                        ep = &e;
                        break;
                    }
                }
                if (ep && load > ep->weeklyCapacityHours) {
                    a.overload = true;
                    if (!a.warning.isEmpty())
                        a.warning += QStringLiteral(" | ");
                    a.warning += QStringLiteral("Surcharge");
                }
            }

            r.assignments.push_back(a);
        }
    }

    // Congés visibles: on ajoute des "blocs" gris pour chaque employé en congé approuvé
    if (leaveManager) {
        for (const Employee &e : employees) {
            for (const QDate &day : r.days) {
                if (!leaveManager->isOnApprovedLeave(e.id, day))
                    continue;
                Assignment a;
                a.employeeId = e.id;
                a.employeeName = e.name;
                a.day = day;
                a.task.name = QStringLiteral("CONGÉ");
                a.task.durationHours = 0;
                a.onLeave = true;
                a.matchScore = 1.0;
                r.assignments.push_back(a);
            }
        }
    }

    return r;
}

SchedulingEngine::ScheduleResult SchedulingEngine::optimizeSchedule(const ScheduleResult &current,
                                                                    const LeaveManager *leaveManager) const
{
    // Optimisation légère: on régénère en réutilisant les mêmes employés mais en "rééquilibrant"
    // via la fonction generateWeekSchedule() (le scoring inclut déjà la charge).
    // On conserve le même début de semaine.
    const QDate ws = current.days.isEmpty() ? QDate::currentDate() : current.days.first();
    return generateWeekSchedule(current.employees, defaultTasksForOlivePress(), ws, leaveManager);
}

