#include "planningwidget.h"

#include <QComboBox>
#include <QDateEdit>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlDatabase>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {

static QString statusToString(LeaveManager::Status s)
{
    switch (s) {
    case LeaveManager::Pending:
        return QStringLiteral("En attente");
    case LeaveManager::Approved:
        return QStringLiteral("Approuvé");
    case LeaveManager::Rejected:
        return QStringLiteral("Refusé");
    }
    return QStringLiteral("En attente");
}

static QString statusColor(LeaveManager::Status s)
{
    switch (s) {
    case LeaveManager::Approved:
        return QStringLiteral("#2e7d32");
    case LeaveManager::Rejected:
        return QStringLiteral("#dc2626");
    case LeaveManager::Pending:
    default:
        return QStringLiteral("#f59e0b");
    }
}

} // namespace

PlanningWidget::PlanningWidget(QWidget *parent)
    : QWidget(parent)
{
    m_leaveMgr = new LeaveManager(this);
    connect(m_leaveMgr, &LeaveManager::leaveDataChanged, this, [this]() {
        renderLeaveHistory();
        // Recalcul automatique après validation/refus: le planning doit refléter les indisponibilités.
        if (!m_lastSchedule.employees.isEmpty() && !m_lastSchedule.days.isEmpty())
            onGenerateClicked();
    });

    buildUi();
    loadEmployees();
    renderLeaveHistory();
}

void PlanningWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    auto *card = new QFrame(this);
    card->setStyleSheet(QStringLiteral(
        "QFrame { background: #ffffff; border: 1px solid #dbe4ea; border-radius: 12px; }"
        "QLabel { color: #0f172a; }"
        "QPushButton { border-radius: 10px; padding: 10px 14px; font-weight: 700; }"
        "QPushButton#primaryBtn { background: #556B2F; color: white; }"
        "QPushButton#primaryBtn:hover { background: #4e612b; }"
        "QPushButton#secondaryBtn { background: #f8fafc; border: 1px solid #dbe4ea; color: #0f172a; }"
        "QPushButton#secondaryBtn:hover { background: #eef2f7; }"
        "QTableWidget { background: #ffffff; border: 1px solid #dbe4ea; border-radius: 10px; gridline-color:#e5e7eb; }"
        "QHeaderView::section { background: #f8fafc; border: 1px solid #e5e7eb; padding: 8px; font-weight: 700; }"
        "QTextEdit { background: #ffffff; border: 1px solid #dbe4ea; border-radius: 10px; padding: 8px; }"
        "QComboBox, QDateEdit { background: #ffffff; border: 1px solid #dbe4ea; border-radius: 8px; padding: 6px 10px; min-height: 34px; }"));

    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(16, 14, 16, 16);
    lay->setSpacing(10);

    m_title = new QLabel(tr("<b>Planification Intelligente</b>"), card);
    QFont tf = m_title->font();
    tf.setPointSizeF(tf.pointSizeF() + 2.0);
    m_title->setFont(tf);
    lay->addWidget(m_title);

    m_hint = new QLabel(tr("Génération automatique de planning (tâches ↔ compétences) avec gestion des congés, détection de conflits et équilibrage de charge."), card);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("QLabel { color: #475569; font-size: 12px; }"));
    lay->addWidget(m_hint);

    // Actions
    auto *actions = new QHBoxLayout();
    actions->setSpacing(10);
    actions->addWidget(new QLabel(tr("Semaine du :"), card));
    m_weekStartEdit = new QDateEdit(QDate::currentDate(), card);
    m_weekStartEdit->setCalendarPopup(true);
    actions->addWidget(m_weekStartEdit);
    actions->addStretch();

    m_generateBtn = new QPushButton(tr("Générer Planning"), card);
    m_generateBtn->setObjectName(QStringLiteral("primaryBtn"));
    m_generateBtn->setCursor(Qt::PointingHandCursor);
    connect(m_generateBtn, &QPushButton::clicked, this, &PlanningWidget::onGenerateClicked);

    m_optimizeBtn = new QPushButton(tr("Optimiser Planning"), card);
    m_optimizeBtn->setObjectName(QStringLiteral("secondaryBtn"));
    m_optimizeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_optimizeBtn, &QPushButton::clicked, this, &PlanningWidget::onOptimizeClicked);

    actions->addWidget(m_optimizeBtn);
    actions->addWidget(m_generateBtn);
    lay->addLayout(actions);

    // Planning table
    lay->addWidget(new QLabel(tr("<b>Planning (vue tableau)</b>"), card));
    m_scheduleTable = new QTableWidget(card);
    m_scheduleTable->setMinimumHeight(220);
    m_scheduleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_scheduleTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_scheduleTable->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_scheduleTable->horizontalHeader()->setStretchLastSection(true);
    m_scheduleTable->verticalHeader()->setDefaultSectionSize(56);
    lay->addWidget(m_scheduleTable);

    // Warnings
    lay->addWidget(new QLabel(tr("<b>Avertissements / Conflits</b>"), card));
    m_warningsView = new QTextEdit(card);
    m_warningsView->setReadOnly(true);
    m_warningsView->setMinimumHeight(90);
    lay->addWidget(m_warningsView);

    // Congés section
    lay->addWidget(new QLabel(tr("<b>Gestion des Congés</b>"), card));
    auto *leaveGrid = new QGridLayout();
    leaveGrid->setHorizontalSpacing(10);
    leaveGrid->setVerticalSpacing(8);

    m_leaveEmployeeCombo = new QComboBox(card);
    m_leaveStartEdit = new QDateEdit(QDate::currentDate(), card);
    m_leaveEndEdit = new QDateEdit(QDate::currentDate(), card);
    m_leaveStartEdit->setCalendarPopup(true);
    m_leaveEndEdit->setCalendarPopup(true);
    m_addLeaveBtn = new QPushButton(tr("Demander Congé"), card);
    m_addLeaveBtn->setObjectName(QStringLiteral("secondaryBtn"));
    m_addLeaveBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addLeaveBtn, &QPushButton::clicked, this, &PlanningWidget::onAddLeaveClicked);

    leaveGrid->addWidget(new QLabel(tr("Employé :"), card), 0, 0);
    leaveGrid->addWidget(m_leaveEmployeeCombo, 0, 1);
    leaveGrid->addWidget(new QLabel(tr("Du :"), card), 0, 2);
    leaveGrid->addWidget(m_leaveStartEdit, 0, 3);
    leaveGrid->addWidget(new QLabel(tr("Au :"), card), 0, 4);
    leaveGrid->addWidget(m_leaveEndEdit, 0, 5);
    leaveGrid->addWidget(m_addLeaveBtn, 0, 6);
    lay->addLayout(leaveGrid);

    auto *approveRow = new QHBoxLayout();
    approveRow->addStretch();
    m_approveBtn = new QPushButton(tr("Approuver"), card);
    m_approveBtn->setObjectName(QStringLiteral("primaryBtn"));
    m_approveBtn->setCursor(Qt::PointingHandCursor);
    connect(m_approveBtn, &QPushButton::clicked, this, &PlanningWidget::onApproveLeaveClicked);
    m_rejectBtn = new QPushButton(tr("Refuser"), card);
    m_rejectBtn->setObjectName(QStringLiteral("secondaryBtn"));
    m_rejectBtn->setCursor(Qt::PointingHandCursor);
    connect(m_rejectBtn, &QPushButton::clicked, this, &PlanningWidget::onRejectLeaveClicked);
    approveRow->addWidget(m_rejectBtn);
    approveRow->addWidget(m_approveBtn);
    lay->addLayout(approveRow);

    // Panneau recommandation IA (pour aider le manager à décider)
    auto *aiTitle = new QLabel(tr("<b>Recommandation IA (congé)</b>"), card);
    aiTitle->setStyleSheet(QStringLiteral("QLabel { color: #0f172a; }"));
    lay->addWidget(aiTitle);

    m_leaveAiLabel = new QLabel(tr("Sélectionnez une demande pour voir la recommandation."), card);
    m_leaveAiLabel->setWordWrap(true);
    m_leaveAiLabel->setStyleSheet(QStringLiteral("QLabel { color: #475569; }"));
    lay->addWidget(m_leaveAiLabel);

    m_leaveAiDetail = new QTextEdit(card);
    m_leaveAiDetail->setReadOnly(true);
    m_leaveAiDetail->setMinimumHeight(70);
    lay->addWidget(m_leaveAiDetail);

    m_leaveHistoryTable = new QTableWidget(card);
    m_leaveHistoryTable->setColumnCount(6);
    m_leaveHistoryTable->setHorizontalHeaderLabels(
        {tr("ID"), tr("Employé"), tr("Du"), tr("Au"), tr("Statut"), tr("Raison")});
    m_leaveHistoryTable->setColumnHidden(0, true);
    m_leaveHistoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_leaveHistoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_leaveHistoryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_leaveHistoryTable->verticalHeader()->setDefaultSectionSize(46);
    m_leaveHistoryTable->horizontalHeader()->setStretchLastSection(true);
    connect(m_leaveHistoryTable, &QTableWidget::itemSelectionChanged, this, &PlanningWidget::onLeaveSelectionChanged);
    lay->addWidget(m_leaveHistoryTable);

    root->addWidget(card);
}

