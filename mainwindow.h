#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "employee.h"
#include "equipement.h"
#include "stock.h"

#include <QDate>
#include <QMainWindow>
#include <QSqlQueryModel>
#include <QTableWidgetItem>
#include <QLabel>
#include <QMenu>
#include <QVector>
#include <QRect>

#include <QtCharts/QChartView>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QtCharts/QChart>

#include <QSqlQuery>
#include <QString>

class EmployeeStatisticsWidget;
class EmployeeAiClient;
class FinanceAiClient;
class AiChatWidget;
class FournisseurChatWidget;

class QDoubleValidator;
class QLineEdit;
class QRegularExpressionValidator;
class QTimer;
class QTableWidget;
class QPushButton;
class QListWidget;
class ArduinoHandler;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private:
    void setupClientsFinanceModule();

    void loadEmployees();
    void setupEmployeeSearchBar();
    void setupEmployeeExportMenu();
    void populateEmployeeSortCombo();
    bool executeEmployeeQuery(QSqlQuery &query);
    void refreshEmployeeTable();
    void updateEmployeeTableHeightForUnifiedScroll();
    void scheduleEmployeeTableRefresh();

    void setupEquipmentModule();
    void initialiserStatistiquesEquipement();
    void mettreAJourStatistiquesEquipement();
    void loadEquipmentTable();
    void refreshEquipmentTable();
    void scheduleEquipmentTableRefresh();
    void populateEquipmentSortCombo();
    bool executeEquipmentQuery(QSqlQuery &query);
    void applyEquipmentRowToForm(int row);
    void clearEquipmentForm();
    void setEquipmentModifyMode(bool active, int equipmentId = -1);
    bool validateEquipmentInputs(QString *errMsg = nullptr);
    void updateEquipmentTableHeightForUnifiedScroll();
    void setupEquipmentExportMenu();
    void exportEquipmentToCsv();
    void exportEquipmentToPdf();
    void exportEquipmentToHtml();
    void refreshEmployeeStatistics();

    void setupStockModule();
    void initialiserStatistiquesStock();
    void mettreAJourStatistiquesStock();
    void loadStockTable();
    void refreshStockTable();
    void scheduleStockTableRefresh();
    void populateStockSortCombo();
    bool executeStockQuery(QSqlQuery &query);
    void applyStockRowToForm(int row);
    void clearStockForm();
    void setStockModifyMode(bool active, int stockId = -1);
    bool validateStockInputs(QString *errMsg = nullptr);
    void updateStockTableHeightForUnifiedScroll();

    void setupStockExportMenu();
    void exportStockToCsv();
    void exportStockToPdf();
    void exportStockToHtml();
    void setupAssistantTab();
    void appendAssistantMessage(const QString &author, const QString &message);
    QString buildAssistantContext() const;
    QString buildAssistantFallback(const QString &question) const;
    void setupFinanceAssistantTab();
    void appendFinanceAssistantMessage(QListWidget *chatList, const QString &author, const QString &message) const;
    QString buildFinanceAssistantContext() const;
    QString buildFinanceAssistantFallback(const QString &question) const;

    void exportEmployeesToCsv();
    void exportEmployeesToPdf();
    void exportEmployeesToHtml();

    void setupEmployeeIdRow();
    void setupPrenomField();
    void setupInputValidation();
    void connectLiveValidation();
    void setFieldError(QLineEdit *edit, bool error);
    void clearAllFieldErrors();
    bool validateInputs(QString *firstErrorMessage = nullptr);

    void setModifyMode(bool active, int employeeId = -1);
    void clearEmployeeForm();
    void applyTableRowToForm(int row);

    // --- Clients / fournisseurs / factures (integrated from projetqt) ---
    void afficherClients();
    void viderChampsClient();
    void activerBoutonsClient(bool etat);

    void configurerControleSaisieFournisseur();
    bool validerSaisieFournisseur(QString &nom,
                                  QString &region,
                                  QString &email,
                                  QString &type,
                                  QString &status,
                                  QString &telephone,
                                  QString &dateAjout);
    void chargerFournisseurs();
    int prochainIdFournisseur();
    void reinitialiserEditionFournisseur();
    int trouverLigneFournisseurParId(int id) const;
    void actualiserLibelleTriFournisseur();
    void filtrerFournisseurs(const QString &filtre);
    void exporterFournisseursPdf();

    void configurerControleSaisieFacture();
    void configurerTriAvanceFacture();
    void configurerTriAvanceFournisseur();
    void appliquerTriFacture(const QString &champSql, bool asc, const QString &libelle);
    void appliquerTriFournisseur(int colonne, bool asc, const QString &libelle);
    QString ordreSqlTriFacture() const;
    void actualiserLibelleTri();
    void chargerFactures(const QString &filtre = QString());
    void chargerFactureParId(int idFacture);
    void viderFormulaireFacture();
    bool validerSaisieFacture(QString &matriculeFiscal,
                              QString &nomEntreprise,
                              QString &raisonSociale,
                              QString &typeFacture,
                              double &prixUnitaire,
                              int &quantite,
                              QString &description);
    void exporterFacturesPdf();

    void configurerTriAvanceClient();
    void appliquerTriClient(int colonne, bool asc, const QString &libelle);
    void actualiserLibelleTriClient();
    void filtrerClients(const QString &filtre);
    void setupCommandesClientsSection();
    void chargerCommandesClients();
    void filtrerCommandesClients(const QString &filtre);
    void appliquerTriCommandesClients(int colonne, bool asc, const QString &libelle);
    void actualiserLibelleTriCommandesClients();

    void resetStyles();
    bool validerFormatEmail(const QString &email);
    bool validerFormatTelephone(const QString &telephone);
    bool validerFormatNomPrenom(const QString &texte);
    bool validerFormatCin(const QString &cin);
    QString nettoyerTelephone(const QString &telephone);
    bool emailExisteDeja(const QString &email, int idClientExclu = -1);
    bool telephoneExisteDeja(const QString &telephone, int idClientExclu = -1);
    bool cinExisteDeja(const QString &cin, int idClientExclu = -1);
    bool validerSaisieClient();

    void exporterRapportPDF(int hommes, int femmes, int total,
                            double pourcentageHommes, double pourcentageFemmes);
    QString genererRapportHTML(int hommes, int femmes, int total,
                               double pourcentageHommes, double pourcentageFemmes);

    void initialiserStatistiques();
    void calculerStatistiquesGenre();
    QWidget *creerCarteKPI(const QString &titre, int valeur, double pourcentage,
                             const QString &couleur, const QString &icone);
    void creerDiagrammeCirculaire(int hommes, int femmes, int total);
    void calculerStatistiquesJournalieres();
    void afficherCourbeAcquisition(const QVector<QDate> &dates,
                                   const QVector<int> &nombres,
                                   const QVector<double> &prevision);
    void afficherCarteTendance(int totalPrevus, double pente);
    QString formaterNombre(int nombre);
    void setupLoginFlow();
    void setupAccessControlBridge();
    bool isAccessCodeAllowed(const QString &code) const;
    void setAuthenticated(bool authenticated);
    bool tryAuthenticateUser();
    void initialiserStatistiquesFinance();
    void mettreAJourStatistiquesFinance();
    void initialiserStatistiquesFournisseur();
    void mettreAJourStatistiquesFournisseur();

