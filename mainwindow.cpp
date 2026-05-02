#include "mainwindow.h"
#include <ui_mainwindow.h>
#include "arduinohandler.h"
#include "employeemanager.h"
#include "employeestatisticswidget.h"
#include "fingerprintdialog.h"
#include "fingerprintlogindialog.h"
#include "pindialog.h"
#include "employeeaiclient.h"
#include "recruitmentwidget.h"
#include "planningwidget.h"
#include <QSqlQuery>
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QDoubleValidator>
#include <QFile>
#include <QFileDialog>
#include <QColor>
#include <QChart>
#include <QChartView>
#include <QDateTimeAxis>
#include <QGridLayout>
#include <QHash>
#include <QLabel>
#include <QLineSeries>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPieSeries>
#include <QPieSlice>
#include <QPrinter>
#include <QPushButton>
#include <QSplineSeries>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSerialPortInfo>
#include <QSettings>
#include <QStringList>
#include <QSqlDatabase>
#include <QSqlError>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextDocument>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <QValueAxis>
#include <QVariantMap>
#include <QVBoxLayout>
#include <QScrollArea>
#include <utility>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#else
#include <QTextCodec>
#endif

namespace {

static QString sqlOrderExpression(const QString &sortKey)
{
    static const QHash<QString, QString> map{
        {QStringLiteral("id"), QStringLiteral("ID")},
        {QStringLiteral("nom"),
         QStringLiteral("CASE WHEN INSTR(TRIM(NOM), ' ') > 0 THEN SUBSTR(NOM, 1, INSTR(NOM, ' ') - 1) ELSE TRIM(NOM) END")},
        {QStringLiteral("prenom"),
         QStringLiteral(
             "CASE WHEN INSTR(TRIM(NOM), ' ') > 0 THEN TRIM(SUBSTR(NOM, INSTR(NOM, ' ') + 1)) ELSE '' END")},
        {QStringLiteral("poste"), QStringLiteral("POSTE")},
        {QStringLiteral("depart"), QStringLiteral("DEPART")},
        {QStringLiteral("salaire"), QStringLiteral("SALAIRE")},
        {QStringLiteral("date"), QStringLiteral("DATE_DEBUT")},
    };
    return map.value(sortKey, QStringLiteral("ID"));
}

static QString htmlEscape(const QString &s)
{
    QString t = s;
    return t.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
        .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
        .replace(QLatin1Char('>'), QStringLiteral("&gt;"))
        .replace(QLatin1Char('"'), QStringLiteral("&quot;"));
}

static QString safeAssistantText(const QString &value)
{
    const QString trimmed = value.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("-") : trimmed;
}

static QString formatTnd(double value)
{
    return QLocale::system().toString(value, 'f', 2) + QStringLiteral(" TND");
}

// Colonnes du tableau: id | Select | nom | email | poste | salaire | depart | telephone | date
enum EmployeeTableCol {
    ColId = 0,
    ColSelect = 1,
    ColNom = 2,
    ColEmail = 3,
    ColPoste = 4,
    ColSalaire = 5,
    ColDepart = 6,
    ColTelephone = 7,
    ColDate = 8
};

static QString tableCellText(QTableWidget *t, int row, int col)
{
    QTableWidgetItem *it = t ? t->item(row, col) : nullptr;
    return it ? it->text() : QString();
}

// Stockage en base : « nom famille » + espace + prénom.
static QString combineNomPrenom(const QString &nom, const QString &prenom)
{
    const QString n = nom.trimmed();
    const QString p = prenom.trimmed();
    if (p.isEmpty())
        return n;
    if (n.isEmpty())
        return p;
    return n + QLatin1Char(' ') + p;
}

static void splitNomComplet(const QString &full, QString *nom, QString *prenom)
{
    const QString f = full.trimmed();
    const int sp = f.indexOf(QLatin1Char(' '));
    if (sp < 0) {
        *nom = f;
        prenom->clear();
        return;
    }
    *nom = f.left(sp).trimmed();
    *prenom = f.mid(sp + 1).trimmed();
}

enum EquipmentTableCol {
    EqColId = 0,
    EqColRef,
    EqColModele,
    EqColMarque,
    EqColType,
    EqColEtat,
    EqColSerie,
    EqColCout,
    EqColDate
};

static QString equipmentSqlOrderExpr(const QString &sortKey)
{
    static const QHash<QString, QString> map{
        {QStringLiteral("id"), QStringLiteral("ID")},
        {QStringLiteral("reference"), QStringLiteral("REFERENCE")},
        {QStringLiteral("modele"), QStringLiteral("MODELE")},
        {QStringLiteral("marque"), QStringLiteral("MARQUE")},
        {QStringLiteral("type"), QStringLiteral("TYPE_EQUI")},
        {QStringLiteral("etat"), QStringLiteral("ETAT")},
        {QStringLiteral("serie"), QStringLiteral("NUMERO_SERIE")},
        {QStringLiteral("cout"), QStringLiteral("COUT")},
        {QStringLiteral("date"), QStringLiteral("DATE_AJOUT")},
    };
    return map.value(sortKey, QStringLiteral("ID"));
}

enum StockTableCol {
    StColId = 0,
    StColType,
    StColRegion,
    StColQty,
    StColDate
};

static QString stockSqlOrderExpr(const QString &sortKey)
{
    static const QHash<QString, QString> map{
        {QStringLiteral("id"), QStringLiteral("ID_STOCK")},
        {QStringLiteral("type"), QStringLiteral("TYPE_PRODUIT")},
        {QStringLiteral("region"), QStringLiteral("REGION")},
        {QStringLiteral("quantite"), QStringLiteral("QUANTITE")},
        {QStringLiteral("date"), QStringLiteral("DATE_STOCK")},
    };
    return map.value(sortKey, QStringLiteral("ID_STOCK"));
}

static QComboBox *findCombo(QWidget *root, const char *name)
{
    return root ? root->findChild<QComboBox *>(QLatin1String(name)) : nullptr;
}

static QLineEdit *findLineEdit(QWidget *root, const char *name)
{
    return root ? root->findChild<QLineEdit *>(QLatin1String(name)) : nullptr;
}

static QPushButton *findButton(QWidget *root, const char *name)
{
    return root ? root->findChild<QPushButton *>(QLatin1String(name)) : nullptr;
}

static QScrollArea *findScrollArea(QWidget *root, const char *name)
{
    return root ? root->findChild<QScrollArea *>(QLatin1String(name)) : nullptr;
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Keep layout spacing from .ui unchanged.

    m_employeeSearchDebounce = new QTimer(this);
    m_employeeSearchDebounce->setSingleShot(true);
    m_employeeSearchDebounce->setInterval(350);
    connect(m_employeeSearchDebounce, &QTimer::timeout, this, &MainWindow::refreshEmployeeTable);

    setupEmployeeIdRow();
    setupPrenomField();
    setupInputValidation();
    connectLiveValidation();

    ui->firstNameLabel->setText(tr("Nom"));

    ui->deleteBtn->setVisible(true);
    ui->deleteBtn->setEnabled(true);
    ui->stackedWidget->setCurrentWidget(ui->employerPage);
    ui->employerTabWidget->setCurrentIndex(0);
    ui->searchAndActionsWidget->setVisible(true);

    QPixmap pix(":/images/logo/zitounalogo.png");
    ui->sidebarLogo->setPixmap(pix);
    ui->sidebarLogo->setScaledContents(true);

    setupEmployeeSearchBar();
    setupEmployeeExportMenu();

    // Onglet Statistiques : un seul scroll pour éviter les scrolls imbriqués.
    auto *statsScroll = new QScrollArea(ui->statisticsTab);
    statsScroll->setWidgetResizable(true);
    statsScroll->setFrameShape(QFrame::NoFrame);
    statsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    statsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    statsScroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));

    auto *statsContainer = new QWidget(statsScroll);
    auto *statsContainerLayout = new QVBoxLayout(statsContainer);
    statsContainerLayout->setContentsMargins(16, 12, 16, 16);
    statsContainerLayout->setSpacing(16);

    m_employeeStatisticsWidget = new EmployeeStatisticsWidget(statsContainer);
    statsContainerLayout->addWidget(m_employeeStatisticsWidget);

    // Recrutement Intelligent (IA) : section dans le même scroll.
    auto *recruitmentWidget = new RecruitmentWidget(statsContainer);
    statsContainerLayout->addWidget(recruitmentWidget);

    // Planification Intelligente : planning + congés (dans le même scroll).
    auto *planningWidget = new PlanningWidget(statsContainer);
    statsContainerLayout->addWidget(planningWidget);
    statsContainerLayout->addStretch(1);

    statsScroll->setWidget(statsContainer);
    ui->statisticsTabLayout->addWidget(statsScroll);

    setupAssistantTab();

    ui->employeeTable_6->setColumnHidden(ColId, true);
    ui->employeeTable_6->setColumnHidden(ColSelect, true);
    ui->employeeTable_6->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->employeeTable_6->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->employeeTable_6->setWordWrap(false);
    ui->employeeTable_6->verticalHeader()->setDefaultSectionSize(48);
    ui->employeeTable_6->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_baseLineEditStyle = ui->firstNameEdit->styleSheet();

    loadEmployees();

    connect(ui->employerTabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        ui->searchAndActionsWidget->setVisible(index == 0);
        if (index == 0) {
            loadEmployees();
        } else if (index == 1) {
            refreshEmployeeStatistics();
        }
    });

    connect(ui->saveEmployeeBtn, &QPushButton::clicked, this, [this]() {
        QString err;
        if (!validateInputs(&err)) {
            QMessageBox::warning(this, tr("Données invalides"), err);
            return;
        }

        const QString nom = ui->firstNameEdit->text().trimmed();
        const QString prenom = m_prenomEdit ? m_prenomEdit->text().trimmed() : QString();
        const QString nomComplet = combineNomPrenom(nom, prenom);
        const QString email = ui->emailEdit->text().trimmed();
        const QString tel = ui->phoneEdit->text();
        const QString depart = ui->departmentComboBox->currentText();
        const QString poste = ui->posteComboBox->currentText();

        bool okSal = false;
        const double salaire = QLocale::system().toDouble(ui->salaryEdit->text().trimmed(), &okSal);
        (void)okSal; // garanti par validateInputs()

        const QDate date = ui->startDateEdit->date();
        const QString mot = Employee::hashPassword(ui->passwordEdit->text());

        Employee E(nomComplet, email, tel, depart, poste, salaire, date, mot);

        const bool explicitUpdateMode = (ui->saveEmployeeBtn
                                         && ui->saveEmployeeBtn->text().contains(tr("Mettre à jour"), Qt::CaseInsensitive));

        bool success = false;
        if (m_isModifyMode && m_modifyEmployeeId > 0 && explicitUpdateMode) {
            success = E.modifier(m_modifyEmployeeId);
        } else {
            QSettings settings(QStringLiteral("ZitounaTech"), QStringLiteral("HuilerieApp"));
            const QString pinPort = settings.value(QStringLiteral("arduino/pin_port"), QStringLiteral("COM8")).toString().trimmed();
            const QString fingerprintPort = settings.value(QStringLiteral("arduino/fingerprint_port"), QStringLiteral("COM9")).toString().trimmed();
            QString pinCode;
            int enrolledSlotId = -1;

            ArduinoHandler pinHandler(this);
            ArduinoHandler *pinHandlerPtr = nullptr;
            QString pinPortError;
            if (pinHandler.open(pinPort, 9600, &pinPortError)) {
                pinHandler.startPinEntry();
                pinHandlerPtr = &pinHandler;
            } else {
                QMessageBox::warning(this,
                                     tr("Arduino PIN"),
                                     tr("Arduino PIN indisponible sur %1.\nBasculer sur la saisie clavier PC.\n\nDétail: %2")
                                         .arg(pinPort, pinPortError));
            }
            PinDialog pinDialog(pinHandlerPtr, this);
            if (pinDialog.exec() != QDialog::Accepted) {
                if (pinHandlerPtr)
                    pinHandler.close();
                QMessageBox::warning(this, tr("Ajout employé"), tr("Ajout annulé: code PIN non validé."));
                return;
            }
            pinCode = pinDialog.pinCode().trimmed();
            if (pinHandlerPtr)
                pinHandler.close();
            if (pinCode.size() != 4) {
                QMessageBox::warning(this, tr("Ajout employé"), tr("Le code PIN doit contenir exactement 4 chiffres."));
                return;
            }

            ArduinoHandler fingerprintHandler(this);
            QString fingerprintPortError;
            if (!fingerprintHandler.open(fingerprintPort, 9600, &fingerprintPortError)) {
                QMessageBox::critical(this,
                                      tr("Arduino empreinte"),
                                      tr("Impossible d'ouvrir le port %1 pour le scenario empreinte.\n%2")
                                          .arg(fingerprintPort, fingerprintPortError));
                return;
            }
            FingerprintDialog fingerprintDialog(&fingerprintHandler, this);
            connect(&fingerprintDialog, &FingerprintDialog::enrollCompleted, this, [&](int slotId) {
                enrolledSlotId = slotId;
            });
            if (fingerprintDialog.exec() != QDialog::Accepted) {
                fingerprintHandler.close();
                QMessageBox::warning(this, tr("Ajout employé"), tr("Ajout annulé: empreinte non enregistrée."));
                return;
            }
            fingerprintHandler.close();
            if (enrolledSlotId < 1) {
                QMessageBox::critical(this, tr("Ajout employé"), tr("Empreinte invalide: aucun slot valide reçu."));
                return;
            }

            success = E.ajouter();

            bool biometricSaved = false;
            if (success && !pinCode.isEmpty()) {
                QSqlQuery updateBiometricQuery;
                updateBiometricQuery.prepare(
                    QStringLiteral("UPDATE EMPLOYEE "
                                   "SET CODE=:code, FINGER_SLOT_ID=:finger_slot_id "
                                   "WHERE LOWER(TRIM(EMAIL))=LOWER(TRIM(:email))"));
                updateBiometricQuery.bindValue(QStringLiteral(":code"), pinCode.left(4));
                updateBiometricQuery.bindValue(QStringLiteral(":finger_slot_id"), enrolledSlotId);
                updateBiometricQuery.bindValue(QStringLiteral(":email"), email);
                if (updateBiometricQuery.exec()) {
                    biometricSaved = (updateBiometricQuery.numRowsAffected() > 0);
                } else {
                    qWarning() << "Failed to update EMPLOYEE biometric fields:"
                               << updateBiometricQuery.lastError().text();
                }
            }
            if (success && (!biometricSaved || enrolledSlotId < 1)) {
                // Hard stop: never keep employee if Arduino scenarios are not fully persisted.
                QSqlQuery rollbackQuery;
                rollbackQuery.prepare(QStringLiteral("DELETE FROM EMPLOYEE WHERE LOWER(TRIM(EMAIL))=LOWER(TRIM(:email))"));
                rollbackQuery.bindValue(QStringLiteral(":email"), email);
                rollbackQuery.exec();
                success = false;
            }
            if (success) {
                // Sync biometric data in local SQLite repository used by fingerprint login flow.
                QSqlQuery employeeIdQuery;
                employeeIdQuery.prepare(QStringLiteral("SELECT ID FROM EMPLOYEE WHERE LOWER(TRIM(EMAIL))=LOWER(TRIM(:email))"));
                employeeIdQuery.bindValue(QStringLiteral(":email"), email);
                int createdEmployeeId = -1;
                if (employeeIdQuery.exec() && employeeIdQuery.next()) {
                    createdEmployeeId = employeeIdQuery.value(0).toInt();
                }

                EmployeeData sqliteEmployee;
                sqliteEmployee.id = createdEmployeeId;
                sqliteEmployee.nom = nom;
                sqliteEmployee.prenom = prenom;
                sqliteEmployee.poste = poste;
                sqliteEmployee.dateEmbauche = date;
                EmployeeManager manager;
                QString sqliteError;
                if (!manager.addEmployee(sqliteEmployee, pinCode, enrolledSlotId, &sqliteError)) {
                    QSqlQuery rollbackQuery;
                    rollbackQuery.prepare(QStringLiteral("DELETE FROM EMPLOYEE WHERE LOWER(TRIM(EMAIL))=LOWER(TRIM(:email))"));
                    rollbackQuery.bindValue(QStringLiteral(":email"), email);
                    rollbackQuery.exec();
                    success = false;
                    QMessageBox::critical(this,
                                          tr("SQLite biométrie"),
                                          tr("Ajout annulé: impossible de sauvegarder PIN/empreinte en SQLite.\n%1")
                                              .arg(sqliteError));
                }
            }
            if (success && biometricSaved) {
                qInfo() << "Employee Arduino scenarios completed. Finger slot:" << enrolledSlotId;
            }
        }

        if (success) {
            clearEmployeeForm();
            ui->employerTabWidget->setCurrentIndex(0);
            QTimer::singleShot(0, this, [this]() {
                loadEmployees();
                refreshEmployeeStatistics();
            });
            QMessageBox::information(this, tr("Succès"), tr("Opération réussie."));
        } else {
            QMessageBox::critical(
                this,
                tr("Erreur"),
                tr("L'opération a échoué (unicité e-mail / téléphone, contraintes Oracle, etc.)."));
        }
    });

    connect(ui->deleteBtn, &QPushButton::clicked, this, [this]() {
        const int row = ui->employeeTable_6->currentRow();
        if (row < 0) {
            QMessageBox::warning(this, tr("Attention"), tr("Sélectionnez un employé."));
            return;
        }
        const int id = tableCellText(ui->employeeTable_6, row, ColId).toInt();
        if (Etmp.supprimer(id)) {
            loadEmployees();
            refreshEmployeeStatistics();
            QMessageBox::information(this, tr("Succès"), tr("Employé supprimé."));
        } else {
            QMessageBox::critical(this, tr("Erreur"), tr("Échec de la suppression."));
        }
    });

    connect(ui->modifyBtn, &QPushButton::clicked, this, [this]() {
        const int row = ui->employeeTable_6->currentRow();
        if (row < 0) {
            QMessageBox::warning(this, tr("Attention"), tr("Sélectionnez un employé."));
            return;
        }
        applyTableRowToForm(row);
        ui->employerTabWidget->setCurrentIndex(0);
    });

    ui->modifyBtn->setEnabled(true);

    connect(ui->cancelBtn, &QPushButton::clicked, this, [this]() {
        clearEmployeeForm();
    });

    setupLoginFlow();
    setupAccessControlBridge();

    if (ui->profileInfoLayout) {
        QPushButton *logoutProfileBtn =
            ui->profilePage->findChild<QPushButton *>(QStringLiteral("logoutProfileButton"));
        if (!logoutProfileBtn) {
            logoutProfileBtn = new QPushButton(tr("SE DECONNECTER"), ui->profilePage);
            logoutProfileBtn->setObjectName(QStringLiteral("logoutProfileButton"));
            logoutProfileBtn->setMinimumHeight(44);
            logoutProfileBtn->setCursor(Qt::PointingHandCursor);
            logoutProfileBtn->setStyleSheet(QStringLiteral(
                "QPushButton#logoutProfileButton {"
                " background-color: #c62828;"
                " color: #ffffff;"
                " border: none;"
                " border-radius: 12px;"
                " padding: 10px 16px;"
                " font-weight: 600;"
                " font-family: 'Poppins', 'Segoe UI', Arial, sans-serif;"
                "}"
                "QPushButton#logoutProfileButton:hover {"
                " background-color: #b71c1c;"
                "}"
                "QPushButton#logoutProfileButton:pressed {"
                " background-color: #8e1616;"
                "}"));
            ui->profileInfoLayout->addWidget(logoutProfileBtn, 0, Qt::AlignRight);
        }
        connect(logoutProfileBtn, &QPushButton::clicked, this, [this]() {
            m_loggedInEmail.clear();
            if (m_loginErrorLabel) {
                m_loginErrorLabel->clear();
                m_loginErrorLabel->setVisible(false);
            }
            setAuthenticated(false);
        });
    }

    auto *sidebarGroup = new QButtonGroup(this);
    sidebarGroup->setExclusive(true);
    const QList<QPushButton *> sidebarButtons{
        ui->employerBtn,
        ui->profileBtn,
        ui->clientsBtn,
        ui->equipmentBtn,
        ui->financeBtn,
        ui->fournisseursBtn,
        ui->stockBtn,
        ui->AproposBtn,
    };
    for (auto *button : sidebarButtons) {
        button->setCheckable(true);
        sidebarGroup->addButton(button);
        connect(button, &QPushButton::clicked, this, [button]() {
            button->setChecked(true);
        });
    }

    setupEquipmentModule();
    setupStockModule();
    setupClientsFinanceModule();

    // Uniformiser l'entrée de chaque module sur l'onglet principal "Ajouter / Liste".
    if (ui->employerTabWidget && ui->addTab)
        ui->employerTabWidget->setCurrentWidget(ui->addTab);
    if (ui->clientTabWidget_2 && ui->addTab_2)
        ui->clientTabWidget_2->setCurrentWidget(ui->addTab_2);
    if (ui->ProductsTab && ui->addTab_3)
        ui->ProductsTab->setCurrentWidget(ui->addTab_3);
    if (ui->VentesTab && ui->addTab_4)
        ui->VentesTab->setCurrentWidget(ui->addTab_4);
    if (ui->VentesTab_2 && ui->addTab_5)
        ui->VentesTab_2->setCurrentWidget(ui->addTab_5);
    if (ui->stockTabWidget && ui->stockAddTab)
        ui->stockTabWidget->setCurrentWidget(ui->stockAddTab);

    if (ui->VentesTab && ui->addTab_4) {
        const int fournisseurAddTabIndex = ui->VentesTab->indexOf(ui->addTab_4);
        if (fournisseurAddTabIndex >= 0)
            ui->VentesTab->setTabText(fournisseurAddTabIndex, tr("Ajouter / Liste"));
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupLoginFlow()
{
    if (ui->mainStackedWidget && m_mainStackedDefaultGeometry.isNull()) {
        m_mainStackedDefaultGeometry = ui->mainStackedWidget->geometry();
    }
    if (ui->sidebarWidget && m_sidebarDefaultGeometry.isNull()) {
        m_sidebarDefaultGeometry = ui->sidebarWidget->geometry();
    }

    if (ui->passwordLineEdit) {
        ui->passwordLineEdit->setEchoMode(QLineEdit::Password);
    }

    if (!m_loginErrorLabel && ui->loginPageLayout) {
        m_loginErrorLabel = new QLabel(ui->loginPage);
        m_loginErrorLabel->setObjectName(QStringLiteral("loginErrorLabel"));
        m_loginErrorLabel->setStyleSheet(
            QStringLiteral("QLabel#loginErrorLabel { color: #c62828; font-weight: 600; }"));
        m_loginErrorLabel->setWordWrap(true);
        m_loginErrorLabel->setVisible(false);
        const int insertIndex = ui->loginPageLayout->indexOf(ui->loginSubmitButton);
        if (insertIndex >= 0) {
            ui->loginPageLayout->insertWidget(insertIndex, m_loginErrorLabel);
        } else {
            ui->loginPageLayout->addWidget(m_loginErrorLabel);
        }
    }

    // Never allow bypass from the login screen.
    if (ui->backToMainButton) {
        ui->backToMainButton->setVisible(false);
        ui->backToMainButton->setEnabled(false);
    }

    if (ui->loginSubmitButton) {
        connect(ui->loginSubmitButton, &QPushButton::clicked, this, [this]() {
            if (tryAuthenticateUser()) {
                setAuthenticated(true);
            }
        });
    }
    if (ui->loginPageLayout) {
        QPushButton *fingerprintLoginBtn = ui->loginPage->findChild<QPushButton *>(QStringLiteral("loginFingerprintButton"));
        if (!fingerprintLoginBtn) {
            fingerprintLoginBtn = new QPushButton(tr("Se connecter avec empreinte 👆"), ui->loginPage);
            fingerprintLoginBtn->setObjectName(QStringLiteral("loginFingerprintButton"));
            if (ui->loginSubmitButton) {
                fingerprintLoginBtn->setMinimumHeight(ui->loginSubmitButton->minimumHeight());
                fingerprintLoginBtn->setMaximumHeight(ui->loginSubmitButton->maximumHeight());
                fingerprintLoginBtn->setFont(ui->loginSubmitButton->font());
                fingerprintLoginBtn->setSizePolicy(ui->loginSubmitButton->sizePolicy());
                fingerprintLoginBtn->setStyleSheet(ui->loginSubmitButton->styleSheet());
                fingerprintLoginBtn->setPalette(ui->loginSubmitButton->palette());
            } else {
                fingerprintLoginBtn->setMinimumHeight(44);
            }
            ui->loginPageLayout->addWidget(fingerprintLoginBtn);
        }
        connect(fingerprintLoginBtn, &QPushButton::clicked, this, [this]() {
            QSettings settings(QStringLiteral("ZitounaTech"), QStringLiteral("HuilerieApp"));
            const QString fingerprintPort = settings.value(QStringLiteral("arduino/fingerprint_port"), QStringLiteral("COM9"))
                                               .toString()
                                               .trimmed();

            ArduinoHandler handler(this);
            QString portError;
            if (!handler.open(fingerprintPort, 9600, &portError)) {
                if (m_loginErrorLabel) {
                    m_loginErrorLabel->setText(tr("Capteur indisponible sur %1 (%2).").arg(fingerprintPort, portError));
                    m_loginErrorLabel->setVisible(true);
                }
                return;
            }

            bool loginMatched = false;
            int matchedSlot = -1;
            FingerprintLoginDialog dialog(&handler, this);
            connect(&dialog, &FingerprintLoginDialog::loginSuccess, this, [&](int slotId) {
                loginMatched = true;
                matchedSlot = slotId;
            });
            dialog.exec();
            handler.close();

            if (!loginMatched || matchedSlot < 1) {
                if (m_loginErrorLabel) {
                    m_loginErrorLabel->setText(tr("Empreinte non reconnue."));
                    m_loginErrorLabel->setVisible(true);
                }
                return;
            }

            EmployeeManager manager;
            EmployeeData employee;
            QString lookupError;
            if (!manager.getEmployeeByFingerSlot(matchedSlot, &employee, &lookupError)) {
                if (m_loginErrorLabel) {
                    m_loginErrorLabel->setText(tr("Aucun employé lié au slot empreinte %1.").arg(matchedSlot));
                    m_loginErrorLabel->setVisible(true);
                }
                return;
            }

            if (employee.id > 0) {
                QSqlQuery emailQuery;
                emailQuery.prepare(QStringLiteral("SELECT EMAIL FROM EMPLOYEE WHERE ID=:id"));
                emailQuery.bindValue(QStringLiteral(":id"), employee.id);
                if (emailQuery.exec() && emailQuery.next()) {
                    m_loggedInEmail = emailQuery.value(0).toString();
                } else {
                    m_loggedInEmail = QStringLiteral("slot:%1").arg(matchedSlot);
                }
            } else {
                m_loggedInEmail = QStringLiteral("slot:%1").arg(matchedSlot);
            }

            if (m_loginErrorLabel) {
                m_loginErrorLabel->clear();
                m_loginErrorLabel->setVisible(false);
            }
            setAuthenticated(true);
        });
    }
    if (ui->usernameLineEdit && ui->loginSubmitButton) {
        connect(ui->usernameLineEdit, &QLineEdit::returnPressed, ui->loginSubmitButton, &QPushButton::click);
    }
    if (ui->passwordLineEdit && ui->loginSubmitButton) {
        connect(ui->passwordLineEdit, &QLineEdit::returnPressed, ui->loginSubmitButton, &QPushButton::click);
    }

    setAuthenticated(false);
}

void MainWindow::setupAccessControlBridge()
{
    QSettings settings(QStringLiteral("ZitounaTech"), QStringLiteral("HuilerieApp"));
    const QString defaultPort = settings.value(QStringLiteral("arduino/pin_port"), QStringLiteral("COM8")).toString().trimmed();
    const QString accessPort = settings.value(QStringLiteral("arduino/access_port"), defaultPort).toString().trimmed();
    QStringList candidatePorts;
    // Prefer the keypad PIN port first (same board for CHECK_CODE flow).
    if (!defaultPort.isEmpty())
        candidatePorts << defaultPort;
    if (!accessPort.isEmpty() && !candidatePorts.contains(accessPort, Qt::CaseInsensitive))
        candidatePorts << accessPort;
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        const QString name = info.portName().trimmed();
        if (name.isEmpty() || candidatePorts.contains(name, Qt::CaseInsensitive))
            continue;
        const QString hint = (info.description() + QLatin1Char(' ') + info.manufacturer()).toLower();
        if (hint.contains(QStringLiteral("arduino"))
            || hint.contains(QStringLiteral("ch340"))
            || hint.contains(QStringLiteral("usb serial"))) {
            candidatePorts << name;
        }
    }

    if (candidatePorts.isEmpty()) {
        qWarning() << "No serial ports available for access bridge.";
        return;
    }

    m_accessBridgeHandler = new ArduinoHandler(this);
    QString openedPort;
    QString openError;
    for (const QString &portName : candidatePorts) {
        if (m_accessBridgeHandler->open(portName, 9600, &openError)) {
            openedPort = portName;
            break;
        }
    }
    if (openedPort.isEmpty()) {
        qWarning() << "Access bridge unavailable. Tried ports:" << candidatePorts << "Last error:" << openError;
        m_accessBridgeHandler->deleteLater();
        m_accessBridgeHandler = nullptr;
        return;
    }
    qInfo() << "Access bridge connected on" << openedPort << "(priority: keypad pin port)";
    settings.setValue(QStringLiteral("arduino/access_port"), openedPort);

    connect(m_accessBridgeHandler,
            &ArduinoHandler::accessCodeCheckRequested,
            this,
            [this](const QString &code) {
                if (!m_accessBridgeHandler || !m_accessBridgeHandler->isOpen())
                    return;
                const bool allowed = isAccessCodeAllowed(code);
                m_accessBridgeHandler->sendToArduino(allowed ? QStringLiteral("ACCESS_GRANTED")
                                                             : QStringLiteral("ACCESS_DENIED"));
            });

    connect(m_accessBridgeHandler,
            &ArduinoHandler::rawMessageReceived,
            this,
            [](const QString &message) {
                if (message.startsWith(QStringLiteral("PUMP"), Qt::CaseInsensitive)
                    || message.compare(QStringLiteral("Pump OFF"), Qt::CaseInsensitive) == 0) {
                    qInfo() << "Pump serial event:" << message;
                }
            });

    connect(m_accessBridgeHandler,
            &ArduinoHandler::pumpCycleComplete,
            this,
            [this](qulonglong cycleId) {
                static qint64 lastLegacyInsertMs = 0;
                if (cycleId > 0 && cycleId <= m_lastPumpCycleId) {
                    qInfo() << "Duplicate pump cycle ignored:" << cycleId;
                    return;
                }
                if (cycleId == 0) {
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    if (lastLegacyInsertMs > 0 && (nowMs - lastLegacyInsertMs) < 3500) {
                        qInfo() << "Duplicate legacy pump event ignored";
                        return;
                    }
                    lastLegacyInsertMs = nowMs;
                }

                const QSqlDatabase db = QSqlDatabase::database();
                if (!db.isValid() || !db.isOpen()) {
                    qWarning() << "Pump cycle ignored: database is not open";
                    return;
                }

                QSqlQuery query;
                query.prepare(QStringLiteral("INSERT INTO STOCK (TYPE_PRODUIT, REGION, QUANTITE, DATE_STOCK) "
                                             "VALUES ('olive oil', 'tounes', 1, SYSDATE)"));
                if (!query.exec()) {
                    qWarning() << "Pump cycle STOCK insert failed:" << query.lastError().text();
                    return;
                }

                if (cycleId > 0)
                    m_lastPumpCycleId = cycleId;

                qInfo() << "Pump cycle stored in STOCK. cycleId=" << cycleId;
                scheduleStockTableRefresh();
            });
}