void PlanningWidget::loadEmployees()
{
    m_leaveEmployeeCombo->clear();

    bool ok = false;
    QVector<SchedulingEngine::Employee> employees = m_engine.inferEmployeesFromDb(&ok);

    if (!ok || employees.isEmpty()) {
        // fallback si pas de BD: mini équipe simulée
        employees = {
            {1, QStringLiteral("Employé A"), QStringLiteral("Production"), QStringLiteral("Opérateur"), {QStringLiteral("Conduite presse"), QStringLiteral("Sécurité machine")}, 40},
            {2, QStringLiteral("Employé B"), QStringLiteral("Qualité"), QStringLiteral("Contrôleur"), {QStringLiteral("HACCP"), QStringLiteral("Contrôle qualité"), QStringLiteral("Traçabilité")}, 40},
            {3, QStringLiteral("Employé C"), QStringLiteral("Maintenance"), QStringLiteral("Technicien"), {QStringLiteral("Maintenance 1er niveau"), QStringLiteral("Diagnostic")}, 40},
        };
    }

    // Remplir combo + stocker la liste dans le dernier schedule (pour la génération)
    m_lastSchedule.employees = employees;
    for (const auto &e : employees) {
        m_leaveEmployeeCombo->addItem(QStringLiteral("%1 — %2").arg(e.name, e.poste), e.id);
    }
}

int PlanningWidget::selectedEmployeeId() const
{
    return m_leaveEmployeeCombo ? m_leaveEmployeeCombo->currentData().toInt() : -1;
}

QString PlanningWidget::selectedEmployeeName() const
{
    if (!m_leaveEmployeeCombo)
        return QString();
    const QString label = m_leaveEmployeeCombo->currentText();
    const int sep = label.indexOf(QStringLiteral(" — "));
    return sep > 0 ? label.left(sep) : label;
}

int PlanningWidget::selectedLeaveRequestId() const
{
    if (!m_leaveHistoryTable)
        return -1;
    const int row = m_leaveHistoryTable->currentRow();
    if (row < 0)
        return -1;
    return m_leaveHistoryTable->item(row, 0)->text().toInt();
}

void PlanningWidget::onGenerateClicked()
{
    if (m_lastSchedule.employees.isEmpty())
        loadEmployees();

    const QVector<SchedulingEngine::Task> tasks = m_engine.defaultTasksForOlivePress();
    const QDate ws = m_weekStartEdit ? m_weekStartEdit->date() : QDate::currentDate();

    m_lastSchedule = m_engine.generateWeekSchedule(m_lastSchedule.employees, tasks, ws, m_leaveMgr);
    renderSchedule(m_lastSchedule);
    renderWarnings(m_lastSchedule.globalWarnings);
}

void PlanningWidget::onOptimizeClicked()
{
    if (m_lastSchedule.employees.isEmpty() || m_lastSchedule.days.isEmpty()) {
        onGenerateClicked();
        return;
    }
    m_lastSchedule = m_engine.optimizeSchedule(m_lastSchedule, m_leaveMgr);
    renderSchedule(m_lastSchedule);
    renderWarnings(m_lastSchedule.globalWarnings);
}