private slots:
    void on_employerBtn_clicked();
    void on_profileBtn_clicked();
    void on_clientsBtn_clicked();
    void on_equipmentBtn_clicked();
    void on_financeBtn_clicked();
    void on_fournisseursBtn_clicked();
    void on_stockBtn_clicked();
    void on_AproposBtn_clicked();

    void on_saveEmployeeBtn_2_clicked();
    void on_cancelBtn_2_clicked();
    void handleClientModifyRequested();
    void on_deleteBtn_2_clicked();
    void on_searchBtn_2_clicked();
    void on_sortBtn_2_clicked();
    void on_exportBtn_2_clicked();
    void on_searchInput_2_textChanged(const QString &text);
    void on_employeeTable_2_itemClicked(QTableWidgetItem *item);

    void mettreAJourStatistiques();

private:
    Employee Etmp;
    Equipement EtmpEquip;
    Stock EtmpStock;
    Ui::MainWindow *ui;

    bool m_isModifyMode = false;
    int m_modifyEmployeeId = -1;
    QLineEdit *m_employeeIdEdit = nullptr;
    QLabel *m_employeeIdLabel = nullptr;
    QLineEdit *m_prenomEdit = nullptr;
    QLabel *m_prenomLabel = nullptr;

    QString m_baseLineEditStyle;
    QDoubleValidator *m_salaryValidator = nullptr;
    QRegularExpressionValidator *m_nomValidator = nullptr;
    QRegularExpressionValidator *m_prenomValidator = nullptr;
    QRegularExpressionValidator *m_emailValidator = nullptr;
    QRegularExpressionValidator *m_phoneValidator = nullptr;

    QTimer *m_employeeSearchDebounce = nullptr;
    QTimer *m_equipmentSearchDebounce = nullptr;
    QTimer *m_stockSearchDebounce = nullptr;

    bool m_equipmentModifyMode = false;
    int m_modifyEquipmentId = -1;
    bool m_stockModifyMode = false;
    int m_modifyStockId = -1;

    EmployeeStatisticsWidget *m_employeeStatisticsWidget = nullptr;
    EmployeeAiClient *m_employeeAiClient = nullptr;
    FinanceAiClient *m_financeAiClient = nullptr;
    QString m_lastAssistantQuestion;
    QString m_lastFinanceAssistantQuestion;
    bool m_isAuthenticated = false;
    QString m_loggedInEmail;
    QLabel *m_loginErrorLabel = nullptr;
    QRect m_mainStackedDefaultGeometry;
    QRect m_sidebarDefaultGeometry;

    int idClientSelectionne = -1;
    QString ordreTriActuel = QStringLiteral("ASC");
    QString colonneTriActuelle = QStringLiteral("id");

    QMenu *m_menuTriFournisseur = nullptr;
    int m_colTriFournisseur = 0;
    bool m_triFournisseurAsc = true;
    QString m_libelleTriFournisseur = QStringLiteral("ID ASC");
    int m_idFournisseurEditionLocal = -1;

    QMenu *m_menuTriFacture = nullptr;
    QString m_champTriFacture = QStringLiteral("ID_FACTURE");
    QString m_libelleTriFacture = QStringLiteral("ID ASC");
    bool m_triFactureAsc = true;
    int m_idFactureEnEdition = -1;
    int m_idClientEnEdition = -1;
    int m_idFournisseurEnEdition = -1;

    QMenu *m_menuTriClient = nullptr;
    int m_colTriClient = 0;
    bool m_triClientAsc = true;
    QString m_libelleTriClient = QStringLiteral("ID ASC");
    QTableWidget *m_commandesClientsTable = nullptr;
    QLineEdit *m_searchCommandesClients = nullptr;
    QPushButton *m_sortCommandesClientsBtn = nullptr;
    QMenu *m_menuTriCommandesClients = nullptr;
    int m_colTriCommandesClients = 8;
    bool m_triCommandesClientsAsc = false;
    QString m_libelleTriCommandesClients = QStringLiteral("Date commande DESC");

    QVector<QDate> m_datesHistorique;
    QVector<int> m_nombresHistorique;

    AiChatWidget *m_clientAiChatWidget = nullptr;
    FournisseurChatWidget *m_fournisseurChatWidget = nullptr;
    ArduinoHandler *m_accessBridgeHandler = nullptr;
    qulonglong m_lastPumpCycleId = 0;
};

#endif // MAINWINDOW_H