bool MainWindow::isAccessCodeAllowed(const QString &code) const
{
    const QString trimmedCode = code.trimmed();
    if (trimmedCode.size() != 4)
        return false;

    static const QRegularExpression fourDigits(QStringLiteral("^\\d{4}$"));
    if (!fourDigits.match(trimmedCode).hasMatch())
        return false;

    QSqlQuery query;
    query.prepare(
        QStringLiteral("SELECT COUNT(*) FROM EMPLOYEE WHERE TRIM(CODE)=:code"));
    query.bindValue(QStringLiteral(":code"), trimmedCode);
    if (!query.exec()) {
        qWarning() << "Access check SQL failed:" << query.lastError().text();
        return false;
    }
    if (!query.next())
        return false;
    return query.value(0).toInt() > 0;
}

void MainWindow::setAuthenticated(bool authenticated)
{
    m_isAuthenticated = authenticated;
    if (ui->sidebarWidget) {
        ui->sidebarWidget->setEnabled(authenticated);
        ui->sidebarWidget->setVisible(authenticated);
        if (authenticated && !m_sidebarDefaultGeometry.isNull()) {
            ui->sidebarWidget->setGeometry(m_sidebarDefaultGeometry);
        }
    }
    if (ui->mainStackedWidget && ui->centralwidget) {
        if (authenticated) {
            if (!m_mainStackedDefaultGeometry.isNull()) {
                ui->mainStackedWidget->setGeometry(m_mainStackedDefaultGeometry);
            }
        } else {
            const QRect base = m_mainStackedDefaultGeometry.isNull() ? ui->mainStackedWidget->geometry()
                                                                      : m_mainStackedDefaultGeometry;
            const int fullWidth = qMax(base.width(), ui->centralwidget->width());
            ui->mainStackedWidget->setGeometry(0, base.y(), fullWidth, base.height());
        }
    }
    if (ui->normalContentPage) {
        ui->normalContentPage->setEnabled(authenticated);
    }
    if (ui->mainStackedWidget) {
        ui->mainStackedWidget->setCurrentWidget(authenticated ? ui->normalContentPage : ui->loginOverlayPage);
    }
}