void PlanningWidget::renderSchedule(const SchedulingEngine::ScheduleResult &result)
{
    if (!m_scheduleTable)
        return;

    const int rows = result.employees.size();
    const int cols = result.days.size();
    m_scheduleTable->clear();
    m_scheduleTable->setRowCount(rows);
    m_scheduleTable->setColumnCount(cols + 1);

    QStringList headers;
    headers << tr("Employé");
    for (const QDate &d : result.days)
        headers << d.toString(QStringLiteral("ddd dd/MM"));
    m_scheduleTable->setHorizontalHeaderLabels(headers);

    for (int r = 0; r < rows; ++r) {
        const auto &e = result.employees.at(r);
        auto *it = new QTableWidgetItem(QStringLiteral("%1\n%2").arg(e.name, e.poste));
        it->setData(Qt::UserRole, e.id);
        m_scheduleTable->setItem(r, 0, it);
    }

    // Init cells
    for (int r = 0; r < rows; ++r) {
        const int eid = result.employees.at(r).id;
        for (int c = 0; c < cols; ++c) {
            const QDate day = result.days.at(c);
            auto *cell = new QTableWidgetItem();
            cell->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);

            // Congé -> gris
            if (m_leaveMgr && m_leaveMgr->isOnApprovedLeave(eid, day)) {
                cell->setText(tr("CONGÉ"));
                cell->setBackground(QColor("#e5e7eb"));
                cell->setForeground(QColor("#334155"));
                m_scheduleTable->setItem(r, c + 1, cell);
                continue;
            }

            cell->setText(tr("—"));
            cell->setBackground(QColor("#ffffff"));
            cell->setForeground(QColor("#0f172a"));
            m_scheduleTable->setItem(r, c + 1, cell);
        }
    }

    // Place assignments (simple: on concatène si plusieurs tâches)
    for (const auto &a : result.assignments) {
        if (a.employeeId <= 0 || a.onLeave)
            continue;

        int row = -1;
        for (int r = 0; r < rows; ++r) {
            if (result.employees.at(r).id == a.employeeId) {
                row = r;
                break;
            }
        }
        int col = -1;
        for (int c = 0; c < cols; ++c) {
            if (result.days.at(c) == a.day) {
                col = c;
                break;
            }
        }
        if (row < 0 || col < 0)
            continue;

        QTableWidgetItem *cell = m_scheduleTable->item(row, col + 1);
        QString txt = cell ? cell->text() : QString();
        if (txt == tr("—") || txt == tr("CONGÉ"))
            txt.clear();
        if (!txt.isEmpty())
            txt += QStringLiteral("\n");
        txt += QStringLiteral("• %1 (%2h)").arg(a.task.name).arg(a.task.durationHours);
        if (cell)
            cell->setText(txt);

        // Couleurs:
        // - vert si match bon
        // - rouge si surcharge ou match faible + warning
        const bool weak = a.matchScore < 0.5;
        if (a.overload || weak) {
            if (cell)
                cell->setBackground(QColor("#fee2e2")); // rouge clair
        } else {
            if (cell)
                cell->setBackground(QColor("#dcfce7")); // vert clair
        }
    }

    m_scheduleTable->resizeColumnsToContents();
    m_scheduleTable->horizontalHeader()->setStretchLastSection(true);
}

void PlanningWidget::renderWarnings(const QStringList &warnings)
{
    if (!m_warningsView)
        return;
    if (warnings.isEmpty()) {
        m_warningsView->setHtml(tr("<span style='color:#2e7d32'><b>Aucun conflit majeur détecté.</b></span>"));
        return;
    }
    QStringList lines;
    lines << tr("<b style='color:#dc2626'>Attention:</b>");
    for (const QString &w : warnings)
        lines << QStringLiteral("• %1").arg(w.toHtmlEscaped());
    m_warningsView->setHtml(lines.join(QStringLiteral("<br/>")));
}

void PlanningWidget::onAddLeaveClicked()
{
    const int eid = selectedEmployeeId();
    const QString ename = selectedEmployeeName();
    const QDate d1 = m_leaveStartEdit ? m_leaveStartEdit->date() : QDate();
    const QDate d2 = m_leaveEndEdit ? m_leaveEndEdit->date() : QDate();

    QString err;
    const int id = m_leaveMgr->addRequest(eid, ename, d1, d2, QString(), &err);
    if (id < 0) {
        QMessageBox::warning(this, tr("Congé"), err.isEmpty() ? tr("Impossible d'ajouter la demande.") : err);
        return;
    }
    renderLeaveHistory();
}

void PlanningWidget::onApproveLeaveClicked()
{
    const int rid = selectedLeaveRequestId();
    if (rid < 0) {
        QMessageBox::information(this, tr("Congé"), tr("Sélectionnez une demande."));
        return;
    }
    m_leaveMgr->setStatus(rid, LeaveManager::Approved);
}

