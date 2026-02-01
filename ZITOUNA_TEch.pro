QT       += core gui sql printsupport charts concurrent network svg serialport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
CONFIG += console

# Intégration : mainwindow.cpp = module employés uniquement.
# mainwindow_clientsfinance.cpp = clients / fournisseurs / factures / stats.
# Ne pas ajouter aussi le mainwindow.cpp du dossier pojetqt (sinon doublons au lien).
# Ouvrir uniquement ce .pro depuis integration/employee.

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    AiChatWidget.cpp \
    arduinohandler.cpp \
    FournisseurChatWidget.cpp \
    LlmClient.cpp \
    LlmClientFournisseurs.cpp \
    client.cpp \
    connection.cpp \
    employeeaiclient.cpp \
    employeeassistantwidget.cpp \
    equipement.cpp \
    financeaiclient.cpp \
    envloader.cpp \
    leavemanager.cpp \
    schedulingengine.cpp \
    planningwidget.cpp \
    qrcodegen.cpp \
    stock.cpp \
    employee.cpp \
    employeemanager.cpp \
    employeestatisticswidget.cpp \
    fingerprintdialog.cpp \
    fingerprintlogindialog.cpp \
    pindialog.cpp \
    recruitmentwidget.cpp \
    facture.cpp \
    fournisseur.cpp \
    main.cpp \
    mainwindow.cpp \
    mainwindow_clientsfinance.cpp

HEADERS += \
    AiChatWidget.h \
    AiChatMessage.h \
    arduinohandler.h \
    DatabaseContext.h \
    FournisseurChatWidget.h \
    LlmClient.h \
    LlmClientFournisseurs.h \
    client.h \
    connection.h \
    employeeaiclient.h \
    employeeassistantwidget.h \
    equipement.h \
    financeaiclient.h \
    envloader.h \
    leavemanager.h \
    schedulingengine.h \
    planningwidget.h \
    qrcodegen.hpp \
    stock.h \
    employee.h \
    employeemanager.h \
    employeestatisticswidget.h \
    fingerprintdialog.h \
    fingerprintlogindialog.h \
    pindialog.h \
    recruitmentwidget.h \
    facture.h \
    fournisseur.h \
    mainwindow.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources.qrc