bool MainWindow::tryAuthenticateUser()
{
    const QString identifier = ui->usernameLineEdit ? ui->usernameLineEdit->text().trimmed() : QString();
    const QString password = ui->passwordLineEdit ? ui->passwordLineEdit->text() : QString();

    auto showInvalid = [this]() {
        if (m_loginErrorLabel) {
            m_loginErrorLabel->setText(tr("Invalid email or password"));
            m_loginErrorLabel->setVisible(true);
        }
    };

    if (identifier.isEmpty() || password.isEmpty()) {
        showInvalid();
        return false;
    }

    QString authError;
    const bool ok = Employee::authenticate(identifier, password, &authError);
    if (!ok) {
        if (!authError.trimmed().isEmpty()) {
            if (m_loginErrorLabel) {
                if (authError == QStringLiteral("No employee found for this email/username.")
                    || authError == QStringLiteral("Employee found but password mismatch.")) {
                    m_loginErrorLabel->setText(tr("Invalid email or password"));
                } else {
                    m_loginErrorLabel->setText(tr("Login check failed (DB): %1").arg(authError));
                }
                m_loginErrorLabel->setVisible(true);
            }
            return false;
        }
        showInvalid();
        return false;
    }

    m_loggedInEmail = identifier;
    if (m_loginErrorLabel) {
        m_loginErrorLabel->clear();
        m_loginErrorLabel->setVisible(false);
    }
    if (ui->passwordLineEdit) {
        ui->passwordLineEdit->clear();
    }
    return true;
}

void MainWindow::setupEmployeeIdRow()
{
    QGridLayout *grid = qobject_cast<QGridLayout *>(ui->formWidget->layout());
    if (!grid || m_employeeIdLabel || m_employeeIdEdit)
        return;

    m_employeeIdLabel = new QLabel(tr("ID"), ui->formWidget);
    m_employeeIdLabel->setMinimumSize(100, 20);
    m_employeeIdLabel->setStyleSheet(ui->firstNameLabel->styleSheet());

    m_employeeIdEdit = new QLineEdit(ui->formWidget);
    m_employeeIdEdit->setMinimumSize(200, 42);
    m_employeeIdEdit->setStyleSheet(ui->firstNameEdit->styleSheet());
    m_employeeIdEdit->setPlaceholderText(tr("Attribué par la base (séquence)"));
    m_employeeIdEdit->setReadOnly(true);

    grid->addWidget(m_employeeIdLabel, 3, 0);
    grid->addWidget(m_employeeIdEdit, 3, 1);
    m_employeeIdLabel->hide();
    m_employeeIdEdit->hide();
}

void MainWindow::setupPrenomField()
{
    QGridLayout *grid = qobject_cast<QGridLayout *>(ui->formWidget->layout());
    if (!grid || m_prenomLabel || m_prenomEdit)
        return;

    m_prenomLabel = new QLabel(tr("Prénom"), ui->formWidget);
    m_prenomLabel->setMinimumSize(100, 20);
    m_prenomLabel->setStyleSheet(ui->firstNameLabel->styleSheet());

    m_prenomEdit = new QLineEdit(ui->formWidget);
    m_prenomEdit->setMinimumSize(200, 42);
    m_prenomEdit->setStyleSheet(ui->firstNameEdit->styleSheet());
    m_prenomEdit->setPlaceholderText(tr("Lettres uniquement"));

    grid->addWidget(m_prenomLabel, 5, 0);
    grid->addWidget(m_prenomEdit, 5, 1, 1, 3);
}

void MainWindow::setupInputValidation()
{
    m_salaryValidator = new QDoubleValidator(0.0, 1e12, 2, this);
    m_salaryValidator->setNotation(QDoubleValidator::StandardNotation);
    m_salaryValidator->setLocale(QLocale::system());
    ui->salaryEdit->setValidator(m_salaryValidator);

    const QRegularExpression reNom(QStringLiteral(R"(^[\p{L}\-'\s]{3,}$)"));
    m_nomValidator = new QRegularExpressionValidator(reNom, this);
    ui->firstNameEdit->setValidator(m_nomValidator);

    const QRegularExpression rePrenom(QStringLiteral(R"(^[\p{L}\-'\s]*$)"));
    m_prenomValidator = new QRegularExpressionValidator(rePrenom, this);
    m_prenomEdit->setValidator(m_prenomValidator);

    const QRegularExpression reEmail(
        QStringLiteral(R"(^[a-zA-Z0-9._%+\-]+@[a-zA-Z0-9.\-]+\.[a-zA-Z]{2,}$)"));
    m_emailValidator = new QRegularExpressionValidator(reEmail, this);
    ui->emailEdit->setValidator(m_emailValidator);

    const QRegularExpression reTel(QStringLiteral(R"(^[0-9]{8,15}$)"));
    m_phoneValidator = new QRegularExpressionValidator(reTel, this);
    ui->phoneEdit->setValidator(m_phoneValidator);
}

void MainWindow::connectLiveValidation()
{
    const auto hook = [this](QLineEdit *le) {
        if (!le)
            return;
        connect(le, &QLineEdit::textChanged, this, [this, le](const QString &) {
            setFieldError(le, false);
        });
    };
    hook(ui->firstNameEdit);
    hook(m_prenomEdit);
    hook(ui->emailEdit);
    hook(ui->phoneEdit);
    hook(ui->salaryEdit);
}

void MainWindow::setFieldError(QLineEdit *edit, bool error)
{
    if (!edit)
        return;
    if (error) {
        edit->setStyleSheet(m_baseLineEditStyle
                            + QStringLiteral("\nQLineEdit { border: 2px solid #c0392b; border-radius: 8px; }"));
    } else {
        edit->setStyleSheet(m_baseLineEditStyle);
    }
}

void MainWindow::clearAllFieldErrors()
{
    setFieldError(ui->firstNameEdit, false);
    setFieldError(m_prenomEdit, false);
    setFieldError(ui->emailEdit, false);
    setFieldError(ui->phoneEdit, false);
    setFieldError(ui->salaryEdit, false);
}

bool MainWindow::validateInputs(QString *firstErrorMessage)
{
    clearAllFieldErrors();

    QString firstMsg;
    auto fail = [&](QLineEdit *field, const QString &msg) {
        if (field)
            setFieldError(field, true);
        if (firstMsg.isEmpty())
            firstMsg = msg;
    };

    const QString nom = ui->firstNameEdit->text().trimmed();
    const QString prenom = m_prenomEdit ? m_prenomEdit->text().trimmed() : QString();
    const QString email = ui->emailEdit->text().trimmed();
    const QString salText = ui->salaryEdit->text().trimmed();

    const QRegularExpression reNom(QStringLiteral(R"(^[\p{L}\-'\s]{3,}$)"));
    const QRegularExpression rePrenom(QStringLiteral(R"(^[\p{L}\-'\s]{1,}$)"));
    const QRegularExpression reEmail(
        QStringLiteral(R"(^[a-zA-Z0-9._%+\-]+@[a-zA-Z0-9.\-]+\.[a-zA-Z]{2,}$)"));
    const QRegularExpression reTel(QStringLiteral(R"(^[0-9]{8,15}$)"));

    if (!reNom.match(nom).hasMatch() || nom.size() < 3) {
        fail(ui->firstNameEdit,
             tr("Le nom doit contenir au moins 3 caractères (lettres, espaces, tirets ou apostrophes)."));
        if (firstErrorMessage)
            *firstErrorMessage = firstMsg;
        return false;
    }

    if (!prenom.isEmpty() && !rePrenom.match(prenom).hasMatch()) {
        fail(m_prenomEdit, tr("Le prénom ne doit contenir que des lettres (et espaces / tirets / apostrophes)."));
        if (firstErrorMessage)
            *firstErrorMessage = firstMsg;
        return false;
    }

    bool okSal = false;
    const double salaire = QLocale::system().toDouble(salText, &okSal);
    if (!okSal || salaire <= 0.0) {
        fail(ui->salaryEdit, tr("Le salaire doit être un nombre strictement positif."));
        if (firstErrorMessage)
            *firstErrorMessage = firstMsg;
        return false;
    }

    if (!reEmail.match(email).hasMatch()) {
        fail(ui->emailEdit, tr("L'adresse e-mail n'est pas valide."));
        if (firstErrorMessage)
            *firstErrorMessage = firstMsg;
        return false;
    }

    const QString tel = ui->phoneEdit->text().trimmed();
    if (!reTel.match(tel).hasMatch()) {
        fail(ui->phoneEdit, tr("Le téléphone doit contenir 8 à 15 chiffres (sans espaces), comme en base Oracle."));
        if (firstErrorMessage)
            *firstErrorMessage = firstMsg;
        return false;
    }

    if (firstErrorMessage)
        firstErrorMessage->clear();
    return true;
}

void MainWindow::setModifyMode(bool active, int employeeId)
{
    m_isModifyMode = active;
    m_modifyEmployeeId = active ? employeeId : -1;

    if (m_employeeIdEdit && m_employeeIdLabel) {
        m_employeeIdLabel->setVisible(active);
        m_employeeIdEdit->setVisible(active);
        m_employeeIdEdit->setReadOnly(true);
        if (active && employeeId > 0)
            m_employeeIdEdit->setText(QString::number(employeeId));
        else
            m_employeeIdEdit->clear();
    }

    ui->addFormTitle->setText(active ? tr("Modifier l'employé") : tr("Employee Information"));
    ui->saveEmployeeBtn->setText(active ? tr("Mettre à jour") : tr("Enregistrer"));
}

void MainWindow::clearEmployeeForm()
{
    setModifyMode(false);
    ui->firstNameEdit->clear();
    if (m_prenomEdit)
        m_prenomEdit->clear();
    ui->emailEdit->clear();
    ui->phoneEdit->clear();
    ui->salaryEdit->clear();
    ui->passwordEdit->clear();
    ui->startDateEdit->setDate(QDate::currentDate());
    ui->departmentComboBox->setCurrentIndex(0);
    ui->posteComboBox->setCurrentIndex(0);
    clearAllFieldErrors();
}

void MainWindow::applyTableRowToForm(int row)
{
    QTableWidget *t = ui->employeeTable_6;

    const int id = tableCellText(t, row, ColId).toInt();
    QString nomPart;
    QString prenomPart;
    splitNomComplet(tableCellText(t, row, ColNom), &nomPart, &prenomPart);
    ui->firstNameEdit->setText(nomPart);
    if (m_prenomEdit)
        m_prenomEdit->setText(prenomPart);

    ui->emailEdit->setText(tableCellText(t, row, ColEmail));
    ui->posteComboBox->setCurrentText(tableCellText(t, row, ColPoste));
    ui->salaryEdit->setText(tableCellText(t, row, ColSalaire));
    ui->departmentComboBox->setCurrentText(tableCellText(t, row, ColDepart));
    ui->phoneEdit->setText(tableCellText(t, row, ColTelephone));

    QDate date = QDate::fromString(tableCellText(t, row, ColDate), QStringLiteral("dd/MM/yyyy"));
    ui->startDateEdit->setDate(date.isValid() ? date : QDate::currentDate());

    ui->passwordEdit->clear();

    setModifyMode(true, id);
}

void MainWindow::loadEmployees()
{
    refreshEmployeeTable();
}

void MainWindow::refreshEmployeeStatistics()
{
    if (m_employeeStatisticsWidget)
        m_employeeStatisticsWidget->refreshStatistics();
}