void PlanningWidget::onRejectLeaveClicked()
{
    const int rid = selectedLeaveRequestId();
    if (rid < 0) {
        QMessageBox::information(this, tr("Congé"), tr("Sélectionnez une demande."));
        return;
    }
    // "رفض" = refus
    m_leaveMgr->setStatus(rid, LeaveManager::Rejected);
}

void PlanningWidget::onLeaveSelectionChanged()
{
    if (!m_leaveMgr)
        return;
    const int rid = selectedLeaveRequestId();
    if (rid < 0) {
        updateLeaveAiPanel(nullptr);
        return;
    }
    const auto &reqs = m_leaveMgr->requests();
    for (const auto &r : reqs) {
        if (r.id == rid) {
            updateLeaveAiPanel(&r);
            return;
        }
    }
    updateLeaveAiPanel(nullptr);
}

void PlanningWidget::renderLeaveHistory()
{
    if (!m_leaveHistoryTable || !m_leaveMgr)
        return;

    const auto &reqs = m_leaveMgr->requests();
    m_leaveHistoryTable->setRowCount(0);
    int row = 0;
    for (const auto &r : reqs) {
        m_leaveHistoryTable->insertRow(row);
        m_leaveHistoryTable->setItem(row, 0, new QTableWidgetItem(QString::number(r.id)));
        m_leaveHistoryTable->setItem(row, 1, new QTableWidgetItem(r.employeeName));
        m_leaveHistoryTable->setItem(row, 2, new QTableWidgetItem(r.startDate.toString(QStringLiteral("dd/MM/yyyy"))));
        m_leaveHistoryTable->setItem(row, 3, new QTableWidgetItem(r.endDate.toString(QStringLiteral("dd/MM/yyyy"))));

        auto *st = new QTableWidgetItem(statusToString(r.status));
        st->setForeground(QColor(statusColor(r.status)));
        m_leaveHistoryTable->setItem(row, 4, st);
        m_leaveHistoryTable->setItem(row, 5, new QTableWidgetItem(r.reason));
        row++;
    }
    m_leaveHistoryTable->resizeColumnsToContents();
    m_leaveHistoryTable->horizontalHeader()->setStretchLastSection(true);
}