void MainWindow::setupAssistantTab()
{
    if (!ui->assistantTypingLabel || !ui->assistantChatList || !ui->assistantInput || !ui->assistantSendBtn)
        return;

    ui->assistantTypingLabel->setVisible(false);
    ui->assistantChatList->setWordWrap(true);
    ui->assistantChatList->setSelectionMode(QAbstractItemView::NoSelection);
    ui->assistantChatList->setFocusPolicy(Qt::NoFocus);

    if (!m_employeeAiClient) {
        m_employeeAiClient = new EmployeeAiClient(this);
        connect(m_employeeAiClient, &EmployeeAiClient::responseReady, this, [this](const QString &answer) {
            ui->assistantTypingLabel->setVisible(false);
            appendAssistantMessage(tr("Assistant"), answer);
        });
        connect(m_employeeAiClient, &EmployeeAiClient::requestFailed, this, [this](const QString &error) {
            ui->assistantTypingLabel->setVisible(false);
            appendAssistantMessage(tr("Assistant"),
                                   tr("%1\n\n%2").arg(error, buildAssistantFallback(m_lastAssistantQuestion)));
        });
    }

    connect(ui->assistantSendBtn, &QPushButton::clicked, this, [this]() {
        const QString question = ui->assistantInput->text().trimmed();
        if (question.isEmpty()) {
            appendAssistantMessage(tr("Assistant"), tr("Le message est vide. Écrivez une question avant d'envoyer."));
            return;
        }
        appendAssistantMessage(tr("Vous"), question);
        m_lastAssistantQuestion = question;
        ui->assistantInput->clear();
        ui->assistantTypingLabel->setVisible(true);
        if (!m_employeeAiClient->hasApiKey()) {
            ui->assistantTypingLabel->setVisible(false);
            appendAssistantMessage(tr("Assistant"), buildAssistantFallback(question));
            return;
        }
        m_employeeAiClient->sendQuestion(question, buildAssistantContext());
    });

    connect(ui->assistantInput, &QLineEdit::returnPressed, ui->assistantSendBtn, &QPushButton::click);
    connect(ui->assistantSuggestionTopBtn, &QPushButton::clicked, this,
            [this]() { ui->assistantInput->setText(ui->assistantSuggestionTopBtn->text()); ui->assistantSendBtn->click(); });
    connect(ui->assistantSuggestionPlanningBtn, &QPushButton::clicked, this, [this]() {
        ui->assistantInput->setText(ui->assistantSuggestionPlanningBtn->text());
        ui->assistantSendBtn->click();
    });
    connect(ui->assistantSuggestionRecruitBtn, &QPushButton::clicked, this, [this]() {
        ui->assistantInput->setText(ui->assistantSuggestionRecruitBtn->text());
        ui->assistantSendBtn->click();
    });
}

void MainWindow::appendAssistantMessage(const QString &author, const QString &message)
{
    if (!ui->assistantChatList)
        return;
    auto *item = new QListWidgetItem(QStringLiteral("%1: %2").arg(author, message), ui->assistantChatList);
    item->setBackground(author == tr("Vous") ? QColor(QStringLiteral("#e0f2fe")) : QColor(QStringLiteral("#eef2ff")));
    ui->assistantChatList->addItem(item);
    ui->assistantChatList->scrollToBottom();
}

QString MainWindow::buildAssistantContext() const
{
    QStringList lines;

    QSqlQuery globalStats;
    if (globalStats.exec(QStringLiteral("SELECT COUNT(*), ROUND(AVG(SALAIRE),2), MAX(SALAIRE), MIN(SALAIRE) FROM EMPLOYEE"))
        && globalStats.next()) {
        lines << tr("- Employés total: %1").arg(globalStats.value(0).toInt());
        lines << tr("- Salaire moyen: %1").arg(globalStats.value(1).toString());
        lines << tr("- Salaire max: %1 / min: %2")
                     .arg(globalStats.value(2).toString(), globalStats.value(3).toString());
    }

    QSqlQuery bestEmployee;
    if (bestEmployee.exec(
            QStringLiteral("SELECT NOM, POSTE, DEPART, SALAIRE FROM EMPLOYEE ORDER BY SALAIRE DESC FETCH FIRST 1 ROWS ONLY"))
        && bestEmployee.next()) {
        lines << tr("- Performeur potentiel: %1 | %2 | %3 | salaire %4")
                     .arg(safeAssistantText(bestEmployee.value(0).toString()),
                          safeAssistantText(bestEmployee.value(1).toString()),
                          safeAssistantText(bestEmployee.value(2).toString()),
                          bestEmployee.value(3).toString());
    }

    QSqlQuery depLoad;
    if (depLoad.exec(
            QStringLiteral("SELECT DEPART, COUNT(*) FROM EMPLOYEE GROUP BY DEPART ORDER BY COUNT(*) DESC FETCH FIRST 3 ROWS ONLY"))) {
        while (depLoad.next()) {
            lines << tr("- Charge département: %1 => %2 employés")
                         .arg(safeAssistantText(depLoad.value(0).toString()), depLoad.value(1).toString());
        }
    }

    if (lines.isEmpty()) {
        lines << tr("- Aucune donnée live disponible.");
        lines << tr("- Fournir une recommandation RH simulée et exploitable.");
    }
    return lines.join('\n');
}

QString MainWindow::buildAssistantFallback(const QString &question) const
{
    const QString q = question.simplified().toLower();
    if (q.contains(QStringLiteral("congé")) || q.contains(QStringLiteral("conges")) || q.contains(QStringLiteral("congés"))
        || q.contains(QStringLiteral("conge")) || q.contains(QStringLiteral("conj"))) {
        return tr("Simulation: Pour accepter un congé, vérifiez le solde, l'impact planning et la couverture de poste. "
                  "Si conforme, validez la demande et notifiez l'équipe avec les dates approuvées.");
    }
    if (q.contains(QStringLiteral("perform")) || q.contains(QStringLiteral("top")) || q.contains(QStringLiteral("best"))) {
        return tr("Simulation: Le meilleur performeur est généralement celui avec résultats stables, objectifs atteints et "
                  "bonne présence. Croisez salaire, ancienneté et feedback manager.");
    }
    if (q.contains(QStringLiteral("planning")) || q.contains(QStringLiteral("planification"))) {
        return tr("Simulation: Risques planning: surcharge équipe, chevauchements et absence de backup. "
                  "Action: revue hebdomadaire charge/capacité.");
    }
    if (q.contains(QStringLiteral("recrut"))) {
        return tr("Simulation: Priorisez le recrutement sur les départements en tension et les compétences rares.");
    }
    return tr("Simulation: Je peux répondre sur employés, statistiques, planning, congés et recrutement.");
}

void MainWindow::scheduleEmployeeTableRefresh()
{
    m_employeeSearchDebounce->start();
}

void MainWindow::populateEmployeeSortCombo()
{
    QComboBox *c = ui->employeeSortCombo;
    c->clear();
    const auto add = [&](const QString &label, const QString &data) { c->addItem(label, data); };
    add(tr("ID — croissant"), QStringLiteral("id|asc"));
    add(tr("ID — décroissant"), QStringLiteral("id|desc"));
    add(tr("Nom (famille) — croissant"), QStringLiteral("nom|asc"));
    add(tr("Nom (famille) — décroissant"), QStringLiteral("nom|desc"));
    add(tr("Prénom — croissant"), QStringLiteral("prenom|asc"));
    add(tr("Prénom — décroissant"), QStringLiteral("prenom|desc"));
    add(tr("Date de début — croissant"), QStringLiteral("date|asc"));
    add(tr("Date de début — décroissant"), QStringLiteral("date|desc"));
    add(tr("Salaire — croissant"), QStringLiteral("salaire|asc"));
    add(tr("Salaire — décroissant"), QStringLiteral("salaire|desc"));
    add(tr("Poste — croissant"), QStringLiteral("poste|asc"));
    add(tr("Poste — décroissant"), QStringLiteral("poste|desc"));
    add(tr("Département — croissant"), QStringLiteral("depart|asc"));
    add(tr("Département — décroissant"), QStringLiteral("depart|desc"));
    c->setCurrentIndex(0);
}

void MainWindow::setupEmployeeExportMenu()
{
    auto *menu = new QMenu(this);
    menu->addAction(tr("CSV (Excel)…"), this, &MainWindow::exportEmployeesToCsv);
    menu->addAction(tr("PDF…"), this, &MainWindow::exportEmployeesToPdf);
    menu->addAction(tr("HTML…"), this, &MainWindow::exportEmployeesToHtml);
    ui->exportBtn->setMenu(menu);
}

bool MainWindow::executeEmployeeQuery(QSqlQuery &q)
{
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isOpen()) {
        qWarning() << "Base de données non ouverte";
        return false;
    }

    QString sql = QStringLiteral(
        "SELECT ID, NOM, EMAIL, TELEPHONE, DEPART, POSTE, SALAIRE, DATE_DEBUT FROM EMPLOYEE WHERE 1=1");

    const QString term = ui->searchInput->text().trimmed();
    QVariantMap binds;
    if (!term.isEmpty()) {
        sql += QStringLiteral(
            " AND ("
            "UPPER(NOM) LIKE UPPER(:like) OR "
            "UPPER(EMAIL) LIKE UPPER(:like) OR "
            "UPPER(TELEPHONE) LIKE UPPER(:like) OR "
            "UPPER(DEPART) LIKE UPPER(:like) OR "
            "UPPER(POSTE) LIKE UPPER(:like) OR "
            "TO_CHAR(ID) LIKE :like_plain OR "
            "TO_CHAR(SALAIRE) LIKE :like_plain"
            ")");
        const QString pat = QStringLiteral("%") + term + QStringLiteral("%");
        binds.insert(QStringLiteral(":like"), pat);
        binds.insert(QStringLiteral(":like_plain"), pat);
    }

    QString orderSpec = ui->employeeSortCombo->currentData().toString();
    if (orderSpec.isEmpty())
        orderSpec = QStringLiteral("id|asc");
    const QStringList parts = orderSpec.split(QLatin1Char('|'));
    const QString sortKey = parts.value(0, QStringLiteral("id"));
    const bool desc = (parts.value(1, QStringLiteral("asc")) == QLatin1String("desc"));
    sql += QStringLiteral(" ORDER BY ") + sqlOrderExpression(sortKey) + (desc ? QStringLiteral(" DESC") : QStringLiteral(" ASC"));

    QSqlQuery query(db);
    query.prepare(sql);
    for (auto it = binds.constBegin(); it != binds.constEnd(); ++it)
        query.bindValue(it.key(), it.value());

    if (!query.exec()) {
        qWarning() << "executeEmployeeQuery:" << query.lastError().text();
        return false;
    }
    q = std::move(query);
    return true;
}

void MainWindow::setupEmployeeSearchBar()
{
    ui->searchInput->show();
    ui->searchInput->setPlaceholderText(tr("Chercher Employer"));
    ui->searchBtn->setText(tr("Chercher"));

    populateEmployeeSortCombo();

    connect(ui->searchInput, &QLineEdit::textChanged, this, [this]() {
        scheduleEmployeeTableRefresh();
    });
    connect(ui->searchBtn, &QPushButton::clicked, this, &MainWindow::refreshEmployeeTable);
    connect(ui->employeeSortCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                refreshEmployeeTable();
            });
}

void MainWindow::updateEmployeeTableHeightForUnifiedScroll()
{
    QTableWidget *t = ui->employeeTable_6;
    if (!t)
        return;
    t->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    const int rows = t->rowCount();
    const int headerH = qMax(t->horizontalHeader()->height(),
                             t->horizontalHeader()->sizeHint().height());
    int bodyH = 0;
    if (rows <= 0)
        bodyH = t->verticalHeader()->defaultSectionSize();
    else {
        for (int r = 0; r < rows; ++r)
            bodyH += t->rowHeight(r);
    }
    const int h = headerH + bodyH + 8;
    t->setFixedHeight(qMax(h, 120));
}

void MainWindow::refreshEmployeeTable()
{
    QSqlQuery q;
    if (!executeEmployeeQuery(q))
        return;

    QTableWidget *t = ui->employeeTable_6;
    t->setRowCount(0);
    t->setUpdatesEnabled(false);

    int row = 0;
    while (q.next()) {
        t->insertRow(row);
        t->setItem(row, ColId, new QTableWidgetItem(q.value(QStringLiteral("ID")).toString()));
        t->setItem(row, ColSelect, new QTableWidgetItem());
        t->setItem(row, ColNom, new QTableWidgetItem(q.value(QStringLiteral("NOM")).toString()));
        t->setItem(row, ColEmail, new QTableWidgetItem(q.value(QStringLiteral("EMAIL")).toString()));
        t->setItem(row, ColPoste, new QTableWidgetItem(q.value(QStringLiteral("POSTE")).toString()));
        t->setItem(row, ColSalaire, new QTableWidgetItem(q.value(QStringLiteral("SALAIRE")).toString()));
        t->setItem(row, ColDepart, new QTableWidgetItem(q.value(QStringLiteral("DEPART")).toString()));
        t->setItem(row, ColTelephone, new QTableWidgetItem(q.value(QStringLiteral("TELEPHONE")).toString()));

        QDate date = q.value(QStringLiteral("DATE_DEBUT")).toDate();
        t->setItem(row, ColDate, new QTableWidgetItem(date.toString(QStringLiteral("dd/MM/yyyy"))));
        row++;
    }

    t->resizeColumnsToContents();
    t->setUpdatesEnabled(true);
    t->viewport()->update();

    // Hauteur = toutes les lignes visibles : un seul défilement (QScrollArea parent).
    QTimer::singleShot(0, this, &MainWindow::updateEmployeeTableHeightForUnifiedScroll);
}

void MainWindow::exportEmployeesToCsv()
{
    QSqlQuery q;
    if (!executeEmployeeQuery(q)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible de lire les employés."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export CSV"), QString(),
                                                      tr("CSV (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible d'écrire le fichier."));
        return;
    }

    QTextStream out(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec(QTextCodec::codecForName("UTF-8"));
#endif
    out << "\xEF\xBB\xBF";
    out << "ID;NOM;EMAIL;TELEPHONE;DEPART;POSTE;SALAIRE;DATE_DEBUT\n";

    while (q.next()) {
        QStringList cols;
        cols << q.value(QStringLiteral("ID")).toString()
             << q.value(QStringLiteral("NOM")).toString()
             << q.value(QStringLiteral("EMAIL")).toString()
             << q.value(QStringLiteral("TELEPHONE")).toString()
             << q.value(QStringLiteral("DEPART")).toString()
             << q.value(QStringLiteral("POSTE")).toString()
             << q.value(QStringLiteral("SALAIRE")).toString()
             << q.value(QStringLiteral("DATE_DEBUT")).toDate().toString(Qt::ISODate);
        out << cols.join(QLatin1Char(';')) << QLatin1Char('\n');
    }
    f.close();
    QMessageBox::information(this, tr("Export"), tr("Fichier CSV enregistré."));
}

void MainWindow::exportEmployeesToPdf()
{
    QSqlQuery q;
    if (!executeEmployeeQuery(q)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible de lire les employés."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export PDF"), QString(),
                                                      tr("PDF (*.pdf);;All files (*)"));
    if (path.isEmpty())
        return;

    QString html = QStringLiteral("<html><head><meta charset=\"utf-8\"/></head><body>");
    html += QStringLiteral("<h2>%1</h2><table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">")
                 .arg(htmlEscape(tr("Liste des employés")));
    html += QStringLiteral("<tr><th>%1</th><th>%2</th><th>%3</th><th>%4</th><th>%5</th><th>%6</th><th>%7</th><th>%8</th></tr>")
                .arg(htmlEscape(tr("ID")),
                     htmlEscape(tr("Nom")),
                     htmlEscape(tr("Email")),
                     htmlEscape(tr("Tél.")),
                     htmlEscape(tr("Départ.")),
                     htmlEscape(tr("Poste")),
                     htmlEscape(tr("Salaire")),
                     htmlEscape(tr("Date")));

    while (q.next()) {
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td><td>%7</td><td>%8</td></tr>")
                    .arg(htmlEscape(q.value(QStringLiteral("ID")).toString()),
                         htmlEscape(q.value(QStringLiteral("NOM")).toString()),
                         htmlEscape(q.value(QStringLiteral("EMAIL")).toString()),
                         htmlEscape(q.value(QStringLiteral("TELEPHONE")).toString()),
                         htmlEscape(q.value(QStringLiteral("DEPART")).toString()),
                         htmlEscape(q.value(QStringLiteral("POSTE")).toString()),
                         htmlEscape(q.value(QStringLiteral("SALAIRE")).toString()),
                         htmlEscape(q.value(QStringLiteral("DATE_DEBUT")).toDate().toString(Qt::ISODate)));
    }
    html += QStringLiteral("</table></body></html>");

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);

    QTextDocument doc;
    doc.setHtml(html);
    doc.print(&printer);

    QMessageBox::information(this, tr("Export"), tr("Fichier PDF enregistré."));
}

void MainWindow::exportEmployeesToHtml()
{
    QSqlQuery q;
    if (!executeEmployeeQuery(q)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible de lire les employés."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export HTML"), QString(),
                                                      tr("HTML (*.html);;All files (*)"));
    if (path.isEmpty())
        return;

    QString html = QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"/><title>%1</title></head><body>")
                       .arg(htmlEscape(tr("Employés")));
    html += QStringLiteral("<h2>%1</h2><table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">")
                .arg(htmlEscape(tr("Liste des employés")));
    html += QStringLiteral("<tr><th>%1</th><th>%2</th><th>%3</th><th>%4</th><th>%5</th><th>%6</th><th>%7</th><th>%8</th></tr>")
                .arg(htmlEscape(tr("ID")),
                     htmlEscape(tr("Nom")),
                     htmlEscape(tr("Email")),
                     htmlEscape(tr("Tél.")),
                     htmlEscape(tr("Départ.")),
                     htmlEscape(tr("Poste")),
                     htmlEscape(tr("Salaire")),
                     htmlEscape(tr("Date")));

    while (q.next()) {
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td><td>%6</td><td>%7</td><td>%8</td></tr>")
                    .arg(htmlEscape(q.value(QStringLiteral("ID")).toString()),
                         htmlEscape(q.value(QStringLiteral("NOM")).toString()),
                         htmlEscape(q.value(QStringLiteral("EMAIL")).toString()),
                         htmlEscape(q.value(QStringLiteral("TELEPHONE")).toString()),
                         htmlEscape(q.value(QStringLiteral("DEPART")).toString()),
                         htmlEscape(q.value(QStringLiteral("POSTE")).toString()),
                         htmlEscape(q.value(QStringLiteral("SALAIRE")).toString()),
                         htmlEscape(q.value(QStringLiteral("DATE_DEBUT")).toDate().toString(Qt::ISODate)));
    }
    html += QStringLiteral("</table></body></html>");

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible d'écrire le fichier."));
        return;
    }
    QTextStream out(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec(QTextCodec::codecForName("UTF-8"));
#endif
    out << html;
    f.close();
    QMessageBox::information(this, tr("Export"), tr("Fichier HTML enregistré."));
}

// --- Équipements (CRUD + recherche + tri, même principe que les employés) ---

void MainWindow::setupEquipmentModule()
{
    m_equipmentSearchDebounce = new QTimer(this);
    m_equipmentSearchDebounce->setSingleShot(true);
    m_equipmentSearchDebounce->setInterval(350);
    connect(m_equipmentSearchDebounce, &QTimer::timeout, this, &MainWindow::refreshEquipmentTable);

    // Keep layout spacing from .ui unchanged.

    ui->firstNameLabel_3->setText(tr("Référence"));
    ui->searchInput_3->setPlaceholderText(tr("Rechercher un équipement…"));
    populateEquipmentSortCombo();

    QTableWidget *tb = ui->employeeTable_3;
    tb->setColumnCount(9);
    tb->setHorizontalHeaderLabels({tr("ID"),
                                   tr("Référence"),
                                   tr("Modèle"),
                                   tr("Marque"),
                                   tr("Type"),
                                   tr("État"),
                                   tr("N° série"),
                                   tr("Coût"),
                                   tr("Date d'ajout")});
    tb->setColumnHidden(EqColId, true);
    tb->setSelectionBehavior(QAbstractItemView::SelectRows);
    tb->setSelectionMode(QAbstractItemView::SingleSelection);
    tb->setWordWrap(false);
    tb->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tb->verticalHeader()->setDefaultSectionSize(48);
    ui->modifyBtn_3->setEnabled(true);
    ui->deleteBtn_3->setEnabled(true);

    if (m_salaryValidator && ui->salaryEdit_3) {
        ui->salaryEdit_3->setValidator(m_salaryValidator);
    }

    connect(ui->searchInput_3, &QLineEdit::textChanged, this, [this]() { scheduleEquipmentTableRefresh(); });
    connect(ui->searchBtn_3, &QPushButton::clicked, this, &MainWindow::refreshEquipmentTable);
    if (auto *equipmentSortCombo = findCombo(this, "equipmentSortCombo_3")) {
        connect(equipmentSortCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
                [this](int) { refreshEquipmentTable(); });
    }

    connect(ui->saveEmployeeBtn_3, &QPushButton::clicked, this, [this]() {
        QString err;
        if (!validateEquipmentInputs(&err)) {
            QMessageBox::warning(this, tr("Données invalides"), err);
            return;
        }
        const QString ref = ui->firstNameEdit_3->text().trimmed();
        const QString modele = ui->emailEdit_3->text().trimmed();
        const QString marque = ui->roleComboBox_3->currentText();
        const QString type = ui->roleComboBox_6->currentText();
        const QString etat = ui->roleComboBox_7->currentText();
        const QString serie = ui->emailEdit_6->text().trimmed();
        bool okC = false;
        const double cout = QLocale::system().toDouble(ui->salaryEdit_3->text().trimmed(), &okC);
        (void)okC;
        const QDate d = ui->startDateEdit_3->date();

        Equipement E(ref, modele, marque, type, etat, serie, cout, d);
        bool ok = false;
        if (m_equipmentModifyMode && m_modifyEquipmentId > 0)
            ok = E.modifier(m_modifyEquipmentId);
        else
            ok = E.ajouter();

        if (ok) {
            clearEquipmentForm();
            QTimer::singleShot(0, this, [this]() { refreshEquipmentTable(); });
            QMessageBox::information(this, tr("Succès"), tr("Opération réussie."));
        } else {
            QMessageBox::critical(this, tr("Erreur"),
                                  tr("Échec (table EQUIPEMENT absente, contrainte Oracle, etc.)."));
        }
    });

    connect(ui->cancelBtn_3, &QPushButton::clicked, this, [this]() { clearEquipmentForm(); });

    connect(ui->deleteBtn_3, &QPushButton::clicked, this, [this]() {
        QTableWidget *t = ui->employeeTable_3;
        const int row = t->currentRow();
        if (row < 0) {
            QMessageBox::warning(this, tr("Attention"), tr("Sélectionnez un équipement."));
            return;
        }
        const int id = tableCellText(t, row, EqColId).toInt();
        if (EtmpEquip.supprimer(id)) {
            refreshEquipmentTable();
            QMessageBox::information(this, tr("Succès"), tr("Équipement supprimé."));
        } else {
            QMessageBox::critical(this, tr("Erreur"), tr("Échec de la suppression."));
        }
    });

    connect(ui->modifyBtn_3, &QPushButton::clicked, this, [this]() {
        const int row = ui->employeeTable_3->currentRow();
        if (row < 0) {
            QMessageBox::warning(this, tr("Attention"), tr("Sélectionnez un équipement."));
            return;
        }
        applyEquipmentRowToForm(row);
        if (auto *equipmentScroll = findScrollArea(this, "equipmentAddListScrollArea"))
            equipmentScroll->ensureWidgetVisible(ui->addClientContainer_3);
    });

    setupEquipmentExportMenu();
    initialiserStatistiquesEquipement();
    connect(ui->ProductsTab, &QTabWidget::currentChanged, this, [this](int index) {
        if (!ui->ProductsTab) {
            return;
        }
        QWidget *tab = ui->ProductsTab->widget(index);
        if (tab && tab->objectName() == QStringLiteral("equipmentStatsTabDynamic")) {
            mettreAJourStatistiquesEquipement();
        }
    });
}

void MainWindow::initialiserStatistiquesEquipement()
{
    if (!ui->ProductsTab) {
        return;
    }
    if (ui->ProductsTab->findChild<QWidget *>(QStringLiteral("equipmentStatsTabDynamic"))) {
        return;
    }

    auto *statsTab = new QWidget(ui->ProductsTab);
    statsTab->setObjectName(QStringLiteral("equipmentStatsTabDynamic"));
    auto *mainLayout = new QVBoxLayout(statsTab);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(14);

    auto *title = new QLabel(tr("Statistiques equipements"), statsTab);
    title->setStyleSheet(QStringLiteral("QLabel { color: #0f172a; font-size: 22px; font-weight: 700; }"));
    auto *subtitle = new QLabel(tr("Vue globale des couts, etats et ajouts mensuels"), statsTab);
    subtitle->setStyleSheet(QStringLiteral("QLabel { color: #475569; font-size: 12px; }"));
    mainLayout->addWidget(title);
    mainLayout->addWidget(subtitle);

    auto *cardsRow = new QHBoxLayout();
    cardsRow->setSpacing(12);
    auto createCard = [statsTab](const QString &titleText, const QString &valueObject, const QString &accentColor) {
        auto *card = new QFrame(statsTab);
        card->setStyleSheet(QStringLiteral(
            "QFrame { background: #ffffff; border: 1px solid #dbe4ea; border-left: 5px solid %1; border-radius: 12px; }")
                                .arg(accentColor));
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(6);
        auto *lblTitle = new QLabel(titleText, card);
        lblTitle->setStyleSheet(QStringLiteral("QLabel { color: #475569; font-size: 12px; }"));
        auto *lblValue = new QLabel(QStringLiteral("0"), card);
        lblValue->setObjectName(valueObject);
        lblValue->setStyleSheet(QStringLiteral("QLabel { color: #2e7d32; font-size: 24px; font-weight: 700; }"));
        layout->addWidget(lblTitle);
        layout->addWidget(lblValue);
        layout->addStretch();
        return card;
    };

    cardsRow->addWidget(createCard(tr("Total equipements"), QStringLiteral("equipmentStatsTotalValue"), QStringLiteral("#0f766e")));
    cardsRow->addWidget(createCard(tr("Cout total"), QStringLiteral("equipmentStatsCostValue"), QStringLiteral("#1d4ed8")));
    cardsRow->addWidget(createCard(tr("Cout moyen"), QStringLiteral("equipmentStatsAvgValue"), QStringLiteral("#b45309")));
    mainLayout->addLayout(cardsRow);

    auto *chartsRow = new QHBoxLayout();
    chartsRow->setSpacing(12);

    auto *pieView = new QChartView(statsTab);
    pieView->setObjectName(QStringLiteral("equipmentStatsPieView"));
    pieView->setRenderHint(QPainter::Antialiasing);
    pieView->setMinimumHeight(290);
    pieView->setStyleSheet(QStringLiteral("background: #ffffff; border: 1px solid #dbe4ea; border-radius: 12px;"));

    auto *trendView = new QChartView(statsTab);
    trendView->setObjectName(QStringLiteral("equipmentStatsTrendView"));
    trendView->setRenderHint(QPainter::Antialiasing);
    trendView->setMinimumHeight(290);
    trendView->setStyleSheet(QStringLiteral("background: #ffffff; border: 1px solid #dbe4ea; border-radius: 12px;"));

    chartsRow->addWidget(pieView, 1);
    chartsRow->addWidget(trendView, 2);
    mainLayout->addLayout(chartsRow);
    mainLayout->addStretch();

    ui->ProductsTab->addTab(statsTab, tr("Statistiques"));
}