PlanningWidget::LeaveAiRecommendation PlanningWidget::recommendLeave(const LeaveManager::LeaveRequest &req) const
{
    // Heuristique "IA" (pas de ML):
    // - pénalise si le congé tombe sur la semaine planifiée et casse beaucoup de tâches
    // - pénalise si personne d'autre n'a des compétences proches
    // - favorise si l'employé est déjà très chargé (stress) et que le remplacement est possible

    LeaveAiRecommendation out;
    out.decision = QStringLiteral("Peut-être");
    out.confidence = 0.55;

    // Trouver l'employé + ses compétences
    SchedulingEngine::Employee emp;
    bool found = false;
    for (const auto &e : m_lastSchedule.employees) {
        if (e.id == req.employeeId) {
            emp = e;
            found = true;
            break;
        }
    }
    if (!found) {
        out.decision = QStringLiteral("Peut-être");
        out.confidence = 0.35;
        out.reason = tr("Employé non présent dans la liste actuelle. Générer le planning d'abord pour une recommandation plus fiable.");
        return out;
    }

    // Charge hebdo actuelle (si planning déjà généré)
    const int loadHours = m_lastSchedule.weeklyLoadHours.value(emp.id, 0);
    const double loadRatio = emp.weeklyCapacityHours > 0 ? double(loadHours) / double(emp.weeklyCapacityHours) : 0.0;

    // Overlap congé avec la semaine affichée?
    int overlapDays = 0;
    for (const QDate &d : m_lastSchedule.days) {
        if (d.isValid() && d >= req.startDate && d <= req.endDate)
            overlapDays++;
    }

    // Tâches impactées (assignées à cet employé pendant ces jours)
    int impactedTasks = 0;
    int highPriorityImpacted = 0;
    for (const auto &a : m_lastSchedule.assignments) {
        if (a.employeeId != emp.id)
            continue;
        if (a.onLeave)
            continue;
        if (a.day < req.startDate || a.day > req.endDate)
            continue;
        impactedTasks++;
        if (a.task.priority >= 4)
            highPriorityImpacted++;
    }

    // Couverture compétences: est-ce qu'on peut remplacer par quelqu'un d'autre?
    // Score = max overlap (% de skills communs) sur les autres employés.
    double bestCoverage = 0.0;
    for (const auto &e : m_lastSchedule.employees) {
        if (e.id == emp.id)
            continue;
        if (emp.skills.isEmpty() || e.skills.isEmpty())
            continue;
        int common = 0;
        for (const QString &s : emp.skills) {
            if (e.skills.contains(s, Qt::CaseInsensitive))
                common++;
        }
        bestCoverage = qMax(bestCoverage, double(common) / double(emp.skills.size()));
    }

    // Score décision (0..1): plus haut => accepter
    double score = 0.55;

    // Si charge élevée, on est plus permissif (prévention stress)
    if (loadRatio > 1.05)
        score += 0.20;
    else if (loadRatio > 0.85)
        score += 0.10;

    // Si le congé casse beaucoup de tâches/priorités, on baisse
    score -= 0.08 * double(impactedTasks);
    score -= 0.10 * double(highPriorityImpacted);

    // Si le congé ne touche pas la semaine planifiée, ce n'est pas critique
    if (overlapDays == 0)
        score += 0.20;

    // Si couverture compétence bonne, on peut accepter plus facilement
    score += 0.25 * bestCoverage;

    // Bornes
    if (score < 0.0) score = 0.0;
    if (score > 1.0) score = 1.0;

    // Décision
    if (score >= 0.72)
        out.decision = QStringLiteral("Accepter");
    else if (score <= 0.38)
        out.decision = QStringLiteral("Refuser");
    else
        out.decision = QStringLiteral("Peut-être");

    // Confiance: plus on a de contexte (planning généré + overlap), plus c'est fiable.
    double conf = 0.45;
    conf += m_lastSchedule.days.isEmpty() ? 0.0 : 0.15;
    conf += (overlapDays > 0) ? 0.10 : 0.05;
    conf += qMin(0.20, bestCoverage * 0.20);
    conf -= qMin(0.20, double(highPriorityImpacted) * 0.08);
    if (conf < 0.10) conf = 0.10;
    if (conf > 0.95) conf = 0.95;
    out.confidence = conf;

    out.reason = tr("Charge: %1h/%2h (%.0f%%) | Jours impactés: %3 | Tâches impactées: %4 (prioritaires: %5) | Remplacement: %.0f%%")
                     .arg(QString::number(loadHours),
                          QString::number(emp.weeklyCapacityHours),
                          QString::number(loadRatio * 100.0, 'f', 0),
                          QString::number(overlapDays),
                          QString::number(impactedTasks),
                          QString::number(highPriorityImpacted),
                          QString::number(bestCoverage * 100.0, 'f', 0));
    return out;
}

void PlanningWidget::updateLeaveAiPanel(const LeaveManager::LeaveRequest *req)
{
    if (!m_leaveAiLabel || !m_leaveAiDetail) {
        return;
    }
    if (!req) {
        m_leaveAiLabel->setText(tr("Sélectionnez une demande pour voir la recommandation."));
        m_leaveAiLabel->setStyleSheet(QStringLiteral("QLabel { color: #475569; }"));
        m_leaveAiDetail->clear();
        return;
    }

    const LeaveAiRecommendation rec = recommendLeave(*req);
    const QString color = (rec.decision == QStringLiteral("Accepter")) ? QStringLiteral("#2e7d32")
        : (rec.decision == QStringLiteral("Refuser")) ? QStringLiteral("#dc2626")
        : QStringLiteral("#f59e0b");

    m_leaveAiLabel->setText(tr("Recommandation : <b style='color:%1'>%2</b>  (confiance: %3%)")
                                .arg(color,
                                     rec.decision,
                                     QString::number(rec.confidence * 100.0, 'f', 0)));
    m_leaveAiLabel->setStyleSheet(QStringLiteral("QLabel { color: #0f172a; }"));

    const QString detail = tr("Employé: %1\nPériode: %2 → %3\n\n%4")
                               .arg(req->employeeName,
                                    req->startDate.toString(QStringLiteral("dd/MM/yyyy")),
                                    req->endDate.toString(QStringLiteral("dd/MM/yyyy")),
                                    rec.reason);
    m_leaveAiDetail->setText(detail);
}