void MainWindow::mettreAJourStatistiquesEquipement()
{
    if (!ui->ProductsTab) {
        return;
    }
    QWidget *statsTab = ui->ProductsTab->findChild<QWidget *>(QStringLiteral("equipmentStatsTabDynamic"));
    if (!statsTab) {
        return;
    }

    auto *totalValue = statsTab->findChild<QLabel *>(QStringLiteral("equipmentStatsTotalValue"));
    auto *costValue = statsTab->findChild<QLabel *>(QStringLiteral("equipmentStatsCostValue"));
    auto *avgValue = statsTab->findChild<QLabel *>(QStringLiteral("equipmentStatsAvgValue"));
    auto *pieView = statsTab->findChild<QChartView *>(QStringLiteral("equipmentStatsPieView"));
    auto *trendView = statsTab->findChild<QChartView *>(QStringLiteral("equipmentStatsTrendView"));
    if (!totalValue || !costValue || !avgValue || !pieView || !trendView) {
        return;
    }

    int total = 0;
    double totalCost = 0.0;
    double avgCost = 0.0;
    QSqlQuery aggregate;
    if (aggregate.exec(QStringLiteral(
            "SELECT COUNT(*), NVL(SUM(NVL(COUT,0)), 0), NVL(AVG(NVL(COUT,0)), 0) FROM EQUIPEMENT")) &&
        aggregate.next()) {
        total = aggregate.value(0).toInt();
        totalCost = aggregate.value(1).toDouble();
        avgCost = aggregate.value(2).toDouble();
    }

    totalValue->setText(QString::number(total));
    costValue->setText(formatTnd(totalCost));
    avgValue->setText(formatTnd(avgCost));

    auto *pieSeries = new QPieSeries();
    QSqlQuery etatQuery;
    etatQuery.prepare(QStringLiteral(
        "SELECT ETAT_LABEL, CNT "
        "FROM ("
        "  SELECT NVL(ETAT, 'Non renseigne') AS ETAT_LABEL, COUNT(*) AS CNT "
        "  FROM EQUIPEMENT "
        "  GROUP BY NVL(ETAT, 'Non renseigne') "
        "  ORDER BY CNT DESC"
        ") "
        "WHERE ROWNUM <= 6"));
    if (etatQuery.exec()) {
        while (etatQuery.next()) {
            const QString label = etatQuery.value(0).toString();
            const qreal value = etatQuery.value(1).toDouble();
            if (value > 0.0) {
                pieSeries->append(label, value);
            }
        }
    }
    if (pieSeries->slices().isEmpty()) {
        pieSeries->append(tr("Aucune donnee"), 1.0);
    }
    for (QPieSlice *slice : pieSeries->slices()) {
        slice->setLabelVisible(true);
    }
    auto *pieChart = new QChart();
    pieChart->addSeries(pieSeries);
    pieChart->setTitle(tr("Repartition par etat"));
    pieChart->setAnimationOptions(QChart::SeriesAnimations);
    pieChart->legend()->setAlignment(Qt::AlignRight);
    pieChart->setBackgroundVisible(false);
    QChart *oldPieChart = pieView->chart();
    pieView->setChart(pieChart);
    if (oldPieChart) {
        delete oldPieChart;
    }

    QMap<QString, double> monthlyCosts;
    QSqlQuery monthlyQuery;
    monthlyQuery.prepare(QStringLiteral(
        "SELECT TO_CHAR(TRUNC(DATE_AJOUT, 'MM'), 'YYYY-MM') AS MKEY, NVL(SUM(NVL(COUT,0)), 0) "
        "FROM EQUIPEMENT "
        "WHERE DATE_AJOUT >= ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -5) "
        "GROUP BY TRUNC(DATE_AJOUT, 'MM') "
        "ORDER BY TRUNC(DATE_AJOUT, 'MM')"));
    if (monthlyQuery.exec()) {
        while (monthlyQuery.next()) {
            monthlyCosts.insert(monthlyQuery.value(0).toString(), monthlyQuery.value(1).toDouble());
        }
    }

    auto *lineSeries = new QSplineSeries();
    lineSeries->setName(tr("Cout ajoute"));
    QPen linePen(QColor("#2e7d32"));
    linePen.setWidth(3);
    lineSeries->setPen(linePen);
    lineSeries->setPointsVisible(true);

    QVector<qint64> xPoints;
    double maxY = 0.0;
    QDate cursor = QDate::currentDate().addMonths(-5);
    for (int i = 0; i < 6; ++i) {
        const QString key = cursor.toString(QStringLiteral("yyyy-MM"));
        const double value = monthlyCosts.value(key, 0.0);
        const qint64 x = QDateTime(QDate(cursor.year(), cursor.month(), 1), QTime(0, 0)).toMSecsSinceEpoch();
        xPoints.push_back(x);
        lineSeries->append(static_cast<qreal>(x), value);
        maxY = qMax(maxY, value);
        cursor = cursor.addMonths(1);
    }

    auto *lineChart = new QChart();
    lineChart->addSeries(lineSeries);
    lineChart->setTitle(tr("Evolution du cout par mois"));
    lineChart->setAnimationOptions(QChart::SeriesAnimations);
    lineChart->legend()->hide();
    lineChart->setBackgroundVisible(false);

    auto *axisX = new QDateTimeAxis();
    axisX->setFormat(QStringLiteral("MMM yy"));
    axisX->setTickCount(6);
    if (!xPoints.isEmpty()) {
        axisX->setRange(QDateTime::fromMSecsSinceEpoch(xPoints.first()),
                        QDateTime::fromMSecsSinceEpoch(xPoints.last()));
    }

    auto *axisY = new QValueAxis();
    axisY->setLabelFormat(QStringLiteral("%.0f"));
    axisY->setRange(0.0, maxY > 0.0 ? maxY * 1.2 : 10.0);
    axisY->setTickCount(5);

    lineChart->addAxis(axisX, Qt::AlignBottom);
    lineChart->addAxis(axisY, Qt::AlignLeft);
    lineSeries->attachAxis(axisX);
    lineSeries->attachAxis(axisY);

    QChart *oldLineChart = trendView->chart();
    trendView->setChart(lineChart);
    if (oldLineChart) {
        delete oldLineChart;
    }
}

void MainWindow::populateEquipmentSortCombo()
{
    QComboBox *c = findCombo(this, "equipmentSortCombo_3");
    if (!c)
        return;
    c->clear();
    const auto add = [&](const QString &label, const QString &data) { c->addItem(label, data); };
    add(tr("ID — croissant"), QStringLiteral("id|asc"));
    add(tr("ID — décroissant"), QStringLiteral("id|desc"));
    add(tr("Référence — A→Z"), QStringLiteral("reference|asc"));
    add(tr("Référence — Z→A"), QStringLiteral("reference|desc"));
    add(tr("Modèle — A→Z"), QStringLiteral("modele|asc"));
    add(tr("Modèle — Z→A"), QStringLiteral("modele|desc"));
    add(tr("Marque — A→Z"), QStringLiteral("marque|asc"));
    add(tr("Marque — Z→A"), QStringLiteral("marque|desc"));
    add(tr("Type — A→Z"), QStringLiteral("type|asc"));
    add(tr("Type — Z→A"), QStringLiteral("type|desc"));
    add(tr("État — A→Z"), QStringLiteral("etat|asc"));
    add(tr("État — Z→A"), QStringLiteral("etat|desc"));
    add(tr("Coût — croissant"), QStringLiteral("cout|asc"));
    add(tr("Coût — décroissant"), QStringLiteral("cout|desc"));
    add(tr("Date — croissant"), QStringLiteral("date|asc"));
    add(tr("Date — décroissant"), QStringLiteral("date|desc"));
    c->setCurrentIndex(0);
}

bool MainWindow::executeEquipmentQuery(QSqlQuery &q)
{
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isOpen()) {
        qWarning() << "Base de données non ouverte";
        return false;
    }

    QString sql = QStringLiteral(
        "SELECT ID, REFERENCE, MODELE, MARQUE, TYPE_EQUI, ETAT, NUMERO_SERIE, COUT, DATE_AJOUT "
        "FROM EQUIPEMENT WHERE 1=1");

    const QString term = ui->searchInput_3->text().trimmed();
    QVariantMap binds;
    if (!term.isEmpty()) {
        sql += QStringLiteral(
            " AND ("
            "UPPER(REFERENCE) LIKE UPPER(:like) OR "
            "UPPER(MODELE) LIKE UPPER(:like) OR "
            "UPPER(MARQUE) LIKE UPPER(:like) OR "
            "UPPER(TYPE_EQUI) LIKE UPPER(:like) OR "
            "UPPER(ETAT) LIKE UPPER(:like) OR "
            "UPPER(NUMERO_SERIE) LIKE UPPER(:like) OR "
            "TO_CHAR(ID) LIKE :like_plain OR "
            "TO_CHAR(COUT) LIKE :like_plain"
            ")");
        const QString pat = QStringLiteral("%") + term + QStringLiteral("%");
        binds.insert(QStringLiteral(":like"), pat);
        binds.insert(QStringLiteral(":like_plain"), pat);
    }

    QString orderSpec;
    if (auto *equipmentSortCombo = findCombo(this, "equipmentSortCombo_3"))
        orderSpec = equipmentSortCombo->currentData().toString();
    if (orderSpec.isEmpty())
        orderSpec = QStringLiteral("id|asc");
    const QStringList parts = orderSpec.split(QLatin1Char('|'));
    const QString sortKey = parts.value(0, QStringLiteral("id"));
    const bool desc = (parts.value(1, QStringLiteral("asc")) == QLatin1String("desc"));
    sql += QStringLiteral(" ORDER BY ") + equipmentSqlOrderExpr(sortKey)
        + (desc ? QStringLiteral(" DESC") : QStringLiteral(" ASC"));

    QSqlQuery query(db);
    query.prepare(sql);
    for (auto it = binds.constBegin(); it != binds.constEnd(); ++it)
        query.bindValue(it.key(), it.value());

    if (!query.exec()) {
        qWarning() << "executeEquipmentQuery:" << query.lastError().text();
        return false;
    }
    q = std::move(query);
    return true;
}

void MainWindow::scheduleEquipmentTableRefresh()
{
    m_equipmentSearchDebounce->start();
}

void MainWindow::loadEquipmentTable()
{
    refreshEquipmentTable();
}

void MainWindow::setEquipmentModifyMode(bool active, int equipmentId)
{
    m_equipmentModifyMode = active;
    m_modifyEquipmentId = equipmentId;
    ui->addFormTitle_3->setText(active ? tr("Modifier l'équipement") : tr("information de l'equippement"));
    ui->saveEmployeeBtn_3->setText(active ? tr("Mettre à jour") : tr("Enregistrer"));
}

void MainWindow::clearEquipmentForm()
{
    setEquipmentModifyMode(false, -1);
    ui->firstNameEdit_3->clear();
    ui->emailEdit_3->clear();
    ui->emailEdit_6->clear();
    ui->salaryEdit_3->clear();
    ui->startDateEdit_3->setDate(QDate::currentDate());
    ui->roleComboBox_3->setCurrentIndex(0);
    ui->roleComboBox_6->setCurrentIndex(0);
    ui->roleComboBox_7->setCurrentIndex(0);
}

void MainWindow::applyEquipmentRowToForm(int row)
{
    QTableWidget *t = ui->employeeTable_3;
    const int id = tableCellText(t, row, EqColId).toInt();
    ui->firstNameEdit_3->setText(tableCellText(t, row, EqColRef));
    ui->emailEdit_3->setText(tableCellText(t, row, EqColModele));
    ui->roleComboBox_3->setCurrentText(tableCellText(t, row, EqColMarque));
    ui->roleComboBox_6->setCurrentText(tableCellText(t, row, EqColType));
    ui->roleComboBox_7->setCurrentText(tableCellText(t, row, EqColEtat));
    ui->emailEdit_6->setText(tableCellText(t, row, EqColSerie));
    ui->salaryEdit_3->setText(tableCellText(t, row, EqColCout));

    QDate date = QDate::fromString(tableCellText(t, row, EqColDate), QStringLiteral("dd/MM/yyyy"));
    ui->startDateEdit_3->setDate(date.isValid() ? date : QDate::currentDate());

    setEquipmentModifyMode(true, id);
}

bool MainWindow::validateEquipmentInputs(QString *err)
{
    if (ui->firstNameEdit_3->text().trimmed().isEmpty()) {
        if (err)
            *err = tr("La référence est obligatoire.");
        return false;
    }
    bool ok = false;
    QLocale::system().toDouble(ui->salaryEdit_3->text().trimmed(), &ok);
    if (!ok) {
        if (err)
            *err = tr("Le coût doit être un nombre valide.");
        return false;
    }
    return true;
}

void MainWindow::updateEquipmentTableHeightForUnifiedScroll()
{
    QTableWidget *t = ui->employeeTable_3;
    if (!t)
        return;
    t->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const int rows = t->rowCount();
    const int headerH = qMax(t->horizontalHeader()->height(), t->horizontalHeader()->sizeHint().height());
    int bodyH = 0;
    if (rows <= 0)
        bodyH = t->verticalHeader()->defaultSectionSize();
    else {
        for (int r = 0; r < rows; ++r)
            bodyH += t->rowHeight(r);
    }
    const int h = headerH + bodyH + 8;
    t->setFixedHeight(qMax(h, 120));
}

void MainWindow::refreshEquipmentTable()
{
    QSqlQuery q;
    if (!executeEquipmentQuery(q))
        return;

    QTableWidget *t = ui->employeeTable_3;
    t->setRowCount(0);
    t->setUpdatesEnabled(false);

    int row = 0;
    while (q.next()) {
        t->insertRow(row);
        t->setItem(row, EqColId, new QTableWidgetItem(q.value(QStringLiteral("ID")).toString()));
        t->setItem(row, EqColRef, new QTableWidgetItem(q.value(QStringLiteral("REFERENCE")).toString()));
        t->setItem(row, EqColModele, new QTableWidgetItem(q.value(QStringLiteral("MODELE")).toString()));
        t->setItem(row, EqColMarque, new QTableWidgetItem(q.value(QStringLiteral("MARQUE")).toString()));
        t->setItem(row, EqColType, new QTableWidgetItem(q.value(QStringLiteral("TYPE_EQUI")).toString()));
        t->setItem(row, EqColEtat, new QTableWidgetItem(q.value(QStringLiteral("ETAT")).toString()));
        t->setItem(row, EqColSerie, new QTableWidgetItem(q.value(QStringLiteral("NUMERO_SERIE")).toString()));
        const double c = q.value(QStringLiteral("COUT")).toDouble();
        t->setItem(row, EqColCout, new QTableWidgetItem(QLocale::system().toString(c, 'f', 2)));
        const QDate d = q.value(QStringLiteral("DATE_AJOUT")).toDate();
        t->setItem(row, EqColDate, new QTableWidgetItem(d.toString(QStringLiteral("dd/MM/yyyy"))));
        row++;
    }

    t->resizeColumnsToContents();
    t->setUpdatesEnabled(true);
    t->viewport()->update();
    QTimer::singleShot(0, this, &MainWindow::updateEquipmentTableHeightForUnifiedScroll);
    mettreAJourStatistiquesEquipement();
}

void MainWindow::setupEquipmentExportMenu()
{
    auto *menu = new QMenu(this);
    menu->addAction(tr("CSV (Excel)…"), this, &MainWindow::exportEquipmentToCsv);
    menu->addAction(tr("PDF…"), this, &MainWindow::exportEquipmentToPdf);
    menu->addAction(tr("HTML…"), this, &MainWindow::exportEquipmentToHtml);
    ui->exportBtn_3->setMenu(menu);
}

void MainWindow::exportEquipmentToCsv()
{
    QSqlQuery q;
    if (!executeEquipmentQuery(q)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible de lire les équipements."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export CSV"), QString(),
                                                      tr("CSV (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible d'écrire le fichier."));
        return;
    }

    QTextStream out(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec(QTextCodec::codecForName("UTF-8"));
#endif
    out << "\xEF\xBB\xBF";
    out << "ID;REFERENCE;MODELE;MARQUE;TYPE_EQUI;ETAT;NUMERO_SERIE;COUT;DATE_AJOUT\n";

    while (q.next()) {
        QStringList cols;
        cols << q.value(QStringLiteral("ID")).toString()
             << q.value(QStringLiteral("REFERENCE")).toString()
             << q.value(QStringLiteral("MODELE")).toString()
             << q.value(QStringLiteral("MARQUE")).toString()
             << q.value(QStringLiteral("TYPE_EQUI")).toString()
             << q.value(QStringLiteral("ETAT")).toString()
             << q.value(QStringLiteral("NUMERO_SERIE")).toString()
             << q.value(QStringLiteral("COUT")).toString()
             << q.value(QStringLiteral("DATE_AJOUT")).toDate().toString(Qt::ISODate);
        out << cols.join(QLatin1Char(';')) << QLatin1Char('\n');
    }
    f.close();
    QMessageBox::information(this, tr("Export"), tr("Fichier CSV enregistré."));
}

void MainWindow::exportEquipmentToPdf()
{
    QSqlQuery q;
    if (!executeEquipmentQuery(q)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible de lire les équipements."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export PDF"), QString(),
                                                      tr("PDF (*.pdf);;All files (*)"));
    if (path.isEmpty())
        return;

    QString html = QStringLiteral("<html><head><meta charset=\"utf-8\"/></head><body>");
    html += QStringLiteral("<h2>%1</h2><table border=\"1\" cellspacing=\"0\" cellpadding=\"3\"><tr>")
                 .arg(htmlEscape(tr("Liste des équipements")));
    html += QStringLiteral("<th>%1</th><th>%2</th><th>%3</th><th>%4</th><th>%5</th>"
                           "<th>%6</th><th>%7</th><th>%8</th><th>%9</th></tr>")
                .arg(htmlEscape(tr("ID")),
                     htmlEscape(tr("Réf.")),
                     htmlEscape(tr("Modèle")),
                     htmlEscape(tr("Marque")),
                     htmlEscape(tr("Type")),
                     htmlEscape(tr("État")),
                     htmlEscape(tr("N° série")),
                     htmlEscape(tr("Coût")),
                     htmlEscape(tr("Date")));

    while (q.next()) {
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td>"
                               "<td>%6</td><td>%7</td><td>%8</td><td>%9</td></tr>")
                    .arg(htmlEscape(q.value(QStringLiteral("ID")).toString()),
                         htmlEscape(q.value(QStringLiteral("REFERENCE")).toString()),
                         htmlEscape(q.value(QStringLiteral("MODELE")).toString()),
                         htmlEscape(q.value(QStringLiteral("MARQUE")).toString()),
                         htmlEscape(q.value(QStringLiteral("TYPE_EQUI")).toString()),
                         htmlEscape(q.value(QStringLiteral("ETAT")).toString()),
                         htmlEscape(q.value(QStringLiteral("NUMERO_SERIE")).toString()),
                         htmlEscape(q.value(QStringLiteral("COUT")).toString()),
                         htmlEscape(q.value(QStringLiteral("DATE_AJOUT")).toDate().toString(Qt::ISODate)));
    }
    html += QStringLiteral("</table></body></html>");

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);

    QTextDocument doc;
    doc.setHtml(html);
    doc.print(&printer);

    QMessageBox::information(this, tr("Export"), tr("Fichier PDF enregistré."));
}

void MainWindow::exportEquipmentToHtml()
{
    QSqlQuery q;
    if (!executeEquipmentQuery(q)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible de lire les équipements."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export HTML"), QString(),
                                                      tr("HTML (*.html);;All files (*)"));
    if (path.isEmpty())
        return;

    QString html = QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"/><title>%1</title></head><body>")
                       .arg(htmlEscape(tr("Équipements")));
    html += QStringLiteral("<h2>%1</h2><table border=\"1\" cellspacing=\"0\" cellpadding=\"3\"><tr>")
                .arg(htmlEscape(tr("Liste des équipements")));
    html += QStringLiteral("<th>%1</th><th>%2</th><th>%3</th><th>%4</th><th>%5</th>"
                           "<th>%6</th><th>%7</th><th>%8</th><th>%9</th></tr>")
                .arg(htmlEscape(tr("ID")),
                     htmlEscape(tr("Réf.")),
                     htmlEscape(tr("Modèle")),
                     htmlEscape(tr("Marque")),
                     htmlEscape(tr("Type")),
                     htmlEscape(tr("État")),
                     htmlEscape(tr("N° série")),
                     htmlEscape(tr("Coût")),
                     htmlEscape(tr("Date")));

    while (q.next()) {
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td>"
                               "<td>%6</td><td>%7</td><td>%8</td><td>%9</td></tr>")
                    .arg(htmlEscape(q.value(QStringLiteral("ID")).toString()),
                         htmlEscape(q.value(QStringLiteral("REFERENCE")).toString()),
                         htmlEscape(q.value(QStringLiteral("MODELE")).toString()),
                         htmlEscape(q.value(QStringLiteral("MARQUE")).toString()),
                         htmlEscape(q.value(QStringLiteral("TYPE_EQUI")).toString()),
                         htmlEscape(q.value(QStringLiteral("ETAT")).toString()),
                         htmlEscape(q.value(QStringLiteral("NUMERO_SERIE")).toString()),
                         htmlEscape(q.value(QStringLiteral("COUT")).toString()),
                         htmlEscape(q.value(QStringLiteral("DATE_AJOUT")).toDate().toString(Qt::ISODate)));
    }
    html += QStringLiteral("</table></body></html>");

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible d'écrire le fichier."));
        return;
    }
    QTextStream out(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec(QTextCodec::codecForName("UTF-8"));
#endif
    out << html;
    f.close();
    QMessageBox::information(this, tr("Export"), tr("Fichier HTML enregistré."));
}

// --- Stock (CRUD + recherche + tri) ---

void MainWindow::setupStockModule()
{
    m_stockSearchDebounce = new QTimer(this);
    m_stockSearchDebounce->setSingleShot(true);
    m_stockSearchDebounce->setInterval(350);
    connect(m_stockSearchDebounce, &QTimer::timeout, this, &MainWindow::refreshStockTable);

    // Keep layout spacing from .ui unchanged.

    populateStockSortCombo();

    QTableWidget *tb = ui->stockTable;
    tb->setColumnHidden(StColId, true);
    tb->setSelectionBehavior(QAbstractItemView::SelectRows);
    tb->setSelectionMode(QAbstractItemView::SingleSelection);
    tb->setWordWrap(false);
    tb->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tb->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    tb->horizontalHeader()->setStretchLastSection(true);
    tb->verticalHeader()->setDefaultSectionSize(48);
    if (auto *stockModifyBtn = findButton(this, "stockModifyBtn"))
        stockModifyBtn->setEnabled(true);
    if (auto *stockDeleteBtn = findButton(this, "stockDeleteBtn"))
        stockDeleteBtn->setEnabled(true);

    if (m_salaryValidator && ui->marqueEdit_7)
        ui->marqueEdit_7->setValidator(m_salaryValidator);

    if (auto *stockSearchInput = findLineEdit(this, "stockSearchInput")) {
        connect(stockSearchInput, &QLineEdit::textChanged, this, [this]() { scheduleStockTableRefresh(); });
    }
    if (auto *stockSearchBtn = findButton(this, "stockSearchBtn")) {
        connect(stockSearchBtn, &QPushButton::clicked, this, &MainWindow::refreshStockTable);
    }
    if (auto *stockSortCombo = findCombo(this, "stockSortCombo")) {
        connect(stockSortCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
                [this](int) { refreshStockTable(); });
    }

    connect(ui->saveStockBtn, &QPushButton::clicked, this, [this]() {
        QString err;
        if (!validateStockInputs(&err)) {
            QMessageBox::warning(this, tr("Données invalides"), err);
            return;
        }
        const QString typeP = ui->emailEdit_12->text().trimmed();
        const QString region = ui->emailEdit_11->text().trimmed();
        bool okQ = false;
        const double qty = QLocale::system().toDouble(ui->marqueEdit_7->text().trimmed(), &okQ);
        (void)okQ;
        const QDate d = ui->startDateEdit_7->date();

        Stock S(typeP, region, qty, d);
        bool ok = false;
        if (m_stockModifyMode && m_modifyStockId > 0)
            ok = S.modifier(m_modifyStockId);
        else
            ok = S.ajouter();

        if (ok) {
            clearStockForm();
            QTimer::singleShot(0, this, [this]() { refreshStockTable(); });
            QMessageBox::information(this, tr("Succès"), tr("Opération réussie."));
        } else {
            QMessageBox::critical(this, tr("Erreur"),
                                  tr("Échec (table STOCK absente, contrainte Oracle, etc.)."));
        }
    });

    connect(ui->cancelStockBtn, &QPushButton::clicked, this, [this]() { clearStockForm(); });

    if (auto *stockDeleteBtn = findButton(this, "stockDeleteBtn")) {
        connect(stockDeleteBtn, &QPushButton::clicked, this, [this]() {
            QTableWidget *t = ui->stockTable;
            const int row = t->currentRow();
            if (row < 0) {
                QMessageBox::warning(this, tr("Attention"), tr("Sélectionnez une ligne de stock."));
                return;
            }
            const int id = tableCellText(t, row, StColId).toInt();
            if (EtmpStock.supprimer(id)) {
                refreshStockTable();
                QMessageBox::information(this, tr("Succès"), tr("Ligne supprimée."));
            } else {
                QMessageBox::critical(this, tr("Erreur"), tr("Échec de la suppression."));
            }
        });
    }

    if (auto *stockModifyBtn = findButton(this, "stockModifyBtn")) {
        connect(stockModifyBtn, &QPushButton::clicked, this, [this]() {
            const int row = ui->stockTable->currentRow();
            if (row < 0) {
                QMessageBox::warning(this, tr("Attention"), tr("Sélectionnez une ligne de stock."));
                return;
            }
            applyStockRowToForm(row);
            if (auto *stockAddListScrollArea = findScrollArea(this, "stockAddListScrollArea"))
                stockAddListScrollArea->ensureWidgetVisible(ui->stockAddCard);
        });
    }

    setupStockExportMenu();
    initialiserStatistiquesStock();
    connect(ui->stockTabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (!ui->stockTabWidget) {
            return;
        }
        QWidget *tab = ui->stockTabWidget->widget(index);
        if (tab && tab->objectName() == QStringLiteral("stockStatsTabDynamic")) {
            mettreAJourStatistiquesStock();
        }
    });
}

void MainWindow::initialiserStatistiquesStock()
{
    if (!ui->stockTabWidget) {
        return;
    }
    if (ui->stockTabWidget->findChild<QWidget *>(QStringLiteral("stockStatsTabDynamic"))) {
        return;
    }

    auto *statsTab = new QWidget(ui->stockTabWidget);
    statsTab->setObjectName(QStringLiteral("stockStatsTabDynamic"));
    auto *mainLayout = new QVBoxLayout(statsTab);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(14);

    auto *title = new QLabel(tr("Statistiques stock"), statsTab);
    title->setStyleSheet(QStringLiteral("QLabel { color: #0f172a; font-size: 22px; font-weight: 700; }"));
    auto *subtitle = new QLabel(tr("Analyse des quantites, regions et evolution mensuelle"), statsTab);
    subtitle->setStyleSheet(QStringLiteral("QLabel { color: #475569; font-size: 12px; }"));
    mainLayout->addWidget(title);
    mainLayout->addWidget(subtitle);

    auto *cardsRow = new QHBoxLayout();
    cardsRow->setSpacing(12);
    auto createCard = [statsTab](const QString &titleText, const QString &valueObject, const QString &accentColor) {
        auto *card = new QFrame(statsTab);
        card->setStyleSheet(QStringLiteral(
            "QFrame { background: #ffffff; border: 1px solid #dbe4ea; border-left: 5px solid %1; border-radius: 12px; }")
                                .arg(accentColor));
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(14, 12, 14, 12);
        layout->setSpacing(6);
        auto *lblTitle = new QLabel(titleText, card);
        lblTitle->setStyleSheet(QStringLiteral("QLabel { color: #475569; font-size: 12px; }"));
        auto *lblValue = new QLabel(QStringLiteral("0"), card);
        lblValue->setObjectName(valueObject);
        lblValue->setStyleSheet(QStringLiteral("QLabel { color: #2e7d32; font-size: 24px; font-weight: 700; }"));
        layout->addWidget(lblTitle);
        layout->addWidget(lblValue);
        layout->addStretch();
        return card;
    };

    cardsRow->addWidget(createCard(tr("Quantite totale"), QStringLiteral("stockStatsQtyValue"), QStringLiteral("#0f766e")));
    cardsRow->addWidget(createCard(tr("Lignes de stock"), QStringLiteral("stockStatsRowsValue"), QStringLiteral("#1d4ed8")));
    cardsRow->addWidget(createCard(tr("Regions couvertes"), QStringLiteral("stockStatsRegionsValue"), QStringLiteral("#b45309")));
    mainLayout->addLayout(cardsRow);

    auto *chartsRow = new QHBoxLayout();
    chartsRow->setSpacing(12);

    auto *pieView = new QChartView(statsTab);
    pieView->setObjectName(QStringLiteral("stockStatsRegionPieView"));
    pieView->setRenderHint(QPainter::Antialiasing);
    pieView->setMinimumHeight(290);
    pieView->setStyleSheet(QStringLiteral("background: #ffffff; border: 1px solid #dbe4ea; border-radius: 12px;"));

    auto *trendView = new QChartView(statsTab);
    trendView->setObjectName(QStringLiteral("stockStatsTrendView"));
    trendView->setRenderHint(QPainter::Antialiasing);
    trendView->setMinimumHeight(290);
    trendView->setStyleSheet(QStringLiteral("background: #ffffff; border: 1px solid #dbe4ea; border-radius: 12px;"));

    chartsRow->addWidget(pieView, 1);
    chartsRow->addWidget(trendView, 2);
    mainLayout->addLayout(chartsRow);
    mainLayout->addStretch();

    ui->stockTabWidget->addTab(statsTab, tr("Statistiques"));
}

void MainWindow::mettreAJourStatistiquesStock()
{
    if (!ui->stockTabWidget) {
        return;
    }
    QWidget *statsTab = ui->stockTabWidget->findChild<QWidget *>(QStringLiteral("stockStatsTabDynamic"));
    if (!statsTab) {
        return;
    }

    auto *qtyValue = statsTab->findChild<QLabel *>(QStringLiteral("stockStatsQtyValue"));
    auto *rowsValue = statsTab->findChild<QLabel *>(QStringLiteral("stockStatsRowsValue"));
    auto *regionsValue = statsTab->findChild<QLabel *>(QStringLiteral("stockStatsRegionsValue"));
    auto *pieView = statsTab->findChild<QChartView *>(QStringLiteral("stockStatsRegionPieView"));
    auto *trendView = statsTab->findChild<QChartView *>(QStringLiteral("stockStatsTrendView"));
    if (!qtyValue || !rowsValue || !regionsValue || !pieView || !trendView) {
        return;
    }

    double totalQty = 0.0;
    int totalRows = 0;
    int totalRegions = 0;
    QSqlQuery aggregate;
    if (aggregate.exec(QStringLiteral(
            "SELECT NVL(SUM(NVL(QUANTITE,0)), 0), COUNT(*), COUNT(DISTINCT NVL(REGION, '(Sans region)')) "
            "FROM STOCK")) &&
        aggregate.next()) {
        totalQty = aggregate.value(0).toDouble();
        totalRows = aggregate.value(1).toInt();
        totalRegions = aggregate.value(2).toInt();
    }

    qtyValue->setText(QLocale::system().toString(totalQty, 'f', 2));
    rowsValue->setText(QString::number(totalRows));
    regionsValue->setText(QString::number(totalRegions));

    auto *pieSeries = new QPieSeries();
    QSqlQuery regionQuery;
    regionQuery.prepare(QStringLiteral(
        "SELECT REGION_NAME, TOTAL_QTY "
        "FROM ("
        "  SELECT NVL(REGION, '(Sans region)') AS REGION_NAME, NVL(SUM(NVL(QUANTITE,0)), 0) AS TOTAL_QTY "
        "  FROM STOCK "
        "  GROUP BY NVL(REGION, '(Sans region)') "
        "  ORDER BY TOTAL_QTY DESC"
        ") "
        "WHERE ROWNUM <= 6"));
    if (regionQuery.exec()) {
        while (regionQuery.next()) {
            const QString label = regionQuery.value(0).toString();
            const qreal value = regionQuery.value(1).toDouble();
            if (value > 0.0) {
                pieSeries->append(label, value);
            }
        }
    }
    if (pieSeries->slices().isEmpty()) {
        pieSeries->append(tr("Aucune donnee"), 1.0);
    }
    for (QPieSlice *slice : pieSeries->slices()) {
        slice->setLabelVisible(true);
    }
    auto *pieChart = new QChart();
    pieChart->addSeries(pieSeries);
    pieChart->setTitle(tr("Repartition quantite par region"));
    pieChart->setAnimationOptions(QChart::SeriesAnimations);
    pieChart->legend()->setAlignment(Qt::AlignRight);
    pieChart->setBackgroundVisible(false);
    QChart *oldPieChart = pieView->chart();
    pieView->setChart(pieChart);
    if (oldPieChart) {
        delete oldPieChart;
    }

    QMap<QString, double> monthlyQty;
    QSqlQuery monthlyQuery;
    monthlyQuery.prepare(QStringLiteral(
        "SELECT TO_CHAR(TRUNC(DATE_STOCK, 'MM'), 'YYYY-MM') AS MKEY, NVL(SUM(NVL(QUANTITE,0)), 0) "
        "FROM STOCK "
        "WHERE DATE_STOCK >= ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -5) "
        "GROUP BY TRUNC(DATE_STOCK, 'MM') "
        "ORDER BY TRUNC(DATE_STOCK, 'MM')"));
    if (monthlyQuery.exec()) {
        while (monthlyQuery.next()) {
            monthlyQty.insert(monthlyQuery.value(0).toString(), monthlyQuery.value(1).toDouble());
        }
    }

    auto *lineSeries = new QSplineSeries();
    lineSeries->setName(tr("Quantite mensuelle"));
    QPen linePen(QColor("#2e7d32"));
    linePen.setWidth(3);
    lineSeries->setPen(linePen);
    lineSeries->setPointsVisible(true);

    QVector<qint64> xPoints;
    double maxY = 0.0;
    QDate cursor = QDate::currentDate().addMonths(-5);
    for (int i = 0; i < 6; ++i) {
        const QString key = cursor.toString(QStringLiteral("yyyy-MM"));
        const double value = monthlyQty.value(key, 0.0);
        const qint64 x = QDateTime(QDate(cursor.year(), cursor.month(), 1), QTime(0, 0)).toMSecsSinceEpoch();
        xPoints.push_back(x);
        lineSeries->append(static_cast<qreal>(x), value);
        maxY = qMax(maxY, value);
        cursor = cursor.addMonths(1);
    }

    auto *lineChart = new QChart();
    lineChart->addSeries(lineSeries);
    lineChart->setTitle(tr("Evolution des quantites par mois"));
    lineChart->setAnimationOptions(QChart::SeriesAnimations);
    lineChart->legend()->hide();
    lineChart->setBackgroundVisible(false);

    auto *axisX = new QDateTimeAxis();
    axisX->setFormat(QStringLiteral("MMM yy"));
    axisX->setTickCount(6);
    if (!xPoints.isEmpty()) {
        axisX->setRange(QDateTime::fromMSecsSinceEpoch(xPoints.first()),
                        QDateTime::fromMSecsSinceEpoch(xPoints.last()));
    }

    auto *axisY = new QValueAxis();
    axisY->setLabelFormat(QStringLiteral("%.0f"));
    axisY->setRange(0.0, maxY > 0.0 ? maxY * 1.2 : 10.0);
    axisY->setTickCount(5);

    lineChart->addAxis(axisX, Qt::AlignBottom);
    lineChart->addAxis(axisY, Qt::AlignLeft);
    lineSeries->attachAxis(axisX);
    lineSeries->attachAxis(axisY);

    QChart *oldLineChart = trendView->chart();
    trendView->setChart(lineChart);
    if (oldLineChart) {
        delete oldLineChart;
    }
}

void MainWindow::populateStockSortCombo()
{
    QComboBox *c = findCombo(this, "stockSortCombo");
    if (!c)
        return;
    c->clear();
    const auto add = [&](const QString &label, const QString &data) { c->addItem(label, data); };
    add(tr("ID — croissant"), QStringLiteral("id|asc"));
    add(tr("ID — décroissant"), QStringLiteral("id|desc"));
    add(tr("Type — A→Z"), QStringLiteral("type|asc"));
    add(tr("Type — Z→A"), QStringLiteral("type|desc"));
    add(tr("Région — A→Z"), QStringLiteral("region|asc"));
    add(tr("Région — Z→A"), QStringLiteral("region|desc"));
    add(tr("Quantité — croissant"), QStringLiteral("quantite|asc"));
    add(tr("Quantité — décroissant"), QStringLiteral("quantite|desc"));
    add(tr("Date — croissant"), QStringLiteral("date|asc"));
    add(tr("Date — décroissant"), QStringLiteral("date|desc"));
    c->setCurrentIndex(0);
}

bool MainWindow::executeStockQuery(QSqlQuery &q)
{
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.isOpen()) {
        qWarning() << "Base de données non ouverte";
        return false;
    }

    QString sql = QStringLiteral(
        "SELECT ID_STOCK, TYPE_PRODUIT, REGION, QUANTITE, DATE_STOCK "
        "FROM STOCK WHERE 1=1");

    QString term;
    if (auto *stockSearchInput = findLineEdit(this, "stockSearchInput"))
        term = stockSearchInput->text().trimmed();
    QVariantMap binds;
    if (!term.isEmpty()) {
        sql += QStringLiteral(
            " AND ("
            "UPPER(TYPE_PRODUIT) LIKE UPPER(:like) OR "
            "UPPER(REGION) LIKE UPPER(:like) OR "
            "TO_CHAR(QUANTITE) LIKE :like_plain OR "
            "TO_CHAR(ID_STOCK) LIKE :like_plain"
            ")");
        const QString pat = QStringLiteral("%") + term + QStringLiteral("%");
        binds.insert(QStringLiteral(":like"), pat);
        binds.insert(QStringLiteral(":like_plain"), pat);
    }

    QString orderSpec;
    if (auto *stockSortCombo = findCombo(this, "stockSortCombo"))
        orderSpec = stockSortCombo->currentData().toString();
    if (orderSpec.isEmpty())
        orderSpec = QStringLiteral("id|asc");
    const QStringList parts = orderSpec.split(QLatin1Char('|'));
    const QString sortKey = parts.value(0, QStringLiteral("id"));
    const bool desc = (parts.value(1, QStringLiteral("asc")) == QLatin1String("desc"));
    sql += QStringLiteral(" ORDER BY ") + stockSqlOrderExpr(sortKey)
        + (desc ? QStringLiteral(" DESC") : QStringLiteral(" ASC"));

    QSqlQuery query(db);
    query.prepare(sql);
    for (auto it = binds.constBegin(); it != binds.constEnd(); ++it)
        query.bindValue(it.key(), it.value());

    if (!query.exec()) {
        qWarning() << "executeStockQuery:" << query.lastError().text();
        return false;
    }
    q = std::move(query);
    return true;
}

void MainWindow::scheduleStockTableRefresh()
{
    m_stockSearchDebounce->start();
}

void MainWindow::loadStockTable()
{
    refreshStockTable();
}

void MainWindow::setStockModifyMode(bool active, int stockId)
{
    m_stockModifyMode = active;
    m_modifyStockId = stockId;
    ui->stockFormTitle->setText(active ? tr("Modifier le stock") : tr("information du stock"));
    ui->saveStockBtn->setText(active ? tr("Mettre à jour") : tr("Enregistrer"));
}

void MainWindow::clearStockForm()
{
    setStockModifyMode(false, -1);
    ui->emailEdit_12->clear();
    ui->emailEdit_11->clear();
    ui->marqueEdit_7->clear();
    ui->startDateEdit_7->setDate(QDate::currentDate());
}

void MainWindow::applyStockRowToForm(int row)
{
    QTableWidget *t = ui->stockTable;
    const int id = tableCellText(t, row, StColId).toInt();
    ui->emailEdit_12->setText(tableCellText(t, row, StColType));
    ui->emailEdit_11->setText(tableCellText(t, row, StColRegion));
    ui->marqueEdit_7->setText(tableCellText(t, row, StColQty));

    QDate date = QDate::fromString(tableCellText(t, row, StColDate), QStringLiteral("dd/MM/yyyy"));
    ui->startDateEdit_7->setDate(date.isValid() ? date : QDate::currentDate());

    setStockModifyMode(true, id);
}

bool MainWindow::validateStockInputs(QString *err)
{
    if (ui->emailEdit_12->text().trimmed().isEmpty()) {
        if (err)
            *err = tr("Le type / produit est obligatoire.");
        return false;
    }
    bool ok = false;
    QLocale::system().toDouble(ui->marqueEdit_7->text().trimmed(), &ok);
    if (!ok) {
        if (err)
            *err = tr("La quantité doit être un nombre valide.");
        return false;
    }
    return true;
}

void MainWindow::updateStockTableHeightForUnifiedScroll()
{
    QTableWidget *t = ui->stockTable;
    if (!t)
        return;
    t->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const int rows = t->rowCount();
    const int headerH = qMax(t->horizontalHeader()->height(), t->horizontalHeader()->sizeHint().height());
    int bodyH = 0;
    if (rows <= 0)
        bodyH = t->verticalHeader()->defaultSectionSize();
    else {
        for (int r = 0; r < rows; ++r)
            bodyH += t->rowHeight(r);
    }
    const int h = headerH + bodyH + 8;
    t->setFixedHeight(qMax(h, 220));
}

void MainWindow::refreshStockTable()
{
    QSqlQuery q;
    if (!executeStockQuery(q))
        return;

    QTableWidget *t = ui->stockTable;
    t->setRowCount(0);
    t->setUpdatesEnabled(false);

    int row = 0;
    while (q.next()) {
        t->insertRow(row);
        t->setItem(row, StColId, new QTableWidgetItem(q.value(QStringLiteral("ID_STOCK")).toString()));
        t->setItem(row, StColType, new QTableWidgetItem(q.value(QStringLiteral("TYPE_PRODUIT")).toString()));
        t->setItem(row, StColRegion, new QTableWidgetItem(q.value(QStringLiteral("REGION")).toString()));
        const double qty = q.value(QStringLiteral("QUANTITE")).toDouble();
        t->setItem(row, StColQty, new QTableWidgetItem(QLocale::system().toString(qty, 'f', 2)));
        const QDate d = q.value(QStringLiteral("DATE_STOCK")).toDate();
        t->setItem(row, StColDate, new QTableWidgetItem(d.toString(QStringLiteral("dd/MM/yyyy"))));
        row++;
    }

    t->resizeColumnsToContents();
    t->setUpdatesEnabled(true);
    t->viewport()->update();
    QTimer::singleShot(0, this, &MainWindow::updateStockTableHeightForUnifiedScroll);
    mettreAJourStatistiquesStock();
}

void MainWindow::setupStockExportMenu()
{
    auto *menu = new QMenu(this);
    menu->addAction(tr("CSV (Excel)…"), this, &MainWindow::exportStockToCsv);
    menu->addAction(tr("PDF…"), this, &MainWindow::exportStockToPdf);
    menu->addAction(tr("HTML…"), this, &MainWindow::exportStockToHtml);
    if (auto *stockExportBtn = findButton(this, "stockExportBtn"))
        stockExportBtn->setMenu(menu);
}

void MainWindow::exportStockToCsv()
{
    QSqlQuery q;
    if (!executeStockQuery(q)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible de lire le stock."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export CSV"), QString(),
                                                      tr("CSV (*.csv);;All files (*)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible d'écrire le fichier."));
        return;
    }

    QTextStream out(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec(QTextCodec::codecForName("UTF-8"));
#endif
    out << "\xEF\xBB\xBF";
    out << "ID_STOCK;TYPE_PRODUIT;REGION;QUANTITE;DATE_STOCK\n";

    while (q.next()) {
        QStringList cols;
        cols << q.value(QStringLiteral("ID_STOCK")).toString()
             << q.value(QStringLiteral("TYPE_PRODUIT")).toString()
             << q.value(QStringLiteral("REGION")).toString()
             << q.value(QStringLiteral("QUANTITE")).toString()
             << q.value(QStringLiteral("DATE_STOCK")).toDate().toString(Qt::ISODate);
        out << cols.join(QLatin1Char(';')) << QLatin1Char('\n');
    }
    f.close();
    QMessageBox::information(this, tr("Export"), tr("Fichier CSV enregistré."));
}

void MainWindow::exportStockToPdf()
{
    QSqlQuery q;
    if (!executeStockQuery(q)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible de lire le stock."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export PDF"), QString(),
                                                      tr("PDF (*.pdf);;All files (*)"));
    if (path.isEmpty())
        return;

    QString html = QStringLiteral("<html><head><meta charset=\"utf-8\"/></head><body>");
    html += QStringLiteral("<h2>%1</h2><table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">")
                 .arg(htmlEscape(tr("Liste du stock")));
    html += QStringLiteral("<tr><th>%1</th><th>%2</th><th>%3</th><th>%4</th><th>%5</th></tr>")
                .arg(htmlEscape(tr("ID")),
                     htmlEscape(tr("Type")),
                     htmlEscape(tr("Région")),
                     htmlEscape(tr("Quantité")),
                     htmlEscape(tr("Date")));

    while (q.next()) {
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
                    .arg(htmlEscape(q.value(QStringLiteral("ID_STOCK")).toString()),
                         htmlEscape(q.value(QStringLiteral("TYPE_PRODUIT")).toString()),
                         htmlEscape(q.value(QStringLiteral("REGION")).toString()),
                         htmlEscape(q.value(QStringLiteral("QUANTITE")).toString()),
                         htmlEscape(q.value(QStringLiteral("DATE_STOCK")).toDate().toString(Qt::ISODate)));
    }
    html += QStringLiteral("</table></body></html>");

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);

    QTextDocument doc;
    doc.setHtml(html);
    doc.print(&printer);

    QMessageBox::information(this, tr("Export"), tr("Fichier PDF enregistré."));
}

void MainWindow::exportStockToHtml()
{
    QSqlQuery q;
    if (!executeStockQuery(q)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible de lire le stock."));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export HTML"), QString(),
                                                      tr("HTML (*.html);;All files (*)"));
    if (path.isEmpty())
        return;

    QString html = QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"/><title>%1</title></head><body>")
                       .arg(htmlEscape(tr("Stock")));
    html += QStringLiteral("<h2>%1</h2><table border=\"1\" cellspacing=\"0\" cellpadding=\"4\">")
                .arg(htmlEscape(tr("Liste du stock")));
    html += QStringLiteral("<tr><th>%1</th><th>%2</th><th>%3</th><th>%4</th><th>%5</th></tr>")
                .arg(htmlEscape(tr("ID")),
                     htmlEscape(tr("Type")),
                     htmlEscape(tr("Région")),
                     htmlEscape(tr("Quantité")),
                     htmlEscape(tr("Date")));

    while (q.next()) {
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
                    .arg(htmlEscape(q.value(QStringLiteral("ID_STOCK")).toString()),
                         htmlEscape(q.value(QStringLiteral("TYPE_PRODUIT")).toString()),
                         htmlEscape(q.value(QStringLiteral("REGION")).toString()),
                         htmlEscape(q.value(QStringLiteral("QUANTITE")).toString()),
                         htmlEscape(q.value(QStringLiteral("DATE_STOCK")).toDate().toString(Qt::ISODate)));
    }
    html += QStringLiteral("</table></body></html>");

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export"), tr("Impossible d'écrire le fichier."));
        return;
    }
    QTextStream out(&f);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec(QTextCodec::codecForName("UTF-8"));
#endif
    out << html;
    f.close();
    QMessageBox::information(this, tr("Export"), tr("Fichier HTML enregistré."));
}
