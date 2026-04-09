#include "mainwindow.h"
#include "AiChatWidget.h"
#include "FournisseurChatWidget.h"
#include "client.h"
#include "facture.h"
#include "financeaiclient.h"
#include "fournisseur.h"
#include "qrcodegen.hpp"
#include <ui_mainwindow.h>

#include <QAbstractItemView>
#include <QAction>
#include <QButtonGroup>
#include <QDate>
#include <QDateTime>
#include <QDesktopServices>
#include <QTime>
#include <QMap>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QLocale>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QHeaderView>
#include <QIntValidator>
#include <QChart>
#include <QChartView>
#include <QDateTimeAxis>
#include <QLineSeries>
#include <QPieSeries>
#include <QPieSlice>
#include <QScatterSeries>
#include <QSplineSeries>
#include <QValueAxis>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScatterSeries>
#include <QSet>
#include <QSqlError>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QTextEdit>
#include <QValueAxis>
#include <QtPrintSupport/QPrinter>
#include <QVBoxLayout>
#include <QVector>
#include <QSqlQueryModel>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QComboBox>
#include <QStringList>
#include <QTabWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QBuffer>
#include <QIcon>
#include <QImage>
#include <QMouseEvent>
#include <QPixmap>
#include <QProgressBar>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QPageSize>
#include <QPageLayout>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QNetworkInterface>
#include <QFutureWatcher>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <memory>

namespace {
constexpr int kFactureSignatureColumn = 9;
constexpr int kFactureQrColumn = 10;
QString ensureInvoicePagesDir();

std::unique_ptr<QTcpServer> g_invoiceHttpServer;
QString g_invoiceHttpBaseUrl;

QString sanitizeBaseUrl(QString baseUrl)
{
    baseUrl = baseUrl.trimmed();
    if (baseUrl.isEmpty()) {
        return baseUrl;
    }
    if (!baseUrl.startsWith(QStringLiteral("http://"))
        && !baseUrl.startsWith(QStringLiteral("https://"))) {
        baseUrl.prepend(QStringLiteral("https://"));
    }
    while (baseUrl.endsWith('/')) {
        baseUrl.chop(1);
    }
    return baseUrl;
}

QString configuredNetlifyBaseUrl()
{
    QString url = qEnvironmentVariable("ZITOUNATECH_NETLIFY_SITE_URL").trimmed();
    if (url.isEmpty()) {
        url = qEnvironmentVariable("NETLIFY_SITE_URL").trimmed();
    }
    return sanitizeBaseUrl(url);
}

QString configuredNetlifyInvoicesDir()
{
    QString publishDir = qEnvironmentVariable("ZITOUNATECH_NETLIFY_PUBLISH_DIR").trimmed();
    if (publishDir.isEmpty()) {
        // Default local folder to make Netlify deployment straightforward.
        publishDir = QDir::current().filePath(QStringLiteral("netlify_publish"));
    }
    QDir dir(publishDir);
    dir.mkpath(QStringLiteral("invoices"));
    return dir.filePath(QStringLiteral("invoices"));
}

bool isPrivateLanIpv4(const QString &ip)
{
    if (ip.startsWith(QStringLiteral("192.168.")) || ip.startsWith(QStringLiteral("10."))) {
        return true;
    }
    if (!ip.startsWith(QStringLiteral("172."))) {
        return false;
    }
    const QStringList parts = ip.split('.');
    if (parts.size() < 2) {
        return false;
    }
    bool ok = false;
    const int second = parts.at(1).toInt(&ok);
    return ok && second >= 16 && second <= 31;
}

QString pickLocalIpv4()
{
    const QString forcedIp = qEnvironmentVariable("ZITOUNATECH_QR_HOST_IP").trimmed();
    if (!forcedIp.isEmpty()) {
        QHostAddress host;
        if (host.setAddress(forcedIp) && host.protocol() == QAbstractSocket::IPv4Protocol && !host.isLoopback()) {
            return forcedIp;
        }
    }

    // Preferred: route-resolved local IP (avoids VirtualBox/VMware host-only adapters).
    QUdpSocket probe;
    probe.connectToHost(QHostAddress(QStringLiteral("1.1.1.1")), 80);
    if (probe.waitForConnected(80)) {
        const QString routedIp = probe.localAddress().toString();
        if (isPrivateLanIpv4(routedIp) && !routedIp.startsWith(QStringLiteral("192.168.56."))) {
            return routedIp;
        }
    }

    const QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        const auto flags = iface.flags();
        if (!(flags.testFlag(QNetworkInterface::IsUp) && flags.testFlag(QNetworkInterface::IsRunning))
            || flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const QString ifaceName = (iface.humanReadableName() + QStringLiteral(" ") + iface.name()).toLower();
        if (ifaceName.contains(QStringLiteral("virtual"))
            || ifaceName.contains(QStringLiteral("vbox"))
            || ifaceName.contains(QStringLiteral("vmware"))
            || ifaceName.contains(QStringLiteral("hyper-v"))
            || ifaceName.contains(QStringLiteral("docker"))
            || ifaceName.contains(QStringLiteral("wsl"))
            || ifaceName.contains(QStringLiteral("tailscale"))
            || ifaceName.contains(QStringLiteral("hamachi"))) {
            continue;
        }

        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            const QHostAddress ipAddr = entry.ip();
            if (ipAddr.protocol() != QAbstractSocket::IPv4Protocol || ipAddr.isLoopback()) {
                continue;
            }
            const QString ip = ipAddr.toString();
            if (ip.startsWith(QStringLiteral("169.254.")) || ip.startsWith(QStringLiteral("192.168.56."))) {
                continue;
            }
            if (isPrivateLanIpv4(ip)) {
                return ip;
            }
        }
    }

    const QList<QHostAddress> addrs = QNetworkInterface::allAddresses();
    for (const QHostAddress &addr : addrs) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback()) {
            const QString ip = addr.toString();
            if (!ip.startsWith(QStringLiteral("192.168.56."))) {
                return ip;
            }
        }
    }
    return QStringLiteral("127.0.0.1");
}

QByteArray httpHeader(int statusCode, const QByteArray &statusText, const QByteArray &contentType, int contentLength)
{
    QByteArray h;
    h += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n";
    h += "Content-Type: " + contentType + "\r\n";
    h += "Content-Length: " + QByteArray::number(contentLength) + "\r\n";
    h += "Connection: close\r\n";
    h += "\r\n";
    return h;
}

bool startInvoiceHttpServerIfNeeded()
{
    if (g_invoiceHttpServer && g_invoiceHttpServer->isListening()) {
        return true;
    }
    g_invoiceHttpServer = std::make_unique<QTcpServer>();

    QObject::connect(g_invoiceHttpServer.get(), &QTcpServer::newConnection, []() {
        while (g_invoiceHttpServer && g_invoiceHttpServer->hasPendingConnections()) {
            QTcpSocket *socket = g_invoiceHttpServer->nextPendingConnection();
            if (!socket) {
                continue;
            }
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket]() {
                const QByteArray req = socket->readAll();
                const int lineEnd = req.indexOf("\r\n");
                if (lineEnd <= 0) {
                    return;
                }
                const QList<QByteArray> parts = req.left(lineEnd).split(' ');
                if (parts.size() < 2) {
                    socket->write(httpHeader(400, "Bad Request", "text/plain; charset=utf-8", 11));
                    socket->write("Bad request");
                    socket->disconnectFromHost();
                    return;
                }

                const QString path = QString::fromUtf8(parts.at(1));
                if (path == QStringLiteral("/")) {
                    const QByteArray body = QByteArrayLiteral("<html><body><h3>ZITOUNATECH Invoice Server</h3></body></html>");
                    socket->write(httpHeader(200, "OK", "text/html; charset=utf-8", body.size()));
                    socket->write(body);
                    socket->disconnectFromHost();
                    return;
                }

                if (!path.startsWith(QStringLiteral("/invoices/"))) {
                    const QByteArray body = QByteArrayLiteral("Not found");
                    socket->write(httpHeader(404, "Not Found", "text/plain; charset=utf-8", body.size()));
                    socket->write(body);
                    socket->disconnectFromHost();
                    return;
                }

                const QString encodedName = path.mid(QStringLiteral("/invoices/").size());
                const QString fileName = QUrl::fromPercentEncoding(encodedName.toUtf8());
                if (fileName.contains(QStringLiteral("..")) || fileName.contains('/') || fileName.contains('\\')) {
                    const QByteArray body = QByteArrayLiteral("Forbidden");
                    socket->write(httpHeader(403, "Forbidden", "text/plain; charset=utf-8", body.size()));
                    socket->write(body);
                    socket->disconnectFromHost();
                    return;
                }

                QFile file(QDir(ensureInvoicePagesDir()).filePath(fileName));
                if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
                    const QByteArray body = QByteArrayLiteral("Invoice not found");
                    socket->write(httpHeader(404, "Not Found", "text/plain; charset=utf-8", body.size()));
                    socket->write(body);
                    socket->disconnectFromHost();
                    return;
                }
                const QByteArray body = file.readAll();
                socket->write(httpHeader(200, "OK", "text/html; charset=utf-8", body.size()));
                socket->write(body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    const int startPort = 8099;
    for (int p = startPort; p < startPort + 30; ++p) {
        if (g_invoiceHttpServer->listen(QHostAddress::AnyIPv4, static_cast<quint16>(p))) {
            const QString hostIp = pickLocalIpv4();
            g_invoiceHttpBaseUrl = QStringLiteral("http://%1:%2").arg(hostIp).arg(p);
            return true;
        }
    }
    return false;
}

QString formatCurrencyTnd(double amount)
{
    QLocale locale(QLocale::French, QLocale::Tunisia);
    return locale.toString(amount, 'f', 2) + QStringLiteral(" TND");
}

QString formatCompactCurrencyTnd(double amount)
{
    if (amount >= 1000000.0) {
        return QString::number(amount / 1000000.0, 'f', 2) + QStringLiteral(" M TND");
    }
    if (amount >= 1000.0) {
        return QString::number(amount / 1000.0, 'f', 1) + QStringLiteral(" k TND");
    }
    return QString::number(amount, 'f', 0) + QStringLiteral(" TND");
}

QString trendText(double currentValue, double previousValue)
{
    if (qFuzzyCompare(previousValue + 1.0, 1.0)) {
        if (qFuzzyCompare(currentValue + 1.0, 1.0)) {
            return QStringLiteral("Stable vs mois precedent");
        }
        return QStringLiteral("+100.0% vs mois precedent");
    }
    const double variation = ((currentValue - previousValue) / previousValue) * 100.0;
    const QString sign = variation >= 0.0 ? QStringLiteral("+") : QString();
    return sign + QString::number(variation, 'f', 1) + QStringLiteral("% vs mois precedent");
}

QString trendColor(double currentValue, double previousValue)
{
    if (qFuzzyCompare(previousValue + 1.0, 1.0) && qFuzzyCompare(currentValue + 1.0, 1.0)) {
        return QStringLiteral("#64748b");
    }
    if (currentValue >= previousValue) {
        return QStringLiteral("#15803d");
    }
    return QStringLiteral("#b91c1c");
}

QString sqlClientFacturePredicate()
{
    return QStringLiteral("(ID_CLIENT IS NOT NULL OR UPPER(NVL(TYPE_FACTURE,'')) LIKE '%CLIENT%')");
}

QString sqlSupplierFacturePredicate()
{
    return QStringLiteral("(ID_FOURNISSEUR IS NOT NULL OR UPPER(NVL(TYPE_FACTURE,'')) LIKE '%FOURN%')");
}

QString normalizeFinanceText(const QString &value)
{
    QString q = value.simplified().toLower();
    q.replace(QStringLiteral("é"), QStringLiteral("e"));
    q.replace(QStringLiteral("è"), QStringLiteral("e"));
    q.replace(QStringLiteral("ê"), QStringLiteral("e"));
    q.replace(QStringLiteral("ë"), QStringLiteral("e"));
    q.replace(QStringLiteral("à"), QStringLiteral("a"));
    q.replace(QStringLiteral("â"), QStringLiteral("a"));
    q.replace(QStringLiteral("ä"), QStringLiteral("a"));
    q.replace(QStringLiteral("î"), QStringLiteral("i"));
    q.replace(QStringLiteral("ï"), QStringLiteral("i"));
    q.replace(QStringLiteral("ô"), QStringLiteral("o"));
    q.replace(QStringLiteral("ö"), QStringLiteral("o"));
    q.replace(QStringLiteral("ù"), QStringLiteral("u"));
    q.replace(QStringLiteral("û"), QStringLiteral("u"));
    q.replace(QStringLiteral("ü"), QStringLiteral("u"));
    q.replace(QStringLiteral("ç"), QStringLiteral("c"));
    q.replace(QStringLiteral("'"), QStringLiteral(" "));
    return q;
}

QString invoicePublicBaseUrl()
{
    QString baseUrl = sanitizeBaseUrl(qEnvironmentVariable("ZITOUNATECH_INVOICE_BASE_URL"));
    if (!baseUrl.isEmpty()) {
        return baseUrl;
    }

    const QString netlifyUrl = configuredNetlifyBaseUrl();
    if (!netlifyUrl.isEmpty()) {
        return netlifyUrl;
    }

    if (startInvoiceHttpServerIfNeeded()) {
        baseUrl = g_invoiceHttpBaseUrl;
    } else {
        baseUrl = QStringLiteral("http://127.0.0.1:8099");
    }
    return baseUrl;
}

QString ensureInvoicePagesDir()
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::homePath() + QStringLiteral("/ZITOUNATECH");
    }
    QDir dir(baseDir);
    dir.mkpath(QStringLiteral("invoice_pages"));
    return dir.filePath(QStringLiteral("invoice_pages"));
}

QString buildProfessionalInvoiceHtml(int idFacture,
                                     const QString &matriculeFiscal,
                                     const QString &nomEntreprise,
                                     const QString &raisonSociale,
                                     const QString &typeFacture,
                                     const QString &dateFactureIso,
                                     double prixUnitaire,
                                     int quantite,
                                     const QString &description,
                                     const QString &signatureToken,
                                     const QString &signatureRaw,
                                     const QString &publicUrl)
{
    Q_UNUSED(signatureToken);
    const QLocale locale(QLocale::French, QLocale::Tunisia);
    const double totalHt = prixUnitaire * static_cast<double>(quantite);
    const double tva = totalHt * 0.19;
    const double totalTtc = totalHt + tva;

    QString signatureDataUri;
    if (!signatureRaw.trimmed().isEmpty()) {
        const QByteArray sigBytes = QByteArray::fromBase64(signatureRaw.toLatin1());
        signatureDataUri = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(sigBytes.toBase64());
    }

    const QString qrUrlEsc = publicUrl.toHtmlEscaped();
    Q_UNUSED(qrUrlEsc);
    return QStringLiteral(R"(
<!doctype html>
<html lang="fr">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Facture %1</title>
  <style>
    :root { --green:#2e7d32; --ink:#1f2a37; --muted:#6b7280; --line:#d1d5db; --bg:#f4f7f9; --ok:#166534; }
    *{ box-sizing:border-box; }
    body{ margin:0; font-family:'Segoe UI',Arial,sans-serif; background:var(--bg); color:var(--ink); }
    .page{ max-width:920px; margin:20px auto; background:#fff; border:1px solid #e5e7eb; padding:26px 30px 34px; }
    .topline{ display:flex; justify-content:space-between; color:var(--muted); font-size:13px; border-bottom:1px solid var(--line); padding-bottom:8px; }
    .head{ display:grid; grid-template-columns:1.2fr 1fr; gap:18px; margin-top:20px; }
    .brand h1{ margin:0 0 10px; color:var(--green); font-size:36px; letter-spacing:0.5px; }
    .brand p{ margin:2px 0; line-height:1.4; }
    .billto{ border:1px solid var(--line); padding:14px; min-height:120px; }
    .meta{ margin-top:10px; font-size:14px; }
    .meta b{ display:inline-block; min-width:120px; }
    table{ width:100%; border-collapse:collapse; margin-top:24px; font-size:15px; }
    th{ background:var(--green); color:#fff; text-align:left; padding:10px; border:1px solid #1b5e20; }
    td{ border:1px solid var(--line); padding:10px; vertical-align:top; }
    .totals{ margin-top:22px; display:grid; grid-template-columns:1.4fr 0.8fr; gap:20px; }
    .terms{ border:1px solid var(--line); padding:14px; font-size:14px; line-height:1.4; }
    .sum td{ font-size:20px; text-align:right; }
    .sum .label{ color:var(--muted); font-size:16px; }
    .sum .final{ background:var(--green); color:#fff; font-weight:700; }
    .sign{ margin-top:20px; display:flex; gap:16px; align-items:flex-start; }
    .badge{ display:inline-block; color:var(--ok); border:1px solid #86efac; background:#f0fdf4; border-radius:999px; padding:5px 11px; font-size:12px; }
    .sigbox{ border:1px solid var(--line); background:#f8fafc; border-radius:8px; padding:8px; min-width:220px; }
    .sigbox img{ max-width:100%; max-height:70px; }
  </style>
</head>
<body>
  <section class="page">
    <div class="topline">
      <div>ZITOUNATECH - Facture N° %1</div>
      <div>Page 1/1</div>
    </div>
    <div class="head">
      <div class="brand">
        <h1>Facture</h1>
        <p><b>ZITOUNATECH</b></p>
        <p>Tunis, Tunisie</p>
        <p>Email: contact@zitounatech.tn</p>
        <div class="meta">
          <div><b>Date:</b> %2</div>
          <div><b>Type:</b> %3</div>
          <div><b>Matricule fiscal:</b> %4</div>
        </div>
      </div>
      <div class="billto">
        <p style="margin:0 0 8px; color:#6b7280;">Facture pour</p>
        <p style="margin:0; font-size:20px; font-weight:700;">%5</p>
        <p style="margin:4px 0 0;"><b>ID client/fournisseur:</b> %6</p>
      </div>
    </div>
    <table>
      <thead>
        <tr>
          <th>Description</th>
          <th style="width:150px">Prix unitaire</th>
          <th style="width:110px">Quantite</th>
          <th style="width:170px">Montant HT</th>
        </tr>
      </thead>
      <tbody>
        <tr>
          <td>%8</td>
          <td style="text-align:right;">%9 TND</td>
          <td style="text-align:right;">%10</td>
          <td style="text-align:right; font-weight:700;">%11 TND</td>
        </tr>
      </tbody>
    </table>
    <div class="totals">
      <div class="terms">
        <b>Modalites de paiement</b><br/>
        Reglement a reception de facture. En cas de retard, des penalites peuvent s'appliquer.<br/><br/>
        <span class="badge">Facture signee electroniquement</span>
      </div>
      <table class="sum">
        <tr><td class="label">Total HT</td><td>%11 TND</td></tr>
        <tr><td class="label">TVA 19%%</td><td>%13 TND</td></tr>
        <tr><td class="final">Total TTC</td><td class="final">%14 TND</td></tr>
      </table>
    </div>
    <div class="sign">
      <div class="sigbox">
        <div style="font-size:12px; color:#6b7280; margin-bottom:6px;">Signature manuscrite</div>
        %15
      </div>
    </div>
  </section>
</body>
</html>
)")
        .arg(QString::number(idFacture),
             dateFactureIso.toHtmlEscaped(),
             typeFacture.toHtmlEscaped(),
             matriculeFiscal.toHtmlEscaped(),
             nomEntreprise.toHtmlEscaped(),
             raisonSociale.toHtmlEscaped(),
             description.toHtmlEscaped(),
             locale.toString(prixUnitaire, 'f', 2),
             QString::number(quantite),
             locale.toString(totalHt, 'f', 2),
             locale.toString(tva, 'f', 2),
             locale.toString(totalTtc, 'f', 2),
             signatureDataUri.isEmpty()
                 ? QStringLiteral("<span style='color:#6b7280;'>Aucune signature</span>")
                 : QStringLiteral("<img src='%1' alt='signature' />").arg(signatureDataUri.toHtmlEscaped()));
}

QString buildInvoicePublicUrl(const QString &fileName)
{
    return invoicePublicBaseUrl() + QStringLiteral("/invoices/") + fileName;
}

QString writeInvoiceHtmlAndBuildUrl(int idFacture,
                                    const QString &matriculeFiscal,
                                    const QString &nomEntreprise,
                                    const QString &raisonSociale,
                                    const QString &typeFacture,
                                    const QString &dateFactureIso,
                                    double prixUnitaire,
                                    int quantite,
                                    const QString &description,
                                    const QString &signatureToken,
                                    const QString &signatureRaw,
                                    QString *outHtml,
                                    QString *outLocalFilePath)
{
    Q_UNUSED(signatureToken);
    // Stable file name avoids Netlify 404 when token changes between deployments.
    const QString fileName = QStringLiteral("facture_%1.html").arg(idFacture);
    const QString html = buildProfessionalInvoiceHtml(idFacture,
                                                      matriculeFiscal,
                                                      nomEntreprise,
                                                      raisonSociale,
                                                      typeFacture,
                                                      dateFactureIso,
                                                      prixUnitaire,
                                                      quantite,
                                                      description,
                                                      signatureToken,
                                                      signatureRaw,
                                                      buildInvoicePublicUrl(fileName));
    if (outHtml) {
        *outHtml = html;
    }

    const QString dirPath = ensureInvoicePagesDir();
    const QString localFilePath = QDir(dirPath).filePath(fileName);
    QFile file(localFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        file.write(html.toUtf8());
        file.close();
    }

    const QString netlifyInvoicesDir = configuredNetlifyInvoicesDir();
    if (!netlifyInvoicesDir.isEmpty()) {
        QFile netlifyFile(QDir(netlifyInvoicesDir).filePath(fileName));
        if (netlifyFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            netlifyFile.write(html.toUtf8());
            netlifyFile.close();
        }
    }

    if (outLocalFilePath) {
        *outLocalFilePath = localFilePath;
    }
    return buildInvoicePublicUrl(fileName);
}

QPixmap fallbackQrPlaceholder(int pxSize)
{
    QPixmap pix(pxSize, pxSize);
    pix.fill(Qt::white);
    QPainter painter(&pix);
    painter.setPen(QPen(QColor("#0f172a"), 2));
    painter.drawRect(1, 1, pxSize - 2, pxSize - 2);
    painter.fillRect(8, 8, 24, 24, QColor("#0f172a"));
    painter.fillRect(pxSize - 32, 8, 24, 24, QColor("#0f172a"));
    painter.fillRect(8, pxSize - 32, 24, 24, QColor("#0f172a"));
    painter.setPen(QColor("#334155"));
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 9, QFont::DemiBold));
    painter.drawText(QRect(0, (pxSize / 2) - 10, pxSize, 20), Qt::AlignCenter, QStringLiteral("QR"));
    return pix;
}

QPixmap factureQrPixmap(const QString &payload, int pxSize)
{
    try {
        const QByteArray utf8 = payload.toUtf8();
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(utf8.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int border = 4;
        const int moduleCount = qr.getSize();
        const int qrWithBorder = moduleCount + border * 2;
        const int scale = qMax(1, pxSize / qrWithBorder);
        const int imageSize = qrWithBorder * scale;

        QImage img(imageSize, imageSize, QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter painter(&img);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        for (int y = 0; y < moduleCount; ++y) {
            for (int x = 0; x < moduleCount; ++x) {
                if (qr.getModule(x, y)) {
                    const int xx = (x + border) * scale;
                    const int yy = (y + border) * scale;
                    painter.drawRect(xx, yy, scale, scale);
                }
            }
        }
        painter.end();
        return QPixmap::fromImage(img);
    } catch (const std::exception &) {
        return fallbackQrPlaceholder(pxSize);
    }
}

void afficherFactureQrDialog(QWidget *parent,
                             const QString &publicUrl,
                             const QString &idFacture,
                             const QString &invoiceHtml,
                             const QString &localFilePath)
{
    Q_UNUSED(invoiceHtml);
    Q_UNUSED(localFilePath);
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("QR facture %1").arg(idFacture));
    dialog.resize(360, 380);
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *qrLabel = new QLabel;
    qrLabel->setFixedSize(260, 260);
    qrLabel->setAlignment(Qt::AlignCenter);
    const QPixmap qrPixmap = factureQrPixmap(publicUrl, 240);
    qrLabel->setPixmap(qrPixmap);
    qrLabel->setStyleSheet(QStringLiteral("QLabel { background:#ffffff; border:1px solid #dbe4ea; border-radius:10px; }"));
    layout->addWidget(qrLabel, 0, Qt::AlignCenter);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *openInvoiceBtn = buttons->addButton(QStringLiteral("Ouvrir facture"), QDialogButtonBox::ActionRole);
    QObject::connect(openInvoiceBtn, &QPushButton::clicked, &dialog, [publicUrl]() {
        QDesktopServices::openUrl(QUrl(publicUrl));
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}

bool isFinanceRelatedQuestion(const QString &question)
{
    const QString q = normalizeFinanceText(question);
    if (q.isEmpty()) {
        return false;
    }

    static const QStringList financeKeywords{
        QStringLiteral("finance"), QStringLiteral("financier"), QStringLiteral("gestion financiere"),
        QStringLiteral("budget"), QStringLiteral("tresor"), QStringLiteral("tresorerie"),
        QStringLiteral("cash"), QStringLiteral("flux"), QStringLiteral("encaissement"),
        QStringLiteral("decaissement"), QStringLiteral("facture"), QStringLiteral("facturation"),
        QStringLiteral("paiement"), QStringLiteral("impaye"), QStringLiteral("retard"),
        QStringLiteral("dette"), QStringLiteral("creance"), QStringLiteral("client"),
        QStringLiteral("fournisseur"), QStringLiteral("vente"), QStringLiteral("achat"),
        QStringLiteral("prix"), QStringLiteral("cout"), QStringLiteral("montant"),
        QStringLiteral("marge"), QStringLiteral("benefice"), QStringLiteral("profit"),
        QStringLiteral("perte"), QStringLiteral("revenu"), QStringLiteral("chiffre d'affaires"),
        QStringLiteral("ca"), QStringLiteral("rentabilite"), QStringLiteral("tnd"),
        QStringLiteral("depense"), QStringLiteral("depenser"), QStringLiteral("recette"),
        QStringLiteral("combien"), QStringLiteral("mensuel"), QStringLiteral("annuel"),
        QStringLiteral("top"), QStringLiteral("classement")
    };

    for (const QString &keyword : financeKeywords) {
        if (q.contains(keyword)) {
            return true;
        }
    }
    return false;
}

bool isFactureListQuestion(const QString &question)
{
    const QString q = normalizeFinanceText(question);
    if (!(q.contains(QStringLiteral("facture")) || q.contains(QStringLiteral("factures")))) {
        return false;
    }

    return q.contains(QStringLiteral("donne")) || q.contains(QStringLiteral("liste"))
        || q.contains(QStringLiteral("affiche")) || q.contains(QStringLiteral("montre"))
        || q.contains(QStringLiteral("voir")) || q.contains(QStringLiteral("show"));
}

bool isMonthlyFactureTotalQuestion(const QString &question)
{
    const QString q = normalizeFinanceText(question);
    const bool asksFacture = q.contains(QStringLiteral("facture"));
    const bool asksTotal = q.contains(QStringLiteral("total")) || q.contains(QStringLiteral("montant"))
        || q.contains(QStringLiteral("somme")) || q.contains(QStringLiteral("chiffre d affaires"))
        || q.contains(QStringLiteral("ca")) || q.contains(QStringLiteral("combien"));
    const bool asksMonth = q.contains(QStringLiteral("mois")) || q.contains(QStringLiteral("mensuel"))
        || q.contains(QStringLiteral("ce mois"));
    return asksFacture && asksTotal && asksMonth;
}

bool isTopFournisseursByAmountQuestion(const QString &question)
{
    const QString q = normalizeFinanceText(question);
    const bool asksTop = q.contains(QStringLiteral("top")) || q.contains(QStringLiteral("meilleur"))
        || q.contains(QStringLiteral("classement"));
    const bool asksFournisseur = q.contains(QStringLiteral("fournisseur")) || q.contains(QStringLiteral("fournisseurs"));
    const bool asksAmount = q.contains(QStringLiteral("montant")) || q.contains(QStringLiteral("total"))
        || q.contains(QStringLiteral("depense")) || q.contains(QStringLiteral("achat"));
    return asksTop && asksFournisseur && asksAmount;
}

bool isTopClientsByAmountQuestion(const QString &question)
{
    const QString q = normalizeFinanceText(question);
    const bool asksTop = q.contains(QStringLiteral("top")) || q.contains(QStringLiteral("meilleur"))
        || q.contains(QStringLiteral("classement"));
    const bool asksClient = q.contains(QStringLiteral("client")) || q.contains(QStringLiteral("clients"));
    const bool asksAmount = q.contains(QStringLiteral("montant")) || q.contains(QStringLiteral("total"))
        || q.contains(QStringLiteral("chiffre d affaires")) || q.contains(QStringLiteral("ca"))
        || q.contains(QStringLiteral("vente"));
    return asksTop && asksClient && asksAmount;
}

bool isClientTransactionsThisMonthQuestion(const QString &question)
{
    const QString q = normalizeFinanceText(question);
    const bool asksTransactions = q.contains(QStringLiteral("transaction")) || q.contains(QStringLiteral("transactions"))
        || q.contains(QStringLiteral("facture")) || q.contains(QStringLiteral("factures"));
    const bool asksClients = q.contains(QStringLiteral("client")) || q.contains(QStringLiteral("clients"));
    const bool asksMonth = q.contains(QStringLiteral("ce mois")) || q.contains(QStringLiteral("mois"));
    const bool asksCount = q.contains(QStringLiteral("combien")) || q.contains(QStringLiteral("nombre"));
    return asksTransactions && asksClients && asksMonth && asksCount;
}

bool isSupplierTransactionsThisMonthQuestion(const QString &question)
{
    const QString q = normalizeFinanceText(question);
    const bool asksTransactions = q.contains(QStringLiteral("transaction")) || q.contains(QStringLiteral("transactions"))
        || q.contains(QStringLiteral("facture")) || q.contains(QStringLiteral("factures"));
    const bool asksSuppliers = q.contains(QStringLiteral("fournisseur")) || q.contains(QStringLiteral("fournisseurs"));
    const bool asksMonth = q.contains(QStringLiteral("ce mois")) || q.contains(QStringLiteral("mois"));
    const bool asksCount = q.contains(QStringLiteral("combien")) || q.contains(QStringLiteral("nombre"));
    return asksTransactions && asksSuppliers && asksMonth && asksCount;
}

bool isMonthComparisonQuestion(const QString &question)
{
    const QString q = normalizeFinanceText(question);
    const bool asksComparison = q.contains(QStringLiteral("comparaison")) || q.contains(QStringLiteral("compare"))
        || q.contains(QStringLiteral("vs")) || q.contains(QStringLiteral("variation"));
    const bool mentionsCurrent = q.contains(QStringLiteral("mois courant")) || q.contains(QStringLiteral("ce mois"));
    const bool mentionsPrevious = q.contains(QStringLiteral("mois precedent")) || q.contains(QStringLiteral("precedent"));
    return asksComparison && (mentionsCurrent || q.contains(QStringLiteral("courant"))) && mentionsPrevious;
}

bool isForecastQuestion(const QString &question)
{
    const QString q = normalizeFinanceText(question);
    return q.contains(QStringLiteral("prevision"))
        || ((q.contains(QStringLiteral("mois prochain")) && (q.contains(QStringLiteral("montant")) || q.contains(QStringLiteral("ca"))))
        || q.contains(QStringLiteral("forecast")));
}

bool isEvolutionClientsQuestion(const QString &question)
{
    const QString q = normalizeFinanceText(question);
    const bool asksEvolution = q.contains(QStringLiteral("evolution")) || q.contains(QStringLiteral("tendance"));
    const bool asksClients = q.contains(QStringLiteral("client")) || q.contains(QStringLiteral("clients"));
    const bool asksCa = q.contains(QStringLiteral("ca")) || q.contains(QStringLiteral("chiffre d affaires")) || q.contains(QStringLiteral("montant"));
    return asksEvolution && asksClients && asksCa;
}

bool isAverageTicketQuestion(const QString &question)
{
    const QString q = normalizeFinanceText(question);
    return q.contains(QStringLiteral("ticket moyen")) || q.contains(QStringLiteral("panier moyen"))
        || (q.contains(QStringLiteral("moyen")) && q.contains(QStringLiteral("facture")));
}

QString buildFactureListDirectResponse(QTableWidget *factureTable, int limit = 8)
{
    const int safeLimit = qMax(1, limit);
    QStringList lines;
    lines << QStringLiteral("Dernieres factures:");

    // 1) Source prioritaire: tableau deja charge dans l'UI (fiable et instantane).
    if (factureTable && factureTable->rowCount() > 0) {
        const int rows = qMin(safeLimit, factureTable->rowCount());
        for (int r = 0; r < rows; ++r) {
            const QString id = factureTable->item(r, 0) ? factureTable->item(r, 0)->text().trimmed() : QStringLiteral("-");
            const QString entreprise = factureTable->item(r, 2) ? factureTable->item(r, 2)->text().trimmed() : QStringLiteral("-");
            const QString type = factureTable->item(r, 4) ? factureTable->item(r, 4)->text().trimmed() : QStringLiteral("-");
            const QString dateF = factureTable->item(r, 5) ? factureTable->item(r, 5)->text().trimmed() : QStringLiteral("-");
            const QString prix = factureTable->item(r, 6) ? factureTable->item(r, 6)->text().trimmed() : QStringLiteral("-");
            const QString qte = factureTable->item(r, 7) ? factureTable->item(r, 7)->text().trimmed() : QStringLiteral("-");

            lines << QStringLiteral("- #%1 | %2 | %3 | %4 | PU: %5 | QTE: %6")
                         .arg(id.isEmpty() ? QStringLiteral("-") : id,
                              entreprise.isEmpty() ? QStringLiteral("-") : entreprise,
                              type.isEmpty() ? QStringLiteral("-") : type,
                              dateF.isEmpty() ? QStringLiteral("-") : dateF,
                              prix.isEmpty() ? QStringLiteral("-") : prix,
                              qte.isEmpty() ? QStringLiteral("-") : qte);
        }
        return lines.join('\n');
    }

    // 2) Secours SQL compatible Oracle plus ancien (ROWNUM).
    QSqlQuery query;
    const QString sql = QStringLiteral(
        "SELECT * FROM ("
        "  SELECT ID_FACTURE, NVL(NOM_ENTREPRISE,'-') AS NOM_ENTREPRISE, NVL(TYPE_FACTURE,'-') AS TYPE_FACTURE, "
        "         TO_CHAR(DATE_FACTURATION, 'YYYY-MM-DD') AS DATE_F, "
        "         NVL(PRIX_UNITAIRE, 0) AS PRIX_U, NVL(QUANTITE, 0) AS QTE "
        "  FROM FACTURE "
        "  ORDER BY DATE_FACTURATION DESC, ID_FACTURE DESC"
        ") WHERE ROWNUM <= %1").arg(safeLimit);

    if (!query.exec(sql)) {
        return QStringLiteral("Impossible de charger la liste des factures (%1).")
            .arg(query.lastError().text().trimmed().isEmpty()
                     ? QStringLiteral("erreur SQL")
                     : query.lastError().text().trimmed());
    }

    lines.clear();
    lines << QStringLiteral("Dernieres factures:");
    int count = 0;
    while (query.next()) {
        ++count;
        lines << QStringLiteral("- #%1 | %2 | %3 | %4 | PU: %5 | QTE: %6")
                     .arg(query.value(0).toString(),
                          query.value(1).toString().trimmed().isEmpty() ? QStringLiteral("-") : query.value(1).toString().trimmed(),
                          query.value(2).toString().trimmed().isEmpty() ? QStringLiteral("-") : query.value(2).toString().trimmed(),
                          query.value(3).toString(),
                          query.value(4).toString(),
                          query.value(5).toString());
    }

    if (count == 0) {
        return QStringLiteral("Aucune facture trouvee.");
    }
    return lines.join('\n');
}

QString buildMonthlyFactureTotalResponse()
{
    QSqlQuery query;
    const QString sql = QStringLiteral(
        "SELECT NVL(SUM(PRIX_UNITAIRE * QUANTITE), 0) "
        "FROM FACTURE "
        "WHERE TRUNC(DATE_FACTURATION, 'MM') = TRUNC(SYSDATE, 'MM')");
    if (!query.exec(sql) || !query.next()) {
        return QStringLiteral("Impossible de calculer le montant total des factures ce mois.");
    }

    const QString total = query.value(0).toString();
    return QStringLiteral("Montant total des factures du mois en cours: %1 TND.").arg(total);
}

QString buildTopFournisseursByAmountResponse(int limit = 5)
{
    const int safeLimit = qMax(1, limit);
    QSqlQuery query;
    const QString sql = QStringLiteral(
        "SELECT * FROM ("
        "  SELECT NVL(FR.NOM, NVL(F.NOM_ENTREPRISE, 'Fournisseur non renseigne')) AS NOM_F, "
        "         ROUND(NVL(SUM(F.PRIX_UNITAIRE * F.QUANTITE), 0), 2) AS TOTAL_M "
        "  FROM FACTURE F "
        "  LEFT JOIN FOURNISSEUR FR ON FR.ID_FOURNISSEUR = F.ID_FOURNISSEUR "
        "  WHERE F.ID_FOURNISSEUR IS NOT NULL "
        "     OR UPPER(NVL(F.TYPE_FACTURE, '')) LIKE '%FOURN%' "
        "  GROUP BY NVL(FR.NOM, NVL(F.NOM_ENTREPRISE, 'Fournisseur non renseigne')) "
        "  ORDER BY TOTAL_M DESC"
        ") WHERE ROWNUM <= %1").arg(safeLimit);

    if (!query.exec(sql)) {
        return QStringLiteral("Impossible de charger le top fournisseurs (%1).")
            .arg(query.lastError().text().trimmed().isEmpty()
                     ? QStringLiteral("erreur SQL")
                     : query.lastError().text().trimmed());
    }

    QStringList lines;
    lines << QStringLiteral("Top fournisseurs par montant:");
    int count = 0;
    while (query.next()) {
        ++count;
        lines << QStringLiteral("- %1 : %2 TND")
                     .arg(query.value(0).toString().trimmed().isEmpty() ? QStringLiteral("-") : query.value(0).toString().trimmed(),
                          query.value(1).toString());
    }

    if (count == 0) {
        return QStringLiteral("Aucun fournisseur facture disponible.");
    }
    return lines.join('\n');
}

QString buildTopClientsByAmountResponse(int limit = 5)
{
    const int safeLimit = qMax(1, limit);
    QSqlQuery query;
    const QString sql = QStringLiteral(
        "SELECT * FROM ("
        "  SELECT NVL(C.NOM || ' ' || C.PRENOM, NVL(F.NOM_ENTREPRISE, 'Client non renseigne')) AS NOM_C, "
        "         ROUND(NVL(SUM(F.PRIX_UNITAIRE * F.QUANTITE), 0), 2) AS TOTAL_M "
        "  FROM FACTURE F "
        "  LEFT JOIN CLIENT C ON C.ID_CLIENT = F.ID_CLIENT "
        "  WHERE F.ID_CLIENT IS NOT NULL "
        "     OR UPPER(NVL(F.TYPE_FACTURE, '')) LIKE '%CLIENT%' "
        "  GROUP BY NVL(C.NOM || ' ' || C.PRENOM, NVL(F.NOM_ENTREPRISE, 'Client non renseigne')) "
        "  ORDER BY TOTAL_M DESC"
        ") WHERE ROWNUM <= %1").arg(safeLimit);

    if (!query.exec(sql)) {
        return QStringLiteral("Impossible de charger le top clients (%1).")
            .arg(query.lastError().text().trimmed().isEmpty()
                     ? QStringLiteral("erreur SQL")
                     : query.lastError().text().trimmed());
    }

    QStringList lines;
    lines << QStringLiteral("Top clients par montant:");
    int count = 0;
    while (query.next()) {
        ++count;
        lines << QStringLiteral("- %1 : %2 TND")
                     .arg(query.value(0).toString().trimmed().isEmpty() ? QStringLiteral("-") : query.value(0).toString().trimmed(),
                          query.value(1).toString());
    }

    if (count == 0) {
        return QStringLiteral("Aucun client facture disponible.");
    }
    return lines.join('\n');
}

QString buildClientTransactionsThisMonthResponse()
{
    QSqlQuery query;
    const QString sql = QStringLiteral(
        "SELECT COUNT(*) "
        "FROM FACTURE "
        "WHERE TRUNC(DATE_FACTURATION, 'MM') = TRUNC(SYSDATE, 'MM') "
        "AND %1").arg(sqlClientFacturePredicate());
    if (!query.exec(sql) || !query.next()) {
        return QStringLiteral("Impossible de calculer les transactions clients du mois.");
    }
    return QStringLiteral("Transactions clients ce mois: %1.").arg(query.value(0).toInt());
}

QString buildSupplierTransactionsThisMonthResponse()
{
    QSqlQuery query;
    const QString sql = QStringLiteral(
        "SELECT COUNT(*) "
        "FROM FACTURE "
        "WHERE TRUNC(DATE_FACTURATION, 'MM') = TRUNC(SYSDATE, 'MM') "
        "AND %1").arg(sqlSupplierFacturePredicate());
    if (!query.exec(sql) || !query.next()) {
        return QStringLiteral("Impossible de calculer les transactions fournisseurs du mois.");
    }
    return QStringLiteral("Transactions fournisseurs ce mois: %1.").arg(query.value(0).toInt());
}

QString buildMonthComparisonResponse()
{
    QSqlQuery query;
    const QString sql = QStringLiteral(
        "SELECT "
        "NVL(SUM(CASE WHEN TRUNC(DATE_FACTURATION, 'MM') = TRUNC(SYSDATE, 'MM') "
        "THEN NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0) ELSE 0 END), 0) AS CURRENT_REVENUE, "
        "NVL(SUM(CASE WHEN TRUNC(DATE_FACTURATION, 'MM') = ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -1) "
        "THEN NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0) ELSE 0 END), 0) AS PREVIOUS_REVENUE "
        "FROM FACTURE "
        "WHERE %1").arg(sqlClientFacturePredicate());
    if (!query.exec(sql) || !query.next()) {
        return QStringLiteral("Impossible de comparer les mois.");
    }
    const double current = query.value(0).toDouble();
    const double previous = query.value(1).toDouble();
    const double delta = current - previous;
    const double pct = qFuzzyCompare(previous + 1.0, 1.0) ? (current > 0.0 ? 100.0 : 0.0) : ((delta / previous) * 100.0);
    const QString sign = pct >= 0.0 ? QStringLiteral("+") : QString();
    return QStringLiteral("Comparaison clients (mois courant vs precedent): %1 TND vs %2 TND (%3%4%).")
        .arg(QString::number(current, 'f', 2),
             QString::number(previous, 'f', 2),
             sign,
             QString::number(pct, 'f', 1));
}

QString buildAverageTicketResponse()
{
    QSqlQuery query;
    const QString sql = QStringLiteral(
        "SELECT NVL(AVG(NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0)), 0) "
        "FROM FACTURE "
        "WHERE %1").arg(sqlClientFacturePredicate());
    if (!query.exec(sql) || !query.next()) {
        return QStringLiteral("Impossible de calculer le ticket moyen.");
    }
    return QStringLiteral("Ticket moyen clients: %1 TND.").arg(QString::number(query.value(0).toDouble(), 'f', 2));
}

QString buildEvolutionClientsResponse()
{
    QSqlQuery query;
    const QString sql = QStringLiteral(
        "SELECT TO_CHAR(TRUNC(DATE_FACTURATION, 'MM'), 'YYYY-MM') AS MKEY, "
        "NVL(SUM(NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0)), 0) "
        "FROM FACTURE "
        "WHERE DATE_FACTURATION >= ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -5) "
        "AND %1 "
        "GROUP BY TRUNC(DATE_FACTURATION, 'MM') "
        "ORDER BY TRUNC(DATE_FACTURATION, 'MM')").arg(sqlClientFacturePredicate());
    if (!query.exec(sql)) {
        return QStringLiteral("Impossible de charger l'evolution du CA clients.");
    }
    QStringList lines;
    lines << QStringLiteral("Evolution CA clients (6 derniers mois):");
    int n = 0;
    while (query.next()) {
        ++n;
        lines << QStringLiteral("- %1 : %2 TND").arg(query.value(0).toString(), QString::number(query.value(1).toDouble(), 'f', 2));
    }
    if (n == 0) {
        return QStringLiteral("Aucune donnee clients sur les 6 derniers mois.");
    }
    return lines.join('\n');
}

QString buildForecastMonth1Response()
{
    QMap<QString, double> revenueByMonth;
    QSqlQuery query;
    const QString sql = QStringLiteral(
        "SELECT TO_CHAR(TRUNC(DATE_FACTURATION, 'MM'), 'YYYY-MM') AS MKEY, "
        "NVL(SUM(NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0)), 0) "
        "FROM FACTURE "
        "WHERE DATE_FACTURATION >= ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -5) "
        "AND %1 "
        "GROUP BY TRUNC(DATE_FACTURATION, 'MM') "
        "ORDER BY TRUNC(DATE_FACTURATION, 'MM')").arg(sqlClientFacturePredicate());
    if (!query.exec(sql)) {
        return QStringLiteral("Impossible de calculer la prevision du mois prochain.");
    }
    while (query.next()) {
        revenueByMonth.insert(query.value(0).toString(), query.value(1).toDouble());
    }
    QVector<double> y;
    y.reserve(6);
    QDate cursor = QDate::currentDate().addMonths(-5);
    for (int i = 0; i < 6; ++i) {
        y.push_back(revenueByMonth.value(cursor.toString(QStringLiteral("yyyy-MM")), 0.0));
        cursor = cursor.addMonths(1);
    }
    if (y.size() < 2) {
        return QStringLiteral("Prevision indisponible: historique insuffisant.");
    }
    double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumXY = 0.0;
    for (int i = 0; i < y.size(); ++i) {
        const double x = static_cast<double>(i);
        sumX += x;
        sumY += y[i];
        sumXX += x * x;
        sumXY += x * y[i];
    }
    const double denom = (y.size() * sumXX) - (sumX * sumX);
    const double slope = qFuzzyIsNull(denom) ? 0.0 : (((y.size() * sumXY) - (sumX * sumY)) / denom);
    const double intercept = (sumY - slope * sumX) / static_cast<double>(y.size());
    const double forecast = qMax(0.0, intercept + slope * static_cast<double>(y.size()));
    return QStringLiteral("Prevision clients (M+1): %1 TND.").arg(QString::number(forecast, 'f', 2));
}

QComboBox *clientGenreCombo(QWidget *root)
{
    return root ? root->findChild<QComboBox *>(QStringLiteral("emailEdit_17")) : nullptr;
}

QTableWidget *commandesClientsTableWidget(QWidget *root)
{
    return root ? root->findChild<QTableWidget *>(QStringLiteral("commandesClientsTable")) : nullptr;
}

QLineEdit *commandesClientsSearchInput(QWidget *root)
{
    return root ? root->findChild<QLineEdit *>(QStringLiteral("searchCommandesClientsInput")) : nullptr;
}

QPushButton *commandesClientsSortBtn(QWidget *root)
{
    return root ? root->findChild<QPushButton *>(QStringLiteral("sortCommandesClientsBtn")) : nullptr;
}

QString colonneNatureFournisseurStats()
{
    static QString cachedColumn;
    if (!cachedColumn.isEmpty()) {
        return cachedColumn;
    }

    QSqlQuery q;
    if (q.exec(QStringLiteral(
            "SELECT COLUMN_NAME "
            "FROM USER_TAB_COLUMNS "
            "WHERE TABLE_NAME = 'FOURNISSEUR' "
            "AND COLUMN_NAME IN ('NATURE', 'TYPE_FOURNISSEUR', 'TYPE')"))) {
        bool hasNature = false;
        bool hasTypeFournisseur = false;
        bool hasType = false;
        while (q.next()) {
            const QString col = q.value(0).toString().trimmed().toUpper();
            if (col == QStringLiteral("NATURE")) {
                hasNature = true;
            } else if (col == QStringLiteral("TYPE_FOURNISSEUR")) {
                hasTypeFournisseur = true;
            } else if (col == QStringLiteral("TYPE")) {
                hasType = true;
            }
        }
        if (hasNature) {
            cachedColumn = QStringLiteral("NATURE");
        } else if (hasTypeFournisseur) {
            cachedColumn = QStringLiteral("TYPE_FOURNISSEUR");
        } else if (hasType) {
            cachedColumn = QStringLiteral("TYPE");
        }
    }

    if (cachedColumn.isEmpty()) {
        cachedColumn = QStringLiteral("NATURE");
    }
    return cachedColumn;
}

QString colonneTypeFournisseurStats()
{
    static QString cachedColumn;
    if (!cachedColumn.isEmpty()) {
        return cachedColumn;
    }

    QSqlQuery q;
    if (q.exec(QStringLiteral(
            "SELECT COLUMN_NAME "
            "FROM USER_TAB_COLUMNS "
            "WHERE TABLE_NAME = 'FOURNISSEUR' "
            "AND COLUMN_NAME IN ('TYPE', 'TYPE_FOURNISSEUR', 'NATURE')"))) {
        bool hasType = false;
        bool hasTypeFournisseur = false;
        bool hasNature = false;
        while (q.next()) {
            const QString col = q.value(0).toString().trimmed().toUpper();
            if (col == QStringLiteral("TYPE")) {
                hasType = true;
            } else if (col == QStringLiteral("TYPE_FOURNISSEUR")) {
                hasTypeFournisseur = true;
            } else if (col == QStringLiteral("NATURE")) {
                hasNature = true;
            }
        }
        if (hasType) {
            cachedColumn = QStringLiteral("TYPE");
        } else if (hasTypeFournisseur) {
            cachedColumn = QStringLiteral("TYPE_FOURNISSEUR");
        } else if (hasNature) {
            cachedColumn = QStringLiteral("NATURE");
        }
    }

    if (cachedColumn.isEmpty()) {
        cachedColumn = QStringLiteral("TYPE");
    }
    return cachedColumn;
}

QLineEdit *clientCinEdit(MainWindow *window)
{
    return window ? window->findChild<QLineEdit *>(QStringLiteral("cinEdit")) : nullptr;
}

struct ClientInsertPayload
{
    QString nom;
    QString prenom;
    QString email;
    QString telephone;
    QString cin;
    QString genre;
    QDate dateEnregistrement;
    QString achats;
};

struct ClientUpdatePayload
{
    int id = -1;
    QString nom;
    QString prenom;
    QString email;
    QString telephone;
    QString cin;
    QString genre;
    QDate dateEnregistrement;
    QString achats;
};

struct DbTaskResult
{
    bool ok = false;
    QString error;
    int createdClientId = -1;
};

bool openWorkerOracleConnection(const QString &connectionName, QSqlDatabase *dbOut, QString *errorOut = nullptr)
{
    if (!dbOut) {
        if (errorOut)
            *errorOut = QStringLiteral("Handle de connexion invalide.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QODBC"), connectionName);

    const QString user = qEnvironmentVariable("ZITOUNATECH_DB_USER").trimmed().isEmpty()
                             ? QStringLiteral("racem")
                             : qEnvironmentVariable("ZITOUNATECH_DB_USER").trimmed();
    const QString pass = qEnvironmentVariable("ZITOUNATECH_DB_PASSWORD").trimmed().isEmpty()
                             ? QStringLiteral("1234")
                             : qEnvironmentVariable("ZITOUNATECH_DB_PASSWORD").trimmed();

    db.setUserName(user);
    db.setPassword(pass);

    QStringList candidates;
    const QString explicitConnect = qEnvironmentVariable("ZITOUNATECH_DB_ODBC_CONNECT").trimmed();
    const QString envDsn = qEnvironmentVariable("ZITOUNATECH_DB_DSN").trimmed();
    const QString driver = qEnvironmentVariable("ZITOUNATECH_DB_DRIVER").trimmed();
    const QString dbq = qEnvironmentVariable("ZITOUNATECH_DB_DBQ").trimmed();

    if (!explicitConnect.isEmpty())
        candidates << explicitConnect;
    if (!envDsn.isEmpty())
        candidates << QStringLiteral("DSN=%1;").arg(envDsn);

    candidates << QStringLiteral("DSN=zitouna;")
               << QStringLiteral("DSN=ZITOUNA;")
               << QStringLiteral("DSN=Zitounatech;");

    const QString finalDriver = driver.isEmpty() ? QStringLiteral("Oracle in XE") : driver;
    const QString finalDbq = dbq.isEmpty() ? QStringLiteral("localhost:1521/XE") : dbq;
    candidates << QStringLiteral("Driver={%1};DBQ=%2;").arg(finalDriver, finalDbq);
    candidates.removeDuplicates();

    QStringList failures;
    for (const QString &candidate : candidates) {
        db.setDatabaseName(candidate);
        if (db.open()) {
            *dbOut = db;
            return true;
        }
        failures << QStringLiteral("[%1] %2").arg(candidate, db.lastError().text());
    }

    if (errorOut)
        *errorOut = failures.join(QStringLiteral("\n"));
    return false;
}

int fetchNextClientId(QSqlDatabase &db, QString *errorOut = nullptr)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT NVL(MAX(ID_CLIENT), 0) + 1 FROM CLIENT"))) {
        if (errorOut)
            *errorOut = q.lastError().text();
        return -1;
    }
    if (!q.next()) {
        if (errorOut)
            *errorOut = QStringLiteral("Impossible de calculer le prochain ID client.");
        return -1;
    }
    return q.value(0).toInt();
}

struct ExistingClientInfo
{
    bool found = false;
    int id = -1;
    QString nom;
    QString prenom;
    QString email;
    QString telephone;
    QString cin;
    QString genre;
    QString achats;
    QDate dateEnregistrement;
};

ExistingClientInfo fetchClientByCin(const QString &cin)
{
    ExistingClientInfo info;
    QSqlQuery query(QSqlDatabase::database());
    query.prepare(QStringLiteral("SELECT ID_CLIENT, NOM, PRENOM, EMAIL, TELEPHONE, CIN, GENRE, DATE_ENREGISTREMENT, ACHATS "
                                 "FROM CLIENT WHERE CIN = :cin"));
    query.bindValue(QStringLiteral(":cin"), cin);
    if (query.exec() && query.next()) {
        info.found = true;
        info.id = query.value(0).toInt();
        info.nom = query.value(1).toString();
        info.prenom = query.value(2).toString();
        info.email = query.value(3).toString();
        info.telephone = query.value(4).toString();
        info.cin = query.value(5).toString();
        info.genre = query.value(6).toString();
        info.dateEnregistrement = query.value(7).toDate();
        info.achats = query.value(8).toString();
    }
    return info;
}

bool insererCommandeClient(int idClient,
                           const QString &typeCommande,
                           const QString &libelleClient,
                           const QString &cinClient,
                           QString *errorOut = nullptr)
{
    // Evite ORA-00001: la "commande initiale" ne doit exister qu'une seule fois par client.
    QSqlQuery existsCmd(QSqlDatabase::database());
    existsCmd.prepare(QStringLiteral(
        "SELECT 1 FROM FACTURE "
        "WHERE ID_CLIENT = :id_client AND UPPER(NOM_RAISON_SOCIALE) = 'COMMANDE CLIENT' "
        "FETCH FIRST 1 ROWS ONLY"));
    existsCmd.bindValue(QStringLiteral(":id_client"), idClient);
    if (existsCmd.exec() && existsCmd.next()) {
        return true;
    }

    QSqlQuery insertCmd(QSqlDatabase::database());
    insertCmd.prepare(QStringLiteral(
        "INSERT INTO FACTURE ("
        "ID_FACTURE, MATRICULE_FISCAL, NOM_ENTREPRISE, NOM_RAISON_SOCIALE, "
        "TYPE_FACTURE, DATE_FACTURATION, PRIX_UNITAIRE, QUANTITE, DESCRIPTION, "
        "ID_CLIENT, ID_FOURNISSEUR"
        ") VALUES ("
        "SEQ_FACTURE.NEXTVAL, :matricule, :entreprise, :raison, "
        ":type, :datef, :prix, :qte, :descr, :id_client, NULL"
        ")"));

    const QString matriculeBase = cinClient.isEmpty() ? QStringLiteral("CIN-%1").arg(idClient) : cinClient;
    insertCmd.bindValue(QStringLiteral(":matricule"), QStringLiteral("%1-C%2").arg(matriculeBase).arg(idClient));
    insertCmd.bindValue(QStringLiteral(":entreprise"),
                        libelleClient.isEmpty() ? QStringLiteral("Client %1").arg(idClient) : libelleClient);
    insertCmd.bindValue(QStringLiteral(":raison"), QStringLiteral("Commande client"));
    insertCmd.bindValue(QStringLiteral(":type"),
                        typeCommande.trimmed().isEmpty() ? QStringLiteral("ACHAT OLIVE") : typeCommande.trimmed());
    insertCmd.bindValue(QStringLiteral(":datef"), QDate::currentDate());
    insertCmd.bindValue(QStringLiteral(":prix"), 1.0);
    insertCmd.bindValue(QStringLiteral(":qte"), 1);
    insertCmd.bindValue(QStringLiteral(":descr"), QStringLiteral("Commande enregistree depuis module clients."));
    insertCmd.bindValue(QStringLiteral(":id_client"), idClient);

    if (!insertCmd.exec()) {
        const QString dbErr = insertCmd.lastError().text();
        if (dbErr.contains(QStringLiteral("ORA-00001"), Qt::CaseInsensitive)) {
            // Idempotence: si une contrainte unique est déjà satisfaite, considérer l'opération comme déjà faite.
            return true;
        }
        if (errorOut) {
            *errorOut = dbErr;
        }
        return false;
    }
    return true;
}

DbTaskResult insertClientInBackground(const ClientInsertPayload &payload)
{
    const QString connectionName = QStringLiteral("client_insert_%1_%2")
                                       .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
                                       .arg(QDateTime::currentMSecsSinceEpoch());

    DbTaskResult result;
    {
        QSqlDatabase db;
        QString openErr;
        if (!openWorkerOracleConnection(connectionName, &db, &openErr)) {
            result.ok = false;
            result.error = openErr;
        } else {
            bool inserted = false;
            QString lastErr;
            for (int attempt = 0; attempt < 3 && !inserted; ++attempt) {
                const int nextId = fetchNextClientId(db, &lastErr);
                if (nextId <= 0) {
                    break;
                }

                QSqlQuery query(db);
                query.prepare(
                    QStringLiteral("INSERT INTO CLIENT "
                                   "(ID_CLIENT, NOM, PRENOM, EMAIL, TELEPHONE, CIN, GENRE, DATE_ENREGISTREMENT, ACHATS) "
                                   "VALUES (:id_client, :nom, :prenom, :email, :telephone, :cin, :genre, "
                                   ":date_enregistrement, :achats)"));
                query.bindValue(QStringLiteral(":id_client"), nextId);
                query.bindValue(QStringLiteral(":nom"), payload.nom);
                query.bindValue(QStringLiteral(":prenom"), payload.prenom);
                query.bindValue(QStringLiteral(":email"), payload.email);
                query.bindValue(QStringLiteral(":telephone"), payload.telephone);
                query.bindValue(QStringLiteral(":cin"), payload.cin);
                query.bindValue(QStringLiteral(":genre"), payload.genre);
                query.bindValue(QStringLiteral(":date_enregistrement"), payload.dateEnregistrement);
                query.bindValue(QStringLiteral(":achats"), payload.achats);

                if (query.exec()) {
                    inserted = true;
                    result.ok = true;
                    result.createdClientId = nextId;
                } else {
                    lastErr = query.lastError().text();
                }
            }
            if (!inserted) {
                result.ok = false;
                result.error = lastErr.isEmpty() ? QStringLiteral("Echec insertion client.") : lastErr;
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

DbTaskResult updateClientInBackground(const ClientUpdatePayload &payload)
{
    const QString connectionName = QStringLiteral("client_update_%1_%2")
                                       .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
                                       .arg(QDateTime::currentMSecsSinceEpoch());

    DbTaskResult result;
    {
        QSqlDatabase db;
        QString openErr;
        if (!openWorkerOracleConnection(connectionName, &db, &openErr)) {
            result.ok = false;
            result.error = openErr;
        } else {
            QSqlQuery query(db);
            query.prepare(
                QStringLiteral("UPDATE CLIENT SET NOM = :nom, PRENOM = :prenom, EMAIL = :email, "
                               "TELEPHONE = :telephone, CIN = :cin, GENRE = :genre, DATE_ENREGISTREMENT = :date_enregistrement, "
                               "ACHATS = :achats WHERE ID_CLIENT = :id"));
            query.bindValue(QStringLiteral(":id"), payload.id);
            query.bindValue(QStringLiteral(":nom"), payload.nom);
            query.bindValue(QStringLiteral(":prenom"), payload.prenom);
            query.bindValue(QStringLiteral(":email"), payload.email);
            query.bindValue(QStringLiteral(":telephone"), payload.telephone);
            query.bindValue(QStringLiteral(":cin"), payload.cin);
            query.bindValue(QStringLiteral(":genre"), payload.genre);
            query.bindValue(QStringLiteral(":date_enregistrement"), payload.dateEnregistrement);
            query.bindValue(QStringLiteral(":achats"), payload.achats);

            if (!query.exec()) {
                result.ok = false;
                result.error = query.lastError().text();
            } else {
                result.ok = true;
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

DbTaskResult deleteClientInBackground(int idClient)
{
    const QString connectionName = QStringLiteral("client_delete_%1_%2")
                                       .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
                                       .arg(QDateTime::currentMSecsSinceEpoch());

    DbTaskResult result;
    {
        QSqlDatabase db;
        QString openErr;
        if (!openWorkerOracleConnection(connectionName, &db, &openErr)) {
            result.ok = false;
            result.error = openErr;
        } else {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("DELETE FROM CLIENT WHERE ID_CLIENT = :id"));
            query.bindValue(QStringLiteral(":id"), idClient);
            if (!query.exec()) {
                result.ok = false;
                result.error = query.lastError().text();
            } else {
                result.ok = true;
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return result;
}

QProgressDialog *ensureClientBusyDialog(MainWindow *window)
{
    if (!window) {
        return nullptr;
    }
    auto *dialog = window->findChild<QProgressDialog *>(QStringLiteral("clientBusyDialog"));
    if (!dialog) {
        dialog = new QProgressDialog(window);
        dialog->setObjectName(QStringLiteral("clientBusyDialog"));
        dialog->setWindowTitle(QStringLiteral("Traitement"));
        dialog->setCancelButton(nullptr);
        dialog->setRange(0, 0);
        dialog->setMinimumDuration(0);
        dialog->setWindowModality(Qt::WindowModal);
        dialog->setAutoClose(false);
        dialog->setAutoReset(false);
    }
    return dialog;
}

void setClientBusyUi(MainWindow *window, bool busy, const QString &message = QString())
{
    if (!window) {
        return;
    }
    if (auto *btn = window->findChild<QPushButton *>(QStringLiteral("saveEmployeeBtn_2")))
        btn->setEnabled(!busy);
    if (auto *btn = window->findChild<QPushButton *>(QStringLiteral("deleteBtn_2")))
        btn->setEnabled(!busy);
    if (auto *btn = window->findChild<QPushButton *>(QStringLiteral("modifyBtn_2")))
        btn->setEnabled(!busy);
    if (auto *btn = window->findChild<QPushButton *>(QStringLiteral("cancelBtn_2")))
        btn->setEnabled(!busy);

    QProgressDialog *dialog = ensureClientBusyDialog(window);
    if (!dialog) {
        return;
    }
    if (busy) {
        dialog->setLabelText(message.isEmpty() ? QStringLiteral("Traitement en cours...") : message);
        dialog->show();
    } else {
        dialog->hide();
    }
}

class SignaturePadWidget : public QWidget
{
public:
    explicit SignaturePadWidget(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_image(520, 180, QImage::Format_RGB32)
    {
        setMinimumSize(m_image.size());
        setCursor(Qt::CrossCursor);
        clearPad();
    }

    void clearPad()
    {
        m_image.fill(Qt::white);
        m_hasInk = false;
        update();
    }

    bool hasInk() const
    {
        return m_hasInk;
    }

    QImage image() const
    {
        return m_image;
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (!event || event->button() != Qt::LeftButton) {
            return;
        }
        m_drawing = true;
        m_lastPoint = event->position().toPoint();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!event || !m_drawing || !(event->buttons() & Qt::LeftButton)) {
            return;
        }
        drawLineTo(event->position().toPoint());
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!event || event->button() != Qt::LeftButton) {
            return;
        }
        if (m_drawing) {
            drawLineTo(event->position().toPoint());
        }
        m_drawing = false;
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(250, 250, 250));
        painter.drawImage(QPoint(0, 0), m_image);
        painter.setPen(QPen(QColor(173, 181, 189), 1));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    }

private:
    void drawLineTo(const QPoint &endPoint)
    {
        QPoint clamped = endPoint;
        clamped.setX(qBound(0, clamped.x(), m_image.width() - 1));
        clamped.setY(qBound(0, clamped.y(), m_image.height() - 1));
        m_lastPoint.setX(qBound(0, m_lastPoint.x(), m_image.width() - 1));
        m_lastPoint.setY(qBound(0, m_lastPoint.y(), m_image.height() - 1));

        QPainter painter(&m_image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(16, 24, 40), 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(m_lastPoint, clamped);
        m_lastPoint = clamped;
        m_hasInk = true;
        update();
    }

    QImage m_image;
    QPoint m_lastPoint;
    bool m_drawing = false;
    bool m_hasInk = false;
};

QString encodeSignatureImageBase64(const QImage &source)
{
    if (source.isNull()) {
        return QString();
    }

    const int maxChars = 3900;
    QSize currentSize = source.size();
    for (int sizeAttempt = 0; sizeAttempt < 6; ++sizeAttempt) {
        const QImage scaled = source.scaled(currentSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        for (int quality = 90; quality >= 35; quality -= 10) {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            if (!buffer.open(QIODevice::WriteOnly)) {
                continue;
            }
            if (!scaled.save(&buffer, "JPG", quality)) {
                continue;
            }
            const QString base64 = QString::fromLatin1(bytes.toBase64(QByteArray::Base64Encoding));
            if (base64.size() <= maxChars) {
                return base64;
            }
        }

        currentSize.setWidth(qMax(220, static_cast<int>(currentSize.width() * 0.85)));
        currentSize.setHeight(qMax(80, static_cast<int>(currentSize.height() * 0.85)));
    }

    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (buffer.open(QIODevice::WriteOnly)) {
        source.scaled(220, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buffer, "JPG", 35);
    }
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64Encoding)).left(maxChars);
}

QString demanderSignatureManuscrite(QWidget *parent, const QString &signedBy, bool *accepted)
{
    if (accepted) {
        *accepted = false;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Signature manuscrite"));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setSpacing(10);

    auto *infoLabel = new QLabel(
        QStringLiteral("Signez ci-dessous avec la souris (ou le trackpad).\nSignataire: %1")
            .arg(signedBy.isEmpty() ? QStringLiteral("UtilisateurApp") : signedBy),
        &dialog);
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);

    auto *pad = new SignaturePadWidget(&dialog);
    layout->addWidget(pad);

    auto *hint = new QLabel(QStringLiteral("Astuce: utilisez 'Effacer' pour recommencer la signature."), &dialog);
    hint->setStyleSheet(QStringLiteral("QLabel { color: #475467; font-size: 11px; }"));
    layout->addWidget(hint);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QPushButton *okButton = buttons->button(QDialogButtonBox::Ok);
    QPushButton *cancelButton = buttons->button(QDialogButtonBox::Cancel);
    if (okButton) okButton->setText(QStringLiteral("Signer"));
    if (cancelButton) cancelButton->setText(QStringLiteral("Annuler"));

    auto *clearButton = new QPushButton(QStringLiteral("Effacer"), &dialog);
    buttons->addButton(clearButton, QDialogButtonBox::ResetRole);
    QObject::connect(clearButton, &QPushButton::clicked, pad, &SignaturePadWidget::clearPad);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, pad]() {
        if (!pad->hasInk()) {
            QMessageBox::warning(&dialog,
                                 QStringLiteral("Signature"),
                                 QStringLiteral("Veuillez dessiner une signature avant de valider."));
            return;
        }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return QString();
    }

    if (accepted) {
        *accepted = true;
    }
    return encodeSignatureImageBase64(pad->image());
}

void afficherSignatureManuscrite(QWidget *parent, const QString &base64, const QString &factureId)
{
    const QString payload = base64.trimmed();
    if (payload.isEmpty()) {
        QMessageBox::information(parent,
                                 QStringLiteral("Signature"),
                                 QStringLiteral("Aucune signature manuscrite pour cette facture."));
        return;
    }

    const QByteArray raw = QByteArray::fromBase64(payload.toLatin1());
    QImage image;
    if (!image.loadFromData(raw)) {
        QMessageBox::warning(parent,
                             QStringLiteral("Signature"),
                             QStringLiteral("Signature manuscrite invalide ou corrompue."));
        return;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Signature facture %1").arg(factureId));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(&dialog);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("QLabel { background: #ffffff; border: 1px solid #d0d5dd; border-radius: 8px; padding: 12px; }"));
    label->setPixmap(QPixmap::fromImage(image).scaled(760, 280, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout->addWidget(label);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog.exec();
}
} // namespace

void MainWindow::setupClientsFinanceModule()
{
    // ==================== CONFIGURATION DATE PAR DEFAUT ====================
    // Définir la date du jour pour le champ date du client
    ui->startDateEdit_2->setDate(QDate::currentDate());

    // Définir la date du jour pour le champ date du fournisseur
    ui->startDateEdit_4->setDate(QDate::currentDate());

    // Définir la date du jour pour le champ date de la facture
    ui->startDateEdit_5->setDate(QDate::currentDate());

    configurerControleSaisieFournisseur();
    configurerControleSaisieFacture();

    // ==================== CONFIGURATION TABLEAUX ====================

    const QString tableStyle = R"(
QTableWidget {
    border: 1px solid #d9e2da;
    border-radius: 10px;
    background: #ffffff;
    gridline-color: transparent;
    font-family: 'Poppins', 'Segoe UI', Arial, sans-serif;
    font-size: 13px;
    color: #1f2937;
}
QTableWidget::item {
    padding: 10px 8px;
    border: none;
    border-bottom: 1px solid #eef2ef;
}
QTableWidget::item:selected {
    background: #e8f5e9;
    color: #111827;
}
QHeaderView::section {
    background: #f8fbf8;
    color: #1f2937;
    font-weight: 600;
    border: none;
    border-bottom: 2px solid #2E7D32;
    padding: 8px 6px;
}
)";

    auto configTable = [&tableStyle](QTableWidget *table, bool applyCompactStyle) {
        if (!table) return;
        if (applyCompactStyle)
            table->setStyleSheet(tableStyle);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setAlternatingRowColors(false);
        table->setShowGrid(false);
        table->setWordWrap(false);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setStretchLastSection(true);
        table->setMinimumHeight(260);
    };

    // Même rendu que le tableau employés (feuille de style Qt Designer), pas le thème compact.
    configTable(ui->employeeTable_2, false);
    configTable(ui->employeeTable_4, true);
    configTable(ui->employeeTable_5, true);

    // ==================== CONFIGURATION BOUTONS ====================

    const QString dangerBtnStyle = "QPushButton { background:#b42318; color:white; border:none; border-radius:8px; padding:8px 14px; }"
                                   "QPushButton:hover { background:#912018; }";
    const QString primaryBtnStyle = "QPushButton { background:#2E7D32; color:white; border:none; border-radius:8px; padding:8px 14px; }"
                                    "QPushButton:hover { background:#27692B; }";
    if (ui->modifyBtn_2) ui->modifyBtn_2->setStyleSheet(primaryBtnStyle);
    if (ui->deleteBtn_2) ui->deleteBtn_2->setStyleSheet(dangerBtnStyle);

    // ==================== CONFIGURATION CLIENTS ====================

    if (ui->modifyBtn_2) {
        connect(ui->modifyBtn_2, &QPushButton::clicked, this, &MainWindow::handleClientModifyRequested);
    }
    if (ui->employeeTable_2) {
        ui->employeeTable_2->setColumnCount(8);
        ui->employeeTable_2->setHorizontalHeaderLabels(
            {"ID", "Nom", "Prenom", "Email", "Telephone", "CIN", "Date d'enreg.", "Achats"});
    }
    if (auto *cinEdit = clientCinEdit(this)) {
        cinEdit->setMaxLength(8);
        cinEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^[0-9]{0,8}$")), cinEdit));
    }

    activerBoutonsClient(false);
    afficherClients();
    configurerTriAvanceClient();
    setupCommandesClientsSection();
    chargerCommandesClients();
    if (ui->clientAiChatTabLayout && !m_clientAiChatWidget) {
        m_clientAiChatWidget = new AiChatWidget(QSqlDatabase::database(), ui->clientAiChatTab);
        ui->clientAiChatTabLayout->addWidget(m_clientAiChatWidget);
    }

    connect(ui->employeeTable_2, &QTableWidget::itemSelectionChanged, this, [this]() {
        const int row = ui->employeeTable_2 ? ui->employeeTable_2->currentRow() : -1;
        if (row < 0) {
            idClientSelectionne = -1;
            activerBoutonsClient(false);
            return;
        }
        QTableWidgetItem *idItem = ui->employeeTable_2->item(row, 0);
        bool ok = false;
        const int id = idItem ? idItem->text().toInt(&ok) : -1;
        idClientSelectionne = ok ? id : -1;
        activerBoutonsClient(idClientSelectionne > 0);
    });

    // ==================== CONFIGURATION FACTURES ====================

    ui->employeeTable_5->setRowCount(0);
    ui->employeeTable_5->setColumnCount(11);
    ui->employeeTable_5->setHorizontalHeaderLabels(
        {"ID", "Matricule fiscal", "Entreprise", "ID (client/fournisseur)", "Type", "Date", "Prix unitaire", "Quantite", "Description", "Signature manuscrite", "QR"});
    ui->employeeTable_5->setColumnWidth(kFactureSignatureColumn, 150);
    ui->employeeTable_5->setColumnWidth(kFactureQrColumn, 80);
    ui->modifyBtn_5->setEnabled(false);
    ui->deleteBtn_5->setEnabled(false);
    configurerTriAvanceFacture();
    initialiserStatistiquesFinance();
    setupFinanceAssistantTab();
    connect(ui->VentesTab_2, &QTabWidget::currentChanged, this, [this](int index) {
        if (ui->VentesTab_2->widget(index) == ui->stattabvente_2) {
            mettreAJourStatistiquesFinance();
        }
    });

    connect(ui->saveclientBtn_5, &QPushButton::clicked, this, [this]() {
        QString matriculeFiscal;
        QString nomEntreprise;
        QString raisonSociale;
        QString typeFacture;
        QString description;
        double prixUnitaire = 0.0;
        int quantite = 0;

        if (!validerSaisieFacture(matriculeFiscal,
                                  nomEntreprise,
                                  raisonSociale,
                                  typeFacture,
                                  prixUnitaire,
                                  quantite,
                                  description)) {
            return;
        }

        const bool modeEdition = (m_idFactureEnEdition > 0);
        const int idFacture = modeEdition ? m_idFactureEnEdition : 0;
        QString signedBy = ui->usernameLineEdit ? ui->usernameLineEdit->text().trimmed() : QString();
        const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if (signedBy.isEmpty()) {
            signedBy = env.value("USERNAME").trimmed();
        }
        if (signedBy.isEmpty()) {
            signedBy = env.value("USER").trimmed();
        }
        bool signatureAccepted = false;
        const QString signatureDraw = demanderSignatureManuscrite(this, signedBy, &signatureAccepted);
        if (!signatureAccepted) {
            return;
        }
        if (signatureDraw.isEmpty()) {
            QMessageBox::warning(this,
                                 "Signature",
                                 "Impossible de capturer la signature. Reessayez.");
            return;
        }

        Facture facture(idFacture,
                        matriculeFiscal,
                        nomEntreprise,
                        raisonSociale,
                        typeFacture,
                        ui->startDateEdit_5->date(),
                        prixUnitaire,
                        quantite,
                        description,
                        m_idClientEnEdition,
                        modeEdition ? m_idFournisseurEnEdition : -1,
                        signedBy,
                        signatureDraw);

        const bool ok = modeEdition ? facture.modifier() : facture.ajouter();
        if (!ok) {
            QMessageBox::critical(this,
                                  "Erreur SQL",
                                  (modeEdition ? "Echec de la modification de la facture.\n"
                                               : "Echec de l'ajout de la facture.\n")
                                      + Facture::dernierErreur());
            return;
        }

        if (!modeEdition && facture.getIdFacture() > 0) {
            const QString signature = facture.getSignatureElec();
            QMessageBox::information(this,
                                     "Succes",
                                     QStringLiteral("Facture enregistree. No %1 (attribue par la base)\nSignature manuscrite: OK%2")
                                         .arg(facture.getIdFacture())
                                         .arg(signature.isEmpty()
                                                  ? QString()
                                                  : QStringLiteral("\nSignature: %1").arg(signature)));
        }

        viderFormulaireFacture();
        chargerFactures(ui->searchInput_5->text().trimmed());
        chargerCommandesClients();
        ui->VentesTab_2->setCurrentWidget(ui->addTab_5);
    });

    connect(ui->searchBtn_5, &QPushButton::clicked, this, [this]() {
        chargerFactures(ui->searchInput_5->text().trimmed());
    });

    connect(ui->searchInput_5, &QLineEdit::returnPressed, this, [this]() {
        chargerFactures(ui->searchInput_5->text().trimmed());
    });

    connect(ui->searchInput_5, &QLineEdit::textChanged, this, [this](const QString &text) {
        chargerFactures(text.trimmed());
    });

    connect(ui->sortBtn_5, &QPushButton::clicked, this, [this]() {
        if (!m_menuTriFacture || !ui->sortBtn_5) {
            return;
        }
        const QPoint p = ui->sortBtn_5->mapToGlobal(QPoint(0, ui->sortBtn_5->height()));
        // Report après le traitement du clic (meilleure prise en charge sous Windows / onglets)
        QTimer::singleShot(0, this, [this, p]() {
            if (m_menuTriFacture) {
                m_menuTriFacture->popup(p);
            }
        });
    });

    connect(ui->exportBtn_5, &QPushButton::clicked, this, [this]() {
        exporterFacturesPdf();
    });

    // ==================== CONFIGURATION FOURNISSEURS ====================

    ui->employeeTable_4->setRowCount(0);
    ui->employeeTable_4->setColumnCount(8);
    ui->employeeTable_4->setHorizontalHeaderLabels(
        {"ID", "Nom", "Region", "Email", "Nature", "Date ajout", "Status", "Telephone"});
    if (ui->departmentComboBox_4) {
        ui->departmentComboBox_4->blockSignals(true);
        ui->departmentComboBox_4->clear();
        ui->departmentComboBox_4->addItems({QStringLiteral("olive"), QStringLiteral("huile")});
        ui->departmentComboBox_4->setCurrentIndex(0);
        ui->departmentComboBox_4->blockSignals(false);
    }
    ui->employeeTable_4->setColumnWidth(7, 140);
    ui->modifyBtn_4->setEnabled(false);
    ui->deleteBtn_4->setEnabled(false);
    configurerTriAvanceFournisseur();
    initialiserStatistiquesFournisseur();
    if (ui->fournisseurAiChatLayout && !m_fournisseurChatWidget) {
        m_fournisseurChatWidget = new FournisseurChatWidget(QSqlDatabase::database(), ui->fournisseurAiChatTab);
        ui->fournisseurAiChatLayout->addWidget(m_fournisseurChatWidget);
    }
    connect(ui->VentesTab, &QTabWidget::currentChanged, this, [this](int index) {
        if (ui->VentesTab->widget(index) == ui->stattabvente) {
            mettreAJourStatistiquesFournisseur();
        }
    });

    connect(ui->searchBtn_4, &QPushButton::clicked, this, [this]() {
        filtrerFournisseurs(ui->searchInput_4->text().trimmed());
    });

    connect(ui->searchInput_4, &QLineEdit::returnPressed, this, [this]() {
        filtrerFournisseurs(ui->searchInput_4->text().trimmed());
    });

    connect(ui->searchInput_4, &QLineEdit::textChanged, this, [this](const QString &text) {
        filtrerFournisseurs(text.trimmed());
    });

    connect(ui->sortBtn_4, &QPushButton::clicked, this, [this]() {
        if (!m_menuTriFournisseur || !ui->sortBtn_4) {
            return;
        }
        const QPoint p = ui->sortBtn_4->mapToGlobal(QPoint(0, ui->sortBtn_4->height()));
        m_menuTriFournisseur->popup(p);
    });

    connect(ui->exportBtn_4, &QPushButton::clicked, this, [this]() {
        exporterFournisseursPdf();
    });

    connect(ui->employeeTable_4, &QTableWidget::itemSelectionChanged, this, [this]() {
        const bool hasSelection = !ui->employeeTable_4->selectedItems().isEmpty();
        ui->modifyBtn_4->setEnabled(hasSelection);
        ui->deleteBtn_4->setEnabled(hasSelection);
    });

    connect(ui->saveclientBtn_4, &QPushButton::clicked, this, [this]() {
        QString nom;
        QString region;
        QString email;
        QString type;
        QString status;
        QString telephone;
        QString dateAjout;

        if (!validerSaisieFournisseur(nom, region, email, type, status, telephone, dateAjout)) {
            return;
        }
        Q_UNUSED(dateAjout);

        const bool modeEdition = (m_idFournisseurEditionLocal > 0);
        const int idFournisseur = modeEdition ? m_idFournisseurEditionLocal : prochainIdFournisseur();
        if (idFournisseur <= 0) {
            return;
        }

        Fournisseur fournisseur(idFournisseur,
                                nom,
                                region,
                                email,
                                type,
                                ui->startDateEdit_4->date(),
                                status,
                                telephone);

        const bool ok = modeEdition ? fournisseur.modifier() : fournisseur.ajouter();
        if (!ok) {
            QMessageBox::critical(this,
                                  "Erreur SQL",
                                  (modeEdition ? "Echec de la modification du fournisseur.\n"
                                               : "Echec de l'ajout du fournisseur.\n")
                                      + Fournisseur::dernierErreur());
            return;
        }

        chargerFournisseurs();
        ui->employeeTable_4->sortItems(m_colTriFournisseur,
                                       m_triFournisseurAsc ? Qt::AscendingOrder : Qt::DescendingOrder);
        filtrerFournisseurs(ui->searchInput_4->text().trimmed());
        reinitialiserEditionFournisseur();
        ui->VentesTab->setCurrentWidget(ui->addTab_4);
    });

    connect(ui->deleteBtn_4, &QPushButton::clicked, this, [this]() {
        const int row = ui->employeeTable_4->currentRow();
        if (row < 0) return;

        QTableWidgetItem *idItem = ui->employeeTable_4->item(row, 0);
        bool idOk = false;
        const int id = idItem ? idItem->text().toInt(&idOk) : -1;
        if (!idOk) {
            QMessageBox::warning(this, "Selection", "ID fournisseur invalide.");
            return;
        }

        const auto confirmation = QMessageBox::question(
            this, "Suppression", "Voulez-vous supprimer ce fournisseur de la liste ?");
        if (confirmation != QMessageBox::Yes) return;

        if (!Fournisseur::supprimer(id)) {
            QMessageBox::critical(this, "Erreur SQL", "Echec de la suppression.\n" + Fournisseur::dernierErreur());
            return;
        }

        chargerFournisseurs();
        filtrerFournisseurs(ui->searchInput_4->text().trimmed());
        ui->modifyBtn_4->setEnabled(false);
        ui->deleteBtn_4->setEnabled(false);
        if (idOk && id == m_idFournisseurEditionLocal) {
            reinitialiserEditionFournisseur();
        }
    });

    connect(ui->modifyBtn_4, &QPushButton::clicked, this, [this]() {
        const int row = ui->employeeTable_4->currentRow();
        if (row < 0) return;

        auto value = [this, row](int col) -> QString {
            QTableWidgetItem *it = ui->employeeTable_4->item(row, col);
            return it ? it->text().trimmed() : QString();
        };

        bool idOk = false;
        const int id = value(0).toInt(&idOk);
        if (!idOk) {
            QMessageBox::warning(this, "Selection", "ID fournisseur invalide.");
            return;
        }
        m_idFournisseurEditionLocal = id;

        ui->emailEdit_8->setText(value(1));
        ui->emailEdit_4->setText(value(2));
        ui->emailEdit_5->setText(value(3));
        ui->emailEdit_16->setText(value(7));

        const int typeIndex = ui->departmentComboBox_4->findText(value(4));
        if (typeIndex >= 0) {
            ui->departmentComboBox_4->setCurrentIndex(typeIndex);
        }

        const int statusIndex = ui->departmentComboBox_6->findText(value(6));
        if (statusIndex >= 0) {
            ui->departmentComboBox_6->setCurrentIndex(statusIndex);
        }

        const QDate date = QDate::fromString(value(5), "yyyy-MM-dd");
        if (date.isValid()) {
            ui->startDateEdit_4->setDate(date);
        }

        ui->saveclientBtn_4->setText("Modifier");
        ui->VentesTab->setCurrentWidget(ui->addTab_4);
    });

    connect(ui->nnulerBtn_4, &QPushButton::clicked, this, [this]() {
        reinitialiserEditionFournisseur();
    });

    connect(ui->employeeTable_5, &QTableWidget::itemSelectionChanged, this, [this]() {
        const bool hasSelection = !ui->employeeTable_5->selectedItems().isEmpty();
        ui->modifyBtn_5->setEnabled(hasSelection);
        ui->deleteBtn_5->setEnabled(hasSelection);
    });

    connect(ui->employeeTable_5, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (row < 0) {
            return;
        }
        QTableWidgetItem *sigItem = ui->employeeTable_5->item(row, kFactureSignatureColumn);
        QTableWidgetItem *idItem = ui->employeeTable_5->item(row, 0);
        if (!sigItem || !idItem) {
            return;
        }
        const QString signatureDraw = sigItem->data(Qt::UserRole).toString();
        if (col == kFactureSignatureColumn) {
            afficherSignatureManuscrite(this, signatureDraw, idItem->text().trimmed());
            return;
        }
        if (col == kFactureQrColumn) {
            const QString publicUrl = sigItem->data(Qt::UserRole + 1).toString();
            const QString invoiceHtml = sigItem->data(Qt::UserRole + 2).toString();
            const QString localFilePath = sigItem->data(Qt::UserRole + 3).toString();
            afficherFactureQrDialog(this, publicUrl, idItem->text().trimmed(), invoiceHtml, localFilePath);
        }
    });

    connect(ui->modifyBtn_5, &QPushButton::clicked, this, [this]() {
        const int row = ui->employeeTable_5->currentRow();
        if (row < 0) return;

        QTableWidgetItem *idItem = ui->employeeTable_5->item(row, 0);
        if (!idItem) return;

        bool idOk = false;
        const int id = idItem->text().toInt(&idOk);
        if (!idOk) {
            QMessageBox::warning(this, "Selection", "ID facture invalide.");
            return;
        }

        chargerFactureParId(id);
        if (m_idFactureEnEdition > 0) {
            ui->VentesTab_2->setCurrentWidget(ui->addTab_5);
        }
    });

    connect(ui->deleteBtn_5, &QPushButton::clicked, this, [this]() {
        const int row = ui->employeeTable_5->currentRow();
        if (row < 0) return;

        QTableWidgetItem *idItem = ui->employeeTable_5->item(row, 0);
        if (!idItem) return;

        bool idOk = false;
        const int id = idItem->text().toInt(&idOk);
        if (!idOk) {
            QMessageBox::warning(this, "Selection", "ID facture invalide.");
            return;
        }

        const auto confirmation = QMessageBox::question(
            this, "Suppression", "Voulez-vous supprimer la facture ID " + QString::number(id) + " ?");
        if (confirmation != QMessageBox::Yes) return;

        if (!Facture::supprimer(id)) {
            QMessageBox::critical(this, "Erreur SQL", "Echec de la suppression.\n" + Facture::dernierErreur());
            return;
        }

        if (m_idFactureEnEdition == id) {
            viderFormulaireFacture();
        }
        chargerFactures(ui->searchInput_5->text().trimmed());
    });

    connect(ui->nnulerBtn_5, &QPushButton::clicked, this, [this]() {
        viderFormulaireFacture();
    });

    // ==================== CHARGEMENT INITIAL ====================

    viderFormulaireFacture();
    actualiserLibelleTri();
    chargerFactures();
    actualiserLibelleTriFournisseur();
    chargerFournisseurs();
    reinitialiserEditionFournisseur();
    filtrerFournisseurs(ui->searchInput_4->text().trimmed());

    // ==================== INITIALISATION STATISTIQUES ====================
    mettreAJourStatistiques();
}

void MainWindow::initialiserStatistiquesFinance()
{
    if (!ui->stattabvente_2) {
        return;
    }

    ui->salesStatsTitle_2->setText(QStringLiteral("Tableau de bord financier"));
    ui->salesStatsSubtitle_2->setText(QStringLiteral("Synthese des factures, tendances mensuelles et principaux comptes"));
    ui->salesRevenueTitle_2->setText(QStringLiteral("Somme totale clients"));
    ui->salesOrdersTitle_2->setText(QStringLiteral("Transactions clients"));
    ui->salesAovTitle_2->setText(QStringLiteral("Somme totale fournisseurs"));
    ui->salesTrendTitle_2->setText(QStringLiteral("Evolution du CA clients (6 derniers mois)"));
    ui->salesTrendSubtitle_2->setText(QStringLiteral("Base clients uniquement (hors fournisseurs)"));
    ui->salesTopProductsTitle_2->setText(QStringLiteral("Top comptes factures"));
    ui->salesTopProductsSubtitle_2->setText(QStringLiteral("Classement clients/fournisseurs par montant facture"));
    const QString valueGreenStyle = QStringLiteral("QLabel { color: #2e7d32; font-weight: 700; letter-spacing: 0.6px; }");
    ui->salesRevenueValue_2->setStyleSheet(valueGreenStyle);
    ui->salesOrdersValue_2->setStyleSheet(valueGreenStyle);
    ui->salesAovValue_2->setStyleSheet(valueGreenStyle);

    ui->stattabvente_2->setStyleSheet(QStringLiteral(
        "QWidget#stattabvente_2 { background: #f6f8fb; }"
        "QLabel { font-family: 'Poppins', 'Segoe UI', Arial, sans-serif; color: #0f172a; }"));

    auto setCardStyle = [](QWidget *card, const QString &accentColor) {
        if (!card) {
            return;
        }
        const QString selector = QStringLiteral("QWidget#%1").arg(card->objectName());
        card->setStyleSheet(QStringLiteral(
            "%1 {"
            " background-color: #ffffff;"
            " border: 1px solid #dbe4ea;"
            " border-left: 5px solid %2;"
            " border-radius: 12px;"
            "}").arg(selector, accentColor));
    };
    setCardStyle(ui->salesRevenueCard_2, QStringLiteral("#0f766e"));
    setCardStyle(ui->salesOrdersCard_2, QStringLiteral("#1d4ed8"));
    setCardStyle(ui->salesAovCard_2, QStringLiteral("#b45309"));

    // Carte dynamique: transactions fournisseurs (4e KPI)
    if (ui->salesKpiCardsLayout_2 && !ui->salesKpiCardsContainer_2->findChild<QWidget *>(QStringLiteral("salesSupplierTxCard_2"))) {
        auto *supplierTxCard = new QWidget(ui->salesKpiCardsContainer_2);
        supplierTxCard->setObjectName(QStringLiteral("salesSupplierTxCard_2"));
        supplierTxCard->setMinimumSize(220, 120);
        supplierTxCard->setStyleSheet(QStringLiteral(
            "QWidget#salesSupplierTxCard_2 {"
            " background-color: #ffffff;"
            " border: 1px solid #dbe4ea;"
            " border-left: 5px solid #7c3aed;"
            " border-radius: 12px;"
            "}"));

        auto *lay = new QVBoxLayout(supplierTxCard);
        lay->setContentsMargins(18, 18, 18, 18);
        lay->setSpacing(6);

        auto *title = new QLabel(QStringLiteral("Transactions fournisseurs"), supplierTxCard);
        title->setObjectName(QStringLiteral("salesSupplierTxTitle_2"));
        title->setStyleSheet(QStringLiteral("QLabel { color:#6c757d; font-weight:400; letter-spacing:0.4px; }"));
        lay->addWidget(title);

        auto *value = new QLabel(QStringLiteral("0"), supplierTxCard);
        value->setObjectName(QStringLiteral("salesSupplierTxValue_2"));
        value->setStyleSheet(QStringLiteral(
            "QLabel {"
            " color:#7c3aed;"
            " font-weight:700;"
            " letter-spacing:0.6px;"
            "}"));
        lay->addWidget(value);

        auto *trend = new QLabel(QStringLiteral("-"), supplierTxCard);
        trend->setObjectName(QStringLiteral("salesSupplierTxTrend_2"));
        trend->setStyleSheet(QStringLiteral("QLabel { color:#6c757d; font-weight:400; letter-spacing:0.3px; }"));
        lay->addWidget(trend);

        ui->salesKpiCardsLayout_2->addWidget(supplierTxCard);
    }

    if (auto *supplierTxValue = ui->salesKpiCardsContainer_2
                                    ? ui->salesKpiCardsContainer_2->findChild<QLabel *>(QStringLiteral("salesSupplierTxValue_2"))
                                    : nullptr) {
        if (ui->salesOrdersValue_2) {
            supplierTxValue->setFont(ui->salesOrdersValue_2->font());
        }
        supplierTxValue->setStyleSheet(QStringLiteral(
            "QLabel {"
            " color:#7c3aed;"
            " font-weight:700;"
            " letter-spacing:0.6px;"
            "}"));
    }

    if (ui->salesTrendPanel_2) {
        ui->salesTrendPanel_2->setStyleSheet(QStringLiteral(
            "QWidget#salesTrendPanel_2 { background: #ffffff; border: 1px solid #dbe4ea; border-radius: 12px; }"));
        ui->salesTrendPanel_2->setMinimumHeight(700);
    }
    if (ui->salesTopProductsPanel_2) {
        ui->salesTopProductsPanel_2->setStyleSheet(QStringLiteral(
            "QWidget#salesTopProductsPanel_2 {"
            " background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #f8fbf9);"
            " border: 1px solid #dbe4ea;"
            " border-radius: 12px;"
            "}"));
    }
    if (ui->salesTopProductsLayout_2) {
        ui->salesTopProductsLayout_2->setSpacing(8);
    }
    if (ui->salesTopProductsGridLayout_2) {
        ui->salesTopProductsGridLayout_2->setVerticalSpacing(10);
        ui->salesTopProductsGridLayout_2->setContentsMargins(0, 2, 0, 0);
        ui->salesTopProductsGridLayout_2->setAlignment(Qt::AlignTop);
    }
    if (ui->salesTopProductsTitle_2) {
        ui->salesTopProductsTitle_2->setStyleSheet(QStringLiteral(
            "QLabel { color:#14532d; font-weight:700; font-size:19px; letter-spacing:0.2px; }"));
    }
    if (ui->salesTopProductsSubtitle_2) {
        ui->salesTopProductsSubtitle_2->setStyleSheet(QStringLiteral(
            "QLabel { color:#4b5563; font-weight:500; font-size:12px; }"));
    }
    if (ui->salesTopProductsLayout_2 && ui->salesTopProductsPanel_2
        && !ui->salesTopProductsPanel_2->findChild<QWidget *>(QStringLiteral("salesRevenueSplitWrap_2"))) {
        auto *splitWrap = new QWidget(ui->salesTopProductsPanel_2);
        splitWrap->setObjectName(QStringLiteral("salesRevenueSplitWrap_2"));
        auto *splitLay = new QVBoxLayout(splitWrap);
        splitLay->setContentsMargins(0, 0, 0, 0);
        splitLay->setSpacing(4);

        auto *splitTitle = new QLabel(QStringLiteral("Repartition CA clients/fournisseurs"), splitWrap);
        splitTitle->setStyleSheet(QStringLiteral("QLabel { color:#334155; font-size:12px; font-weight:600; }"));
        splitLay->addWidget(splitTitle);

        auto *splitView = new QChartView(splitWrap);
        splitView->setObjectName(QStringLiteral("salesRevenueSplitPieView_2"));
        splitView->setRenderHint(QPainter::Antialiasing);
        splitView->setMinimumHeight(190);
        splitView->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
        splitLay->addWidget(splitView);

        ui->salesTopProductsLayout_2->insertWidget(2, splitWrap);
    }

    const QString greenProgressStyle = QStringLiteral(
        "QProgressBar {"
        " background: #e2e8f0;"
        " border: none;"
        " border-radius: 6px;"
        " min-height: 8px;"
        " max-height: 8px;"
        "}"
        "QProgressBar::chunk {"
        " background: #2e7d32;"
        " border-radius: 6px;"
        "}");
    const QString blueProgressStyle = QStringLiteral(
        "QProgressBar {"
        " background: #e2e8f0;"
        " border: none;"
        " border-radius: 6px;"
        " min-height: 8px;"
        " max-height: 8px;"
        "}"
        "QProgressBar::chunk {"
        " background: #2563eb;"
        " border-radius: 6px;"
        "}");

    const QList<QProgressBar *> monthBars = {
        ui->salesTrendJanBar_2,
        ui->salesTrendFevBar_2,
        ui->salesTrendMarBar_2,
        ui->salesTrendAprBar_2,
        ui->salesTrendMayBar_2,
        ui->salesTrendJunBar_2
    };
    for (QProgressBar *bar : monthBars) {
        if (!bar) {
            continue;
        }
        bar->setTextVisible(false);
        bar->setStyleSheet(greenProgressStyle);
    }

    const QList<QProgressBar *> topBars = {
        ui->salesTopProd1Bar_2,
        ui->salesTopProd2Bar_2,
        ui->salesTopProd3Bar_2,
        ui->salesTopProd4Bar_2
    };
    for (QProgressBar *bar : topBars) {
        if (!bar) {
            continue;
        }
        bar->setTextVisible(false);
        bar->setStyleSheet(QStringLiteral(
            "QProgressBar {"
            " background:#e2e8f0;"
            " border:none;"
            " border-radius:7px;"
            " min-height:10px;"
            " max-height:10px;"
            "}"
            "QProgressBar::chunk {"
            " background:#2563eb;"
            " border-radius:7px;"
            "}"));
    }

    const QList<QLabel *> topLabels = {
        ui->salesTopProd1Label_2,
        ui->salesTopProd2Label_2,
        ui->salesTopProd3Label_2,
        ui->salesTopProd4Label_2
    };
    for (QLabel *lbl : topLabels) {
        if (lbl) {
            lbl->setStyleSheet(QStringLiteral("QLabel { color:#1f2937; font-size:12px; font-weight:600; }"));
        }
    }

    const QList<QLabel *> topValues = {
        ui->salesTopProd1Value_2,
        ui->salesTopProd2Value_2,
        ui->salesTopProd3Value_2,
        ui->salesTopProd4Value_2
    };
    for (QLabel *lbl : topValues) {
        if (lbl) {
            lbl->setStyleSheet(QStringLiteral("QLabel { color:#334155; font-size:12px; font-weight:700; }"));
        }
    }
}

void MainWindow::setupFinanceAssistantTab()
{
    if (!ui->VentesTab_2) {
        return;
    }

    if (ui->VentesTab_2->findChild<QWidget *>(QStringLiteral("financeAssistantTab"))) {
        return;
    }

    auto *assistantTab = new QWidget(ui->VentesTab_2);
    assistantTab->setObjectName(QStringLiteral("financeAssistantTab"));
    assistantTab->setStyleSheet(QStringLiteral(
        "QWidget#financeAssistantTab {"
        " background: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
        " stop:0 #f2faf4, stop:0.52 #e5f4ea, stop:1 #dbeee2);"
        "}"
        "QPushButton#financeQuickBtn {"
        " background: rgba(255,255,255,0.65);"
        " border: 1px solid rgba(46, 125, 50, 0.35);"
        " border-radius: 12px;"
        " padding: 6px 12px;"
        " color: #2f4f35;"
        " font-size: 11px;"
        "}"
        "QPushButton#financeQuickBtn:hover { background: rgba(255,255,255,0.90); }"
        "QLineEdit#financeComposerInput {"
        " background: rgba(255,255,255,0.68);"
        " border: 1px solid rgba(148, 163, 184, 0.6);"
        " border-radius: 10px;"
        " padding: 10px 12px;"
        " color: #334155;"
        "}"
        "QPushButton#financeSendBtn {"
        " background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #3c9a52, stop:1 #2e7d32);"
        " border: 1px solid #2b6f2f;"
        " border-radius: 13px;"
        " min-width: 46px;"
        " min-height: 40px;"
        " max-width: 46px;"
        " max-height: 40px;"
        " color: #ffffff;"
        " font-weight: 700;"
        " font-size: 16px;"
        "}"
        "QPushButton#financeSendBtn:hover {"
        " background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #49ab5f, stop:1 #34893a);"
        "}"
        "QPushButton#financeSendBtn:pressed {"
        " background: #286d2d;"
        "}"));

    auto *mainLayout = new QVBoxLayout(assistantTab);
    mainLayout->setContentsMargins(140, 18, 140, 16);
    mainLayout->setSpacing(8);
    mainLayout->setAlignment(Qt::AlignTop);

    auto *sparkLabel = new QLabel(QStringLiteral("✦"), assistantTab);
    sparkLabel->setAlignment(Qt::AlignHCenter);
    sparkLabel->setStyleSheet(QStringLiteral("font-size: 22px; color: #2b1b2f;"));
    mainLayout->addWidget(sparkLabel, 0, Qt::AlignHCenter);

    auto *title = new QLabel(tr("ZITOUNATECH AI"), assistantTab);
    title->setAlignment(Qt::AlignHCenter);
    title->setStyleSheet(QStringLiteral("font-size: 33px; font-weight: 600; color: #2e7d32;"));
    mainLayout->addWidget(title, 0, Qt::AlignHCenter);

    // Keep content near the top of the tab.

    auto *composerLayout = new QHBoxLayout();
    composerLayout->setSpacing(8);
    auto *input = new QLineEdit(assistantTab);
    input->setObjectName(QStringLiteral("financeComposerInput"));
    input->setPlaceholderText(tr("Posez une question sur les factures, clients, fournisseurs ou la tresorerie..."));
    composerLayout->addWidget(input, 1);
    auto *sendButton = new QPushButton(QStringLiteral("➤"), assistantTab);
    sendButton->setObjectName(QStringLiteral("financeSendBtn"));
    sendButton->setCursor(Qt::PointingHandCursor);
    composerLayout->addWidget(sendButton);
    mainLayout->addLayout(composerLayout);

    auto *chatList = new QListWidget(assistantTab);
    chatList->setObjectName(QStringLiteral("financeChatList"));
    chatList->setWordWrap(true);
    chatList->setAlternatingRowColors(false);
    chatList->setSelectionMode(QAbstractItemView::NoSelection);
    chatList->setFocusPolicy(Qt::NoFocus);
    chatList->setFrameShape(QFrame::NoFrame);
    chatList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    chatList->setSpacing(8);
    chatList->setStyleSheet(
        QStringLiteral("QListWidget#financeChatList {"
                       " background: rgba(255,255,255,0.95);"
                       " border: 1px solid rgba(46,125,50,0.35);"
                       " border-radius: 12px;"
                       " padding: 8px;"
                       "}"
                       "QListWidget#financeChatList::item { margin: 0px; padding: 0px; border: none; }"
                       "QScrollBar:vertical {"
                       " background: transparent;"
                       " width: 10px;"
                       " margin: 6px 4px 6px 0px;"
                       "}"
                       "QScrollBar::handle:vertical {"
                       " background: rgba(46,125,50,0.45);"
                       " border-radius: 5px;"
                       " min-height: 24px;"
                       "}"
                       "QScrollBar::handle:vertical:hover { background: rgba(46,125,50,0.65); }"
                       "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
                       "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));
    chatList->setMinimumHeight(260);
    chatList->setMaximumHeight(380);
    mainLayout->addWidget(chatList, 0);

    auto *typingLabel = new QLabel(tr("OUR AI is typing..."), assistantTab);
    typingLabel->setStyleSheet(QStringLiteral("color: #7a8495; font-style: italic; font-size: 11px;"));
    typingLabel->setVisible(false);
    mainLayout->addWidget(typingLabel);
    mainLayout->addStretch(1);

    ui->VentesTab_2->addTab(assistantTab, tr("Assistant IA"));

    if (!m_financeAiClient) {
        m_financeAiClient = new FinanceAiClient(this);
    }

    connect(m_financeAiClient, &FinanceAiClient::responseReady, assistantTab, [this, chatList, typingLabel](const QString &answer) {
        typingLabel->setVisible(false);
        appendFinanceAssistantMessage(chatList, tr("Assistant"), answer);
    });
    connect(m_financeAiClient, &FinanceAiClient::requestFailed, assistantTab, [this, chatList, typingLabel](const QString &error) {
        typingLabel->setVisible(false);
        appendFinanceAssistantMessage(chatList,
                                      tr("Assistant"),
                                      error);
    });

    const auto sendMessage = [this, chatList, input, typingLabel]() {
        const QString question = input->text().trimmed();
        if (question.isEmpty()) {
            appendFinanceAssistantMessage(chatList,
                                          tr("Assistant"),
                                          tr("Le message est vide. Ecrivez une question avant d'envoyer."));
            return;
        }

        appendFinanceAssistantMessage(chatList, tr("Vous"), question);
        m_lastFinanceAssistantQuestion = question;
        input->clear();

        typingLabel->setVisible(true);

        if (!m_financeAiClient || !m_financeAiClient->hasApiKey()) {
            typingLabel->setVisible(false);
            appendFinanceAssistantMessage(
                chatList,
                tr("Assistant"),
                tr("Cle API Hugging Face introuvable. Configurez HUGGINGFACE_API_KEY dans votre .env."));
            return;
        }

        m_financeAiClient->sendQuestion(question, buildFinanceAssistantContext());
    };

    connect(sendButton, &QPushButton::clicked, assistantTab, sendMessage);
    connect(input, &QLineEdit::returnPressed, sendButton, &QPushButton::click);
}

void MainWindow::appendFinanceAssistantMessage(QListWidget *chatList, const QString &author, const QString &message) const
{
    if (!chatList) {
        return;
    }
    const bool isUser = (author == tr("Vous"));
    const QString role = isUser ? QStringLiteral("ME") : QStringLiteral("ZITOUNATECH AI");

    auto *item = new QListWidgetItem();
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);

    auto *container = new QWidget(chatList);
    auto *rowLayout = new QHBoxLayout(container);
    rowLayout->setContentsMargins(4, 2, 4, 2);
    rowLayout->setSpacing(0);

    auto *bubble = new QFrame(container);
    bubble->setObjectName(QStringLiteral("financeBubble"));
    bubble->setStyleSheet(
        isUser
            ? QStringLiteral("QFrame#financeBubble { background: #e8f7ea; border: 1px solid #bfe4c6; border-radius: 10px; }")
            : QStringLiteral("QFrame#financeBubble { background: #ffffff; border: 1px solid #d6e8d9; border-radius: 10px; }"));
    bubble->setMaximumWidth(chatList->viewport()->width() * 0.78);

    auto *bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(12, 8, 12, 8);
    bubbleLayout->setSpacing(4);

    auto *roleLabel = new QLabel(role, bubble);
    roleLabel->setStyleSheet(QStringLiteral("color: #2e7d32; font-weight: 700; font-size: 11px; letter-spacing: 0.3px;"));
    bubbleLayout->addWidget(roleLabel);

    QString prettyMessage = message;
    prettyMessage.replace(QStringLiteral("\n- "), QStringLiteral("\n - "));
    if (prettyMessage.startsWith(QStringLiteral("- "))) {
        prettyMessage.replace(0, 2, QStringLiteral(" - "));
    }

    auto *textLabel = new QLabel(prettyMessage, bubble);
    textLabel->setWordWrap(true);
    textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    textLabel->setStyleSheet(QStringLiteral("color: #1f2937; font-size: 14px; line-height: 1.35;"));
    bubbleLayout->addWidget(textLabel);

    if (isUser) {
        rowLayout->addStretch(1);
        rowLayout->addWidget(bubble, 0, Qt::AlignRight);
    } else {
        rowLayout->addWidget(bubble, 0, Qt::AlignLeft);
        rowLayout->addStretch(1);
    }

    item->setSizeHint(QSize(0, container->sizeHint().height() + 4));
    chatList->addItem(item);
    chatList->setItemWidget(item, container);
    chatList->scrollToBottom();
}

QString MainWindow::buildFinanceAssistantContext() const
{
    QStringList lines;

    QSqlQuery globalStats;
    if (globalStats.exec(QStringLiteral(
            "SELECT COUNT(*), NVL(SUM(PRIX_UNITAIRE * QUANTITE), 0), NVL(AVG(PRIX_UNITAIRE * QUANTITE), 0) "
            "FROM FACTURE"))
        && globalStats.next()) {
        lines << tr("- Factures total: %1").arg(globalStats.value(0).toInt());
        lines << tr("- Montant cumule: %1 TND").arg(globalStats.value(1).toString());
        lines << tr("- Ticket moyen: %1 TND").arg(globalStats.value(2).toString());
    }

    QSqlQuery monthly;
    if (monthly.exec(QStringLiteral(
            "SELECT "
            "NVL(SUM(CASE WHEN TRUNC(DATE_FACTURATION, 'MM') = TRUNC(SYSDATE, 'MM') "
            "THEN PRIX_UNITAIRE * QUANTITE END), 0), "
            "NVL(SUM(CASE WHEN TRUNC(DATE_FACTURATION, 'MM') = ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -1) "
            "THEN PRIX_UNITAIRE * QUANTITE END), 0) "
            "FROM FACTURE"))
        && monthly.next()) {
        lines << tr("- Montant mois courant: %1 TND").arg(monthly.value(0).toString());
        lines << tr("- Montant mois precedent: %1 TND").arg(monthly.value(1).toString());
    }

    QSqlQuery split;
    if (split.exec(QStringLiteral(
            "SELECT "
            "NVL(SUM(CASE WHEN ID_CLIENT IS NOT NULL THEN PRIX_UNITAIRE * QUANTITE ELSE 0 END), 0), "
            "NVL(SUM(CASE WHEN ID_FOURNISSEUR IS NOT NULL THEN PRIX_UNITAIRE * QUANTITE ELSE 0 END), 0) "
            "FROM FACTURE"))
        && split.next()) {
        lines << tr("- Flux client (ventes): %1 TND").arg(split.value(0).toString());
        lines << tr("- Flux fournisseur (achats): %1 TND").arg(split.value(1).toString());
    }

    QSqlQuery topCompanies;
    if (topCompanies.exec(QStringLiteral(
            "SELECT NOM_ENTREPRISE, NVL(SUM(PRIX_UNITAIRE * QUANTITE), 0) AS TOTAL "
            "FROM FACTURE "
            "GROUP BY NOM_ENTREPRISE "
            "ORDER BY TOTAL DESC "
            "FETCH FIRST 3 ROWS ONLY"))) {
        while (topCompanies.next()) {
            lines << tr("- Top entreprise: %1 => %2 TND")
                         .arg(topCompanies.value(0).toString().trimmed().isEmpty()
                                  ? QStringLiteral("-")
                                  : topCompanies.value(0).toString().trimmed(),
                              topCompanies.value(1).toString());
        }
    }

    QSqlQuery typeDist;
    if (typeDist.exec(QStringLiteral(
            "SELECT TYPE_FACTURE, COUNT(*) "
            "FROM FACTURE "
            "GROUP BY TYPE_FACTURE "
            "ORDER BY COUNT(*) DESC "
            "FETCH FIRST 3 ROWS ONLY"))) {
        while (typeDist.next()) {
            lines << tr("- Type dominant: %1 (%2 factures)")
                         .arg(typeDist.value(0).toString().trimmed().isEmpty()
                                  ? QStringLiteral("-")
                                  : typeDist.value(0).toString().trimmed(),
                              typeDist.value(1).toString());
        }
    }

    if (lines.isEmpty()) {
        lines << tr("- Aucune donnee financiere live disponible.");
        lines << tr("- Proposer une analyse prudente et des actions prioritaires.");
    }
    return lines.join('\n');
}

QString MainWindow::buildFinanceAssistantFallback(const QString &question) const
{
    const QString q = normalizeFinanceText(question);

    if (q.contains(QStringLiteral("budget"))) {
        return tr("Simulation: Fixez un budget mensuel par poste (achats, operationnel, reserve), "
                  "avec alerte a 80 pourcent et revue hebdomadaire des ecarts.");
    }
    if (q.contains(QStringLiteral("tresor")) || q.contains(QStringLiteral("cash")) || q.contains(QStringLiteral("flux"))) {
        return tr("Simulation: Suivez les encaissements clients vs decaissements fournisseurs par semaine, "
                  "et maintenez un coussin de tresorerie minimal de 1 mois de charges.");
    }
    if (q.contains(QStringLiteral("facture")) && (q.contains(QStringLiteral("impaye")) || q.contains(QStringLiteral("retard")))) {
        return tr("Simulation: Priorisez les relances sur les plus gros montants et les retards > 30 jours, "
                  "puis appliquez un plan de relance en 3 etapes: mail, appel, mise en demeure.");
    }
    if (q.contains(QStringLiteral("facture")) && q.contains(QStringLiteral("mois"))
        && (q.contains(QStringLiteral("total")) || q.contains(QStringLiteral("montant")) || q.contains(QStringLiteral("combien")))) {
        return buildMonthlyFactureTotalResponse();
    }
    if ((q.contains(QStringLiteral("top")) || q.contains(QStringLiteral("classement"))) && q.contains(QStringLiteral("fournisseur"))) {
        return buildTopFournisseursByAmountResponse();
    }
    if ((q.contains(QStringLiteral("top")) || q.contains(QStringLiteral("classement"))) && q.contains(QStringLiteral("client"))) {
        return buildTopClientsByAmountResponse();
    }
    if (isClientTransactionsThisMonthQuestion(question)) {
        return buildClientTransactionsThisMonthResponse();
    }
    if (isSupplierTransactionsThisMonthQuestion(question)) {
        return buildSupplierTransactionsThisMonthResponse();
    }
    if (isMonthComparisonQuestion(question)) {
        return buildMonthComparisonResponse();
    }
    if (isAverageTicketQuestion(question)) {
        return buildAverageTicketResponse();
    }
    if (isEvolutionClientsQuestion(question)) {
        return buildEvolutionClientsResponse();
    }
    if (isForecastQuestion(question)) {
        return buildForecastMonth1Response();
    }
    if (q.contains(QStringLiteral("fournisseur"))) {
        return tr("Simulation: Negociez les fournisseurs avec faible volume mais forte valeur, "
                  "et securisez 2 alternatives pour les categories critiques.");
    }
    if (q.contains(QStringLiteral("client")) || q.contains(QStringLiteral("vente"))) {
        return tr("Simulation: Concentrez les actions commerciales sur les 20 pourcent de clients qui generent "
                  "la plus grande part du chiffre d'affaires.");
    }
    return tr("Simulation: Je peux aider sur budget, tresorerie, factures, clients et fournisseurs. "
              "Posez une question finance plus ciblee.");
}

void MainWindow::mettreAJourStatistiquesFinance()
{
    if (!ui->stattabvente_2) {
        return;
    }

    double totalRevenue = 0.0;
    int totalInvoices = 0;

    QSqlQuery kpiQuery;
    if (kpiQuery.exec(QStringLiteral(
            "SELECT "
            "NVL(SUM(NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0)), 0), "
            "COUNT(*) "
            "FROM FACTURE "
            "WHERE %1").arg(sqlClientFacturePredicate())) &&
        kpiQuery.next()) {
        totalRevenue = kpiQuery.value(0).toDouble();
        totalInvoices = kpiQuery.value(1).toInt();
    }

    double monthRevenue = 0.0;
    double previousMonthRevenue = 0.0;
    int monthInvoices = 0;
    int previousMonthInvoices = 0;
    double supplierMonthRevenue = 0.0;
    double supplierPreviousMonthRevenue = 0.0;
    int supplierMonthInvoices = 0;
    int supplierPreviousMonthInvoices = 0;

    QSqlQuery monthQuery;
    if (monthQuery.exec(QStringLiteral(
            "SELECT "
            "NVL(SUM(CASE WHEN TRUNC(DATE_FACTURATION, 'MM') = TRUNC(SYSDATE, 'MM') "
            "THEN NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0) ELSE 0 END), 0) AS CURRENT_REVENUE, "
            "NVL(SUM(CASE WHEN TRUNC(DATE_FACTURATION, 'MM') = ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -1) "
            "THEN NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0) ELSE 0 END), 0) AS PREVIOUS_REVENUE, "
            "SUM(CASE WHEN TRUNC(DATE_FACTURATION, 'MM') = TRUNC(SYSDATE, 'MM') THEN 1 ELSE 0 END) AS CURRENT_COUNT, "
            "SUM(CASE WHEN TRUNC(DATE_FACTURATION, 'MM') = ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -1) THEN 1 ELSE 0 END) AS PREVIOUS_COUNT "
            "FROM FACTURE "
            "WHERE %1").arg(sqlClientFacturePredicate())) &&
        monthQuery.next()) {
        monthRevenue = monthQuery.value(0).toDouble();
        previousMonthRevenue = monthQuery.value(1).toDouble();
        monthInvoices = monthQuery.value(2).toInt();
        previousMonthInvoices = monthQuery.value(3).toInt();
    }

    QSqlQuery supplierMonthQuery;
    if (supplierMonthQuery.exec(QStringLiteral(
            "SELECT "
            "NVL(SUM(CASE WHEN TRUNC(DATE_FACTURATION, 'MM') = TRUNC(SYSDATE, 'MM') "
            "THEN NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0) ELSE 0 END), 0) AS CURRENT_REVENUE, "
            "NVL(SUM(CASE WHEN TRUNC(DATE_FACTURATION, 'MM') = ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -1) "
            "THEN NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0) ELSE 0 END), 0) AS PREVIOUS_REVENUE, "
            "SUM(CASE WHEN TRUNC(DATE_FACTURATION, 'MM') = TRUNC(SYSDATE, 'MM') THEN 1 ELSE 0 END) AS CURRENT_COUNT, "
            "SUM(CASE WHEN TRUNC(DATE_FACTURATION, 'MM') = ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -1) THEN 1 ELSE 0 END) AS PREVIOUS_COUNT "
            "FROM FACTURE "
            "WHERE %1").arg(sqlSupplierFacturePredicate())) &&
        supplierMonthQuery.next()) {
        supplierMonthRevenue = supplierMonthQuery.value(0).toDouble();
        supplierPreviousMonthRevenue = supplierMonthQuery.value(1).toDouble();
        supplierMonthInvoices = supplierMonthQuery.value(2).toInt();
        supplierPreviousMonthInvoices = supplierMonthQuery.value(3).toInt();
    }

    ui->salesRevenueValue_2->setText(formatCurrencyTnd(monthRevenue));
    ui->salesRevenueTrend_2->setText(trendText(monthRevenue, previousMonthRevenue));
    ui->salesRevenueTrend_2->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-weight: 600; }").arg(trendColor(monthRevenue, previousMonthRevenue)));

    ui->salesOrdersValue_2->setText(QString::number(totalInvoices));
    ui->salesOrdersTrend_2->setText(trendText(static_cast<double>(monthInvoices), static_cast<double>(previousMonthInvoices)));
    ui->salesOrdersTrend_2->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-weight: 600; }")
            .arg(trendColor(static_cast<double>(monthInvoices), static_cast<double>(previousMonthInvoices))));

    ui->salesAovValue_2->setText(formatCurrencyTnd(supplierMonthRevenue));
    ui->salesAovTrend_2->setText(trendText(supplierMonthRevenue, supplierPreviousMonthRevenue));
    ui->salesAovTrend_2->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-weight: 600; }")
            .arg(trendColor(supplierMonthRevenue, supplierPreviousMonthRevenue)));

    if (auto *supplierTxValue = ui->salesKpiCardsContainer_2->findChild<QLabel *>(QStringLiteral("salesSupplierTxValue_2"))) {
        supplierTxValue->setText(QString::number(supplierMonthInvoices));
    }
    if (auto *supplierTxTrend = ui->salesKpiCardsContainer_2->findChild<QLabel *>(QStringLiteral("salesSupplierTxTrend_2"))) {
        supplierTxTrend->setText(trendText(static_cast<double>(supplierMonthInvoices),
                                           static_cast<double>(supplierPreviousMonthInvoices)));
        supplierTxTrend->setStyleSheet(QStringLiteral("QLabel { color:%1; font-weight:600; }")
                                           .arg(trendColor(static_cast<double>(supplierMonthInvoices),
                                                           static_cast<double>(supplierPreviousMonthInvoices))));
    }

    QMap<QString, double> revenueByMonth;
    QSqlQuery monthlyRevenueQuery;
    monthlyRevenueQuery.prepare(QStringLiteral(
        "SELECT TO_CHAR(TRUNC(DATE_FACTURATION, 'MM'), 'YYYY-MM') AS MONTH_KEY, "
        "NVL(SUM(NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0)), 0) AS TOTAL "
        "FROM FACTURE "
        "WHERE DATE_FACTURATION >= ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -5) "
        "AND %1 "
        "GROUP BY TRUNC(DATE_FACTURATION, 'MM') "
        "ORDER BY TRUNC(DATE_FACTURATION, 'MM')").arg(sqlClientFacturePredicate()));
    if (monthlyRevenueQuery.exec()) {
        while (monthlyRevenueQuery.next()) {
            revenueByMonth.insert(monthlyRevenueQuery.value(0).toString(), monthlyRevenueQuery.value(1).toDouble());
        }
    }

    QVector<double> orderedRevenue;
    orderedRevenue.reserve(6);
    QDate monthCursor = QDate::currentDate().addMonths(-5);
    double maxRevenue = 0.0;
    for (int i = 0; i < 6; ++i) {
        const QString key = monthCursor.toString(QStringLiteral("yyyy-MM"));
        const double value = revenueByMonth.value(key, 0.0);
        orderedRevenue.push_back(value);
        if (value > maxRevenue) {
            maxRevenue = value;
        }
        monthCursor = monthCursor.addMonths(1);
    }

    // Prevision avancee (3 mois): regression lineaire sur les 6 derniers mois.
    const int historyCount = orderedRevenue.size();
    double slope = 0.0;
    double intercept = 0.0;
    QVector<double> forecastRevenue;
    forecastRevenue.reserve(3);
    double forecastMonth1 = 0.0;
    double forecast3MonthsTotal = 0.0;
    double forecastConfidencePct = 0.0;

    if (historyCount >= 2) {
        double sumX = 0.0;
        double sumY = 0.0;
        double sumXX = 0.0;
        double sumXY = 0.0;
        for (int i = 0; i < historyCount; ++i) {
            const double x = static_cast<double>(i);
            const double y = orderedRevenue[i];
            sumX += x;
            sumY += y;
            sumXX += x * x;
            sumXY += x * y;
        }
        const double denom = (historyCount * sumXX) - (sumX * sumX);
        if (!qFuzzyIsNull(denom)) {
            slope = ((historyCount * sumXY) - (sumX * sumY)) / denom;
            intercept = (sumY - slope * sumX) / static_cast<double>(historyCount);
        } else {
            intercept = historyCount > 0 ? (sumY / static_cast<double>(historyCount)) : 0.0;
        }

        for (int i = 0; i < 3; ++i) {
            const double x = static_cast<double>(historyCount + i);
            const double pred = qMax(0.0, intercept + slope * x);
            forecastRevenue.push_back(pred);
            forecast3MonthsTotal += pred;
            if (pred > maxRevenue) {
                maxRevenue = pred;
            }
        }
        forecastMonth1 = forecastRevenue.isEmpty() ? 0.0 : forecastRevenue.first();

        // Fiabilite simple basee sur l'erreur quadratique moyenne (RMSE) historique.
        double mse = 0.0;
        for (int i = 0; i < historyCount; ++i) {
            const double pred = qMax(0.0, intercept + slope * static_cast<double>(i));
            const double err = orderedRevenue[i] - pred;
            mse += err * err;
        }
        mse = historyCount > 0 ? (mse / static_cast<double>(historyCount)) : 0.0;
        const double rmse = std::sqrt(mse);
        const double avgRevenue = historyCount > 0 ? (std::accumulate(orderedRevenue.begin(), orderedRevenue.end(), 0.0) / historyCount) : 0.0;
        const double normalizedError = rmse / (avgRevenue + 1.0);
        forecastConfidencePct = qBound(35.0, 100.0 - (normalizedError * 100.0), 96.0);
    }

    if (ui->salesTrendSubtitle_2) {
        ui->salesTrendSubtitle_2->setText(
            QStringLiteral("Prevision M+1: %1 | Cumul 3 mois: %2 | Fiabilite: %3%")
                .arg(formatCompactCurrencyTnd(forecastMonth1))
                .arg(formatCompactCurrencyTnd(forecast3MonthsTotal))
                .arg(QString::number(forecastConfidencePct, 'f', 0)));
    }

    // Serie fournisseur (separee): historique 6 mois + prevision M+1.
    QMap<QString, double> supplierRevenueByMonth;
    QSqlQuery supplierMonthlyRevenueQuery;
    supplierMonthlyRevenueQuery.prepare(QStringLiteral(
        "SELECT TO_CHAR(TRUNC(DATE_FACTURATION, 'MM'), 'YYYY-MM') AS MONTH_KEY, "
        "NVL(SUM(NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0)), 0) AS TOTAL "
        "FROM FACTURE "
        "WHERE DATE_FACTURATION >= ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -5) "
        "AND %1 "
        "GROUP BY TRUNC(DATE_FACTURATION, 'MM') "
        "ORDER BY TRUNC(DATE_FACTURATION, 'MM')").arg(sqlSupplierFacturePredicate()));
    if (supplierMonthlyRevenueQuery.exec()) {
        while (supplierMonthlyRevenueQuery.next()) {
            supplierRevenueByMonth.insert(supplierMonthlyRevenueQuery.value(0).toString(),
                                          supplierMonthlyRevenueQuery.value(1).toDouble());
        }
    }

    QVector<double> supplierOrderedRevenue;
    supplierOrderedRevenue.reserve(6);
    QDate supplierMonthCursor = QDate::currentDate().addMonths(-5);
    for (int i = 0; i < 6; ++i) {
        const QString key = supplierMonthCursor.toString(QStringLiteral("yyyy-MM"));
        supplierOrderedRevenue.push_back(supplierRevenueByMonth.value(key, 0.0));
        supplierMonthCursor = supplierMonthCursor.addMonths(1);
    }

    const int supplierHistoryCount = supplierOrderedRevenue.size();
    double supplierSlope = 0.0;
    double supplierIntercept = 0.0;
    QVector<double> supplierForecastRevenue;
    supplierForecastRevenue.reserve(3);
    double supplierForecastMonth1 = 0.0;
    double supplierForecastConfidencePct = 0.0;

    if (supplierHistoryCount >= 2) {
        double sumX = 0.0;
        double sumY = 0.0;
        double sumXX = 0.0;
        double sumXY = 0.0;
        for (int i = 0; i < supplierHistoryCount; ++i) {
            const double x = static_cast<double>(i);
            const double y = supplierOrderedRevenue[i];
            sumX += x;
            sumY += y;
            sumXX += x * x;
            sumXY += x * y;
        }
        const double denom = (supplierHistoryCount * sumXX) - (sumX * sumX);
        if (!qFuzzyIsNull(denom)) {
            supplierSlope = ((supplierHistoryCount * sumXY) - (sumX * sumY)) / denom;
            supplierIntercept = (sumY - supplierSlope * sumX) / static_cast<double>(supplierHistoryCount);
        } else {
            supplierIntercept = supplierHistoryCount > 0 ? (sumY / static_cast<double>(supplierHistoryCount)) : 0.0;
        }

        for (int i = 0; i < 3; ++i) {
            const double x = static_cast<double>(supplierHistoryCount + i);
            const double pred = qMax(0.0, supplierIntercept + supplierSlope * x);
            supplierForecastRevenue.push_back(pred);
        }
        supplierForecastMonth1 = supplierForecastRevenue.isEmpty() ? 0.0 : supplierForecastRevenue.first();

        double mse = 0.0;
        for (int i = 0; i < supplierHistoryCount; ++i) {
            const double pred = qMax(0.0, supplierIntercept + supplierSlope * static_cast<double>(i));
            const double err = supplierOrderedRevenue[i] - pred;
            mse += err * err;
        }
        mse = supplierHistoryCount > 0 ? (mse / static_cast<double>(supplierHistoryCount)) : 0.0;
        const double rmse = std::sqrt(mse);
        const double avgRevenue = supplierHistoryCount > 0
            ? (std::accumulate(supplierOrderedRevenue.begin(), supplierOrderedRevenue.end(), 0.0) / supplierHistoryCount)
            : 0.0;
        const double normalizedError = rmse / (avgRevenue + 1.0);
        supplierForecastConfidencePct = qBound(35.0, 100.0 - (normalizedError * 100.0), 96.0);
    }

    // Carte KPI de prevision (M+1) dans le panneau des tendances.
    if (ui->salesTrendPanel_2 && ui->salesTrendLayout_2) {
        QFrame *forecastCard = ui->salesTrendPanel_2->findChild<QFrame *>(QStringLiteral("financeForecastKpiCard"));
        QLabel *forecastValueLabel = ui->salesTrendPanel_2->findChild<QLabel *>(QStringLiteral("financeForecastKpiValue"));
        QLabel *forecastMetaLabel = ui->salesTrendPanel_2->findChild<QLabel *>(QStringLiteral("financeForecastKpiMeta"));

        if (!forecastCard) {
            forecastCard = new QFrame(ui->salesTrendPanel_2);
            forecastCard->setObjectName(QStringLiteral("financeForecastKpiCard"));
            forecastCard->setStyleSheet(QStringLiteral(
                "QFrame#financeForecastKpiCard {"
                " background: #f8fff9;"
                " border: 1px solid #c8e6c9;"
                " border-left: 5px solid #2e7d32;"
                " border-radius: 10px;"
                "}"));

            auto *cardLayout = new QHBoxLayout(forecastCard);
            cardLayout->setContentsMargins(12, 10, 12, 10);
            cardLayout->setSpacing(12);

            auto *leftCol = new QVBoxLayout();
            leftCol->setSpacing(2);
            auto *titleLbl = new QLabel(QStringLiteral("Prevision mois prochain"), forecastCard);
            titleLbl->setStyleSheet(QStringLiteral("color:#166534; font-weight:700; font-size:13px;"));
            forecastValueLabel = new QLabel(QStringLiteral("0 TND"), forecastCard);
            forecastValueLabel->setObjectName(QStringLiteral("financeForecastKpiValue"));
            forecastValueLabel->setStyleSheet(QStringLiteral("color:#0f172a; font-weight:700; font-size:18px;"));
            leftCol->addWidget(titleLbl);
            leftCol->addWidget(forecastValueLabel);

            forecastMetaLabel = new QLabel(QStringLiteral("Fiabilite: 0%"), forecastCard);
            forecastMetaLabel->setObjectName(QStringLiteral("financeForecastKpiMeta"));
            forecastMetaLabel->setStyleSheet(QStringLiteral("color:#334155; font-size:12px; font-weight:600;"));

            cardLayout->addLayout(leftCol, 1);
            cardLayout->addWidget(forecastMetaLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
            ui->salesTrendLayout_2->insertWidget(0, forecastCard);
        }

        if (forecastValueLabel) {
            forecastValueLabel->setText(formatCurrencyTnd(forecastMonth1));
        }
        if (forecastMetaLabel) {
            const QString trendWord = slope > 0.01 ? QStringLiteral("Hausse")
                : (slope < -0.01 ? QStringLiteral("Baisse") : QStringLiteral("Stable"));
            const QString trendColor = slope > 0.01 ? QStringLiteral("#15803d")
                : (slope < -0.01 ? QStringLiteral("#b91c1c") : QStringLiteral("#64748b"));
            forecastMetaLabel->setText(QStringLiteral("%1 | Fiabilite %2%")
                                           .arg(trendWord, QString::number(forecastConfidencePct, 'f', 0)));
            forecastMetaLabel->setStyleSheet(QStringLiteral("color:%1; font-size:12px; font-weight:700;").arg(trendColor));
        }

        // Carte KPI fournisseur separee (achats fournisseurs)
        QFrame *supplierForecastCard = ui->salesTrendPanel_2->findChild<QFrame *>(QStringLiteral("financeSupplierForecastKpiCard"));
        QLabel *supplierForecastValueLabel = ui->salesTrendPanel_2->findChild<QLabel *>(QStringLiteral("financeSupplierForecastKpiValue"));
        QLabel *supplierForecastMetaLabel = ui->salesTrendPanel_2->findChild<QLabel *>(QStringLiteral("financeSupplierForecastKpiMeta"));

        if (!supplierForecastCard) {
            supplierForecastCard = new QFrame(ui->salesTrendPanel_2);
            supplierForecastCard->setObjectName(QStringLiteral("financeSupplierForecastKpiCard"));
            supplierForecastCard->setStyleSheet(QStringLiteral(
                "QFrame#financeSupplierForecastKpiCard {"
                " background: #fffaf5;"
                " border: 1px solid #fde2bf;"
                " border-left: 5px solid #f59e0b;"
                " border-radius: 10px;"
                "}"));

            auto *cardLayout = new QHBoxLayout(supplierForecastCard);
            cardLayout->setContentsMargins(12, 10, 12, 10);
            cardLayout->setSpacing(12);

            auto *leftCol = new QVBoxLayout();
            leftCol->setSpacing(2);
            auto *titleLbl = new QLabel(QStringLiteral("Prevision achats fournisseurs M+1"), supplierForecastCard);
            titleLbl->setStyleSheet(QStringLiteral("color:#9a6800; font-weight:700; font-size:13px;"));
            supplierForecastValueLabel = new QLabel(QStringLiteral("0 TND"), supplierForecastCard);
            supplierForecastValueLabel->setObjectName(QStringLiteral("financeSupplierForecastKpiValue"));
            supplierForecastValueLabel->setStyleSheet(QStringLiteral("color:#0f172a; font-weight:700; font-size:18px;"));
            leftCol->addWidget(titleLbl);
            leftCol->addWidget(supplierForecastValueLabel);

            supplierForecastMetaLabel = new QLabel(QStringLiteral("Fiabilite: 0%"), supplierForecastCard);
            supplierForecastMetaLabel->setObjectName(QStringLiteral("financeSupplierForecastKpiMeta"));
            supplierForecastMetaLabel->setStyleSheet(QStringLiteral("color:#334155; font-size:12px; font-weight:600;"));

            cardLayout->addLayout(leftCol, 1);
            cardLayout->addWidget(supplierForecastMetaLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
            ui->salesTrendLayout_2->insertWidget(1, supplierForecastCard);
        }

        if (supplierForecastValueLabel) {
            supplierForecastValueLabel->setText(formatCurrencyTnd(supplierForecastMonth1));
        }
        if (supplierForecastMetaLabel) {
            const QString trendWord = supplierSlope > 0.01 ? QStringLiteral("Hausse")
                : (supplierSlope < -0.01 ? QStringLiteral("Baisse") : QStringLiteral("Stable"));
            const QString metaColor = supplierSlope > 0.01 ? QStringLiteral("#15803d")
                : (supplierSlope < -0.01 ? QStringLiteral("#b91c1c") : QStringLiteral("#64748b"));
            supplierForecastMetaLabel->setText(QStringLiteral("%1 | Fiabilite %2%")
                                                   .arg(trendWord, QString::number(supplierForecastConfidencePct, 'f', 0)));
            supplierForecastMetaLabel->setStyleSheet(QStringLiteral("color:%1; font-size:12px; font-weight:700;").arg(metaColor));
        }
    }

    if (ui->salesTrendBars_2) {
        ui->salesTrendBars_2->setVisible(false);
    }

    // Courbe du chiffre d'affaires: historique (6 mois) + prevision (3 mois)
    if (ui->salesTrendPanel_2 && ui->salesTrendLayout_2) {
        QChartView *revenueCurveView =
            ui->salesTrendPanel_2->findChild<QChartView *>(QStringLiteral("financeRevenueCurveView"));
        if (!revenueCurveView) {
            revenueCurveView = new QChartView(ui->salesTrendPanel_2);
            revenueCurveView->setObjectName(QStringLiteral("financeRevenueCurveView"));
            revenueCurveView->setRenderHint(QPainter::Antialiasing);
            revenueCurveView->setMinimumHeight(210);
            revenueCurveView->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
            ui->salesTrendLayout_2->addWidget(revenueCurveView);
        }

        auto *series = new QSplineSeries();
        QPen curvePen(QColor("#2e7d32"));
        curvePen.setWidth(3);
        series->setPen(curvePen);
        series->setPointsVisible(true);

        auto *forecastSeries = new QLineSeries();
        QPen forecastPen(QColor("#f59e0b"));
        forecastPen.setWidth(2);
        forecastPen.setStyle(Qt::DashLine);
        forecastSeries->setPen(forecastPen);
        forecastSeries->setPointsVisible(true);

        QVector<qint64> xPoints;
        xPoints.reserve(9);
        monthCursor = QDate::currentDate().addMonths(-5);
        for (int i = 0; i < orderedRevenue.size(); ++i) {
            const QDate d(monthCursor.year(), monthCursor.month(), 1);
            const qint64 x = QDateTime(d, QTime(0, 0)).toMSecsSinceEpoch();
            xPoints.push_back(x);
            series->append(static_cast<qreal>(x), orderedRevenue[i]);
            monthCursor = monthCursor.addMonths(1);
        }

        if (!xPoints.isEmpty()) {
            // point de continuité depuis le dernier réel vers les prévisions
            forecastSeries->append(static_cast<qreal>(xPoints.last()), orderedRevenue.last());
        }
        for (int i = 0; i < forecastRevenue.size(); ++i) {
            const QDate d(QDate::currentDate().addMonths(i + 1).year(), QDate::currentDate().addMonths(i + 1).month(), 1);
            const qint64 x = QDateTime(d, QTime(0, 0)).toMSecsSinceEpoch();
            xPoints.push_back(x);
            forecastSeries->append(static_cast<qreal>(x), forecastRevenue[i]);
        }

        auto *chart = new QChart();
        chart->addSeries(series);
        chart->addSeries(forecastSeries);
        chart->legend()->hide();
        chart->setBackgroundVisible(false);
        chart->setPlotAreaBackgroundVisible(false);
        chart->setAnimationOptions(QChart::SeriesAnimations);

        auto *axisX = new QDateTimeAxis();
        axisX->setFormat(QStringLiteral("MMM yy"));
        axisX->setTickCount(8);
        axisX->setGridLineVisible(false);
        axisX->setLabelsColor(QColor("#64748b"));
        if (!xPoints.isEmpty()) {
            axisX->setRange(QDateTime::fromMSecsSinceEpoch(xPoints.first()),
                            QDateTime::fromMSecsSinceEpoch(xPoints.last()));
        }

        auto *axisY = new QValueAxis();
        axisY->setLabelFormat(QStringLiteral("%.0f"));
        axisY->setTickCount(5);
        axisY->setLabelsColor(QColor("#64748b"));
        axisY->setGridLineColor(QColor("#e2e8f0"));
        axisY->setRange(0.0, maxRevenue > 0.0 ? (maxRevenue * 1.2) : 10.0);

        chart->addAxis(axisX, Qt::AlignBottom);
        chart->addAxis(axisY, Qt::AlignLeft);
        series->attachAxis(axisX);
        series->attachAxis(axisY);
        forecastSeries->attachAxis(axisX);
        forecastSeries->attachAxis(axisY);

        QChart *oldChart = revenueCurveView->chart();
        revenueCurveView->setChart(chart);
        if (oldChart) {
            delete oldChart;
        }

        // Courbe des achats fournisseurs: historique (6 mois) + prevision (3 mois)
        QLabel *supplierCurveTitle =
            ui->salesTrendPanel_2->findChild<QLabel *>(QStringLiteral("financeSupplierCurveTitle"));
        if (!supplierCurveTitle) {
            supplierCurveTitle = new QLabel(QStringLiteral("Evolution des achats fournisseurs (6 mois + prevision)"),
                                            ui->salesTrendPanel_2);
            supplierCurveTitle->setObjectName(QStringLiteral("financeSupplierCurveTitle"));
            supplierCurveTitle->setStyleSheet(QStringLiteral("QLabel { color:#7c5b05; font-weight:700; font-size:12px; }"));
            ui->salesTrendLayout_2->addWidget(supplierCurveTitle);
        }

        QChartView *supplierCurveView =
            ui->salesTrendPanel_2->findChild<QChartView *>(QStringLiteral("financeSupplierCurveView"));
        if (!supplierCurveView) {
            supplierCurveView = new QChartView(ui->salesTrendPanel_2);
            supplierCurveView->setObjectName(QStringLiteral("financeSupplierCurveView"));
            supplierCurveView->setRenderHint(QPainter::Antialiasing);
            supplierCurveView->setMinimumHeight(185);
            supplierCurveView->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
            ui->salesTrendLayout_2->addWidget(supplierCurveView);
        }

        auto *supplierSeries = new QSplineSeries();
        QPen supplierPen(QColor("#d97706"));
        supplierPen.setWidth(3);
        supplierSeries->setPen(supplierPen);
        supplierSeries->setPointsVisible(true);

        auto *supplierForecastSeries = new QLineSeries();
        QPen supplierForecastPen(QColor("#7c3aed"));
        supplierForecastPen.setWidth(2);
        supplierForecastPen.setStyle(Qt::DashLine);
        supplierForecastSeries->setPen(supplierForecastPen);
        supplierForecastSeries->setPointsVisible(true);

        QVector<qint64> supplierXPoints;
        supplierXPoints.reserve(9);
        QDate supplierCursor = QDate::currentDate().addMonths(-5);
        double supplierMaxRevenue = 0.0;
        for (int i = 0; i < supplierOrderedRevenue.size(); ++i) {
            const QDate d(supplierCursor.year(), supplierCursor.month(), 1);
            const qint64 x = QDateTime(d, QTime(0, 0)).toMSecsSinceEpoch();
            const double y = supplierOrderedRevenue[i];
            supplierXPoints.push_back(x);
            supplierSeries->append(static_cast<qreal>(x), y);
            if (y > supplierMaxRevenue) {
                supplierMaxRevenue = y;
            }
            supplierCursor = supplierCursor.addMonths(1);
        }

        if (!supplierXPoints.isEmpty()) {
            supplierForecastSeries->append(static_cast<qreal>(supplierXPoints.last()), supplierOrderedRevenue.last());
        }
        for (int i = 0; i < supplierForecastRevenue.size(); ++i) {
            const QDate d(QDate::currentDate().addMonths(i + 1).year(), QDate::currentDate().addMonths(i + 1).month(), 1);
            const qint64 x = QDateTime(d, QTime(0, 0)).toMSecsSinceEpoch();
            const double y = supplierForecastRevenue[i];
            supplierXPoints.push_back(x);
            supplierForecastSeries->append(static_cast<qreal>(x), y);
            if (y > supplierMaxRevenue) {
                supplierMaxRevenue = y;
            }
        }

        auto *supplierChart = new QChart();
        supplierChart->addSeries(supplierSeries);
        supplierChart->addSeries(supplierForecastSeries);
        supplierChart->legend()->hide();
        supplierChart->setBackgroundVisible(false);
        supplierChart->setPlotAreaBackgroundVisible(false);
        supplierChart->setAnimationOptions(QChart::SeriesAnimations);

        auto *supplierAxisX = new QDateTimeAxis();
        supplierAxisX->setFormat(QStringLiteral("MMM yy"));
        supplierAxisX->setTickCount(8);
        supplierAxisX->setGridLineVisible(false);
        supplierAxisX->setLabelsColor(QColor("#64748b"));
        if (!supplierXPoints.isEmpty()) {
            supplierAxisX->setRange(QDateTime::fromMSecsSinceEpoch(supplierXPoints.first()),
                                    QDateTime::fromMSecsSinceEpoch(supplierXPoints.last()));
        }

        auto *supplierAxisY = new QValueAxis();
        supplierAxisY->setLabelFormat(QStringLiteral("%.0f"));
        supplierAxisY->setTickCount(5);
        supplierAxisY->setLabelsColor(QColor("#64748b"));
        supplierAxisY->setGridLineColor(QColor("#e2e8f0"));
        supplierAxisY->setRange(0.0, supplierMaxRevenue > 0.0 ? (supplierMaxRevenue * 1.2) : 10.0);

        supplierChart->addAxis(supplierAxisX, Qt::AlignBottom);
        supplierChart->addAxis(supplierAxisY, Qt::AlignLeft);
        supplierSeries->attachAxis(supplierAxisX);
        supplierSeries->attachAxis(supplierAxisY);
        supplierForecastSeries->attachAxis(supplierAxisX);
        supplierForecastSeries->attachAxis(supplierAxisY);

        QChart *oldSupplierChart = supplierCurveView->chart();
        supplierCurveView->setChart(supplierChart);
        if (oldSupplierChart) {
            delete oldSupplierChart;
        }
    }

    QVector<QPair<QString, double>> topCompanies;
    topCompanies.reserve(4);

    QSqlQuery topClientsQuery;
    topClientsQuery.prepare(QStringLiteral(
        "SELECT COMPANY_NAME, TOTAL_REVENUE "
        "FROM ("
        "  SELECT NVL(NOM_ENTREPRISE, '(Sans nom)') AS COMPANY_NAME, "
        "         NVL(SUM(NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0)), 0) AS TOTAL_REVENUE "
        "  FROM FACTURE "
        "  WHERE %1 "
        "  GROUP BY NVL(NOM_ENTREPRISE, '(Sans nom)') "
        "  ORDER BY TOTAL_REVENUE DESC"
        ") "
        "WHERE ROWNUM <= 2").arg(sqlClientFacturePredicate()));
    if (topClientsQuery.exec()) {
        while (topClientsQuery.next()) {
            topCompanies.push_back({QStringLiteral("[Client] ") + topClientsQuery.value(0).toString(),
                                    topClientsQuery.value(1).toDouble()});
        }
    }

    QSqlQuery topSuppliersQuery;
    topSuppliersQuery.prepare(QStringLiteral(
        "SELECT COMPANY_NAME, TOTAL_REVENUE "
        "FROM ("
        "  SELECT NVL(NOM_ENTREPRISE, '(Sans nom)') AS COMPANY_NAME, "
        "         NVL(SUM(NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0)), 0) AS TOTAL_REVENUE "
        "  FROM FACTURE "
        "  WHERE %1 "
        "  GROUP BY NVL(NOM_ENTREPRISE, '(Sans nom)') "
        "  ORDER BY TOTAL_REVENUE DESC"
        ") "
        "WHERE ROWNUM <= 2").arg(sqlSupplierFacturePredicate()));
    if (topSuppliersQuery.exec()) {
        while (topSuppliersQuery.next()) {
            topCompanies.push_back({QStringLiteral("[Fourn.] ") + topSuppliersQuery.value(0).toString(),
                                    topSuppliersQuery.value(1).toDouble()});
        }
    }

    QSqlQuery combinedRevenueQuery;
    double combinedRevenue = totalRevenue;
    double supplierTotalRevenue = 0.0;
    if (combinedRevenueQuery.exec(QStringLiteral(
            "SELECT NVL(SUM(NVL(PRIX_UNITAIRE,0) * NVL(QUANTITE,0)), 0) "
            "FROM FACTURE "
            "WHERE %1").arg(sqlSupplierFacturePredicate()))
        && combinedRevenueQuery.next()) {
        supplierTotalRevenue = combinedRevenueQuery.value(0).toDouble();
        combinedRevenue += supplierTotalRevenue;
    }

    if (ui->salesTopProductsPanel_2) {
        if (auto *splitView = ui->salesTopProductsPanel_2->findChild<QChartView *>(QStringLiteral("salesRevenueSplitPieView_2"))) {
            auto *series = new QPieSeries();
            series->setHoleSize(0.52);
            const double clientRevenuePct = combinedRevenue > 0.0 ? (totalRevenue / combinedRevenue) * 100.0 : 0.0;
            const double supplierRevenuePct = combinedRevenue > 0.0 ? (supplierTotalRevenue / combinedRevenue) * 100.0 : 0.0;
            const double clientSliceValue = combinedRevenue > 0.0 ? qMax(0.0, totalRevenue) : 1.0;
            const double supplierSliceValue = combinedRevenue > 0.0 ? qMax(0.0, supplierTotalRevenue) : 1.0;
            auto *clientsSlice = series->append(
                QStringLiteral("Clients %1%").arg(QString::number(clientRevenuePct, 'f', 1)), clientSliceValue);
            auto *suppliersSlice = series->append(
                QStringLiteral("Fournisseurs %1%").arg(QString::number(supplierRevenuePct, 'f', 1)), supplierSliceValue);
            clientsSlice->setColor(QColor("#2563eb"));
            clientsSlice->setLabelVisible(true);
            suppliersSlice->setColor(QColor("#2e7d32"));
            suppliersSlice->setLabelVisible(true);

            auto *splitChart = new QChart();
            splitChart->addSeries(series);
            splitChart->setBackgroundVisible(false);
            splitChart->setPlotAreaBackgroundVisible(false);
            splitChart->setMargins(QMargins(0, 0, 0, 0));
            splitChart->legend()->setAlignment(Qt::AlignBottom);
            splitChart->legend()->setLabelColor(QColor("#334155"));

            QChart *old = splitView->chart();
            splitView->setChart(splitChart);
            if (old) {
                delete old;
            }
        }
    }

    const QList<QLabel *> companyLabels = {
        ui->salesTopProd1Label_2,
        ui->salesTopProd2Label_2,
        ui->salesTopProd3Label_2,
        ui->salesTopProd4Label_2
    };
    const QList<QProgressBar *> companyBars = {
        ui->salesTopProd1Bar_2,
        ui->salesTopProd2Bar_2,
        ui->salesTopProd3Bar_2,
        ui->salesTopProd4Bar_2
    };
    const QList<QLabel *> companyValues = {
        ui->salesTopProd1Value_2,
        ui->salesTopProd2Value_2,
        ui->salesTopProd3Value_2,
        ui->salesTopProd4Value_2
    };

    const auto topBarStyle = [](bool isSupplier) {
        const QString chunkColor = isSupplier ? QStringLiteral("#2e7d32") : QStringLiteral("#2563eb");
        return QStringLiteral(
                   "QProgressBar {"
                   " background:#e2e8f0;"
                   " border:none;"
                   " border-radius:7px;"
                   " min-height:10px;"
                   " max-height:10px;"
                   "}"
                   "QProgressBar::chunk {"
                   " background:%1;"
                   " border-radius:7px;"
                   "}").arg(chunkColor);
    };

    const QLocale frLocale(QLocale::French, QLocale::Tunisia);

    for (int i = 0; i < companyLabels.size(); ++i) {
        if (i < topCompanies.size() && combinedRevenue > 0.0) {
            QString companyName = topCompanies[i].first.trimmed();
            const bool isSupplier = companyName.startsWith(QStringLiteral("[Fourn.]"));
            const bool isClient = companyName.startsWith(QStringLiteral("[Client]"));
            if (isSupplier) {
                companyName.remove(0, QStringLiteral("[Fourn.]").size());
                companyName = QStringLiteral("Fournisseur | ") + companyName.trimmed();
            } else if (isClient) {
                companyName.remove(0, QStringLiteral("[Client]").size());
                companyName = QStringLiteral("Client | ") + companyName.trimmed();
            }
            if (companyName.isEmpty()) {
                companyName = QStringLiteral("(Sans nom)");
            }
            if (companyName.size() > 34) {
                companyName = companyName.left(31) + QStringLiteral("...");
            }
            const double amount = topCompanies[i].second;
            const double share = (amount / combinedRevenue) * 100.0;
            companyLabels[i]->setText(companyName);
            companyBars[i]->setValue(qBound(0, qRound(share), 100));
            companyBars[i]->setStyleSheet(topBarStyle(isSupplier));
            companyValues[i]->setText(
                QStringLiteral("%1%  •  %2 TND")
                    .arg(QString::number(share, 'f', 1))
                    .arg(frLocale.toString(amount, 'f', 2)));
        } else {
            companyLabels[i]->setText(QStringLiteral("-"));
            companyBars[i]->setValue(0);
            companyBars[i]->setStyleSheet(topBarStyle(false));
            companyValues[i]->setText(QStringLiteral("0.0%"));
        }
    }
}

void MainWindow::initialiserStatistiquesFournisseur()
{
    if (!ui->stattabvente) {
        return;
    }
    ui->stattabvente->setStyleSheet(QStringLiteral(
        "QWidget#stattabvente { background: #f6f8fb; }"
        "QLabel { font-family: 'Poppins', 'Segoe UI', Arial, sans-serif; color: #0f172a; }"));

    auto *rootLayout = qobject_cast<QVBoxLayout *>(ui->stattabvente->layout());
    if (!rootLayout) {
        rootLayout = new QVBoxLayout(ui->stattabvente);
        rootLayout->setSpacing(16);
        rootLayout->setContentsMargins(12, 12, 12, 12);
    }
    if (ui->stattabvente->findChild<QLabel *>(QStringLiteral("supplierStatsTitle"))) {
        return;
    }

    auto mkLabel = [](const QString &name, const QString &text, int pt, QFont::Weight weight, const QString &color) {
        auto *lbl = new QLabel(text);
        lbl->setObjectName(name);
        QFont f(QStringLiteral("Poppins"), pt);
        f.setWeight(weight);
        lbl->setFont(f);
        lbl->setStyleSheet(QStringLiteral("QLabel { color:%1; }").arg(color));
        return lbl;
    };

    rootLayout->addWidget(mkLabel(QStringLiteral("supplierStatsTitle"),
                                  QStringLiteral("Tableau de bord fournisseurs - niveau professionnel"),
                                  16,
                                  QFont::DemiBold,
                                  QStringLiteral("#0f172a")));
    rootLayout->addWidget(mkLabel(QStringLiteral("supplierStatsSubtitle"),
                                  QStringLiteral("Performance, risque, segmentation nature et classification automatique"),
                                  10,
                                  QFont::Normal,
                                  QStringLiteral("#64748b")));

    auto mkCard = [&](const QString &cardName,
                      const QString &titleName,
                      const QString &valueName,
                      const QString &trendName,
                      const QString &titleText,
                      const QString &accent) {
        auto *card = new QFrame(ui->stattabvente);
        card->setObjectName(cardName);
        card->setStyleSheet(QStringLiteral("QFrame#%1 { background:#ffffff; border:1px solid #dbe4ea; border-left:5px solid %2; border-radius:10px; }")
                                .arg(cardName, accent));
        auto *lay = new QVBoxLayout(card);
        lay->setContentsMargins(12, 10, 12, 10);
        lay->setSpacing(4);
        lay->addWidget(mkLabel(titleName, titleText, 9, QFont::Normal, QStringLiteral("#64748b")));
        lay->addWidget(mkLabel(valueName, QStringLiteral("-"), 16, QFont::DemiBold, accent));
        lay->addWidget(mkLabel(trendName, QStringLiteral("-"), 9, QFont::Medium, QStringLiteral("#334155")));
        return card;
    };

    auto *cardsRow = new QWidget(ui->stattabvente);
    auto *cardsLay = new QHBoxLayout(cardsRow);
    cardsLay->setContentsMargins(0, 0, 0, 0);
    cardsLay->setSpacing(12);
    cardsLay->addWidget(mkCard(QStringLiteral("supplierCardTotal"),
                               QStringLiteral("supplierKpiTotalTitle"),
                               QStringLiteral("supplierKpiTotalValue"),
                               QStringLiteral("supplierKpiTotalTrend"),
                               QStringLiteral("Total fournisseurs"),
                               QStringLiteral("#0f766e")));
    cardsLay->addWidget(mkCard(QStringLiteral("supplierCardScore"),
                               QStringLiteral("supplierKpiScoreTitle"),
                               QStringLiteral("supplierKpiScoreValue"),
                               QStringLiteral("supplierKpiScoreTrend"),
                               QStringLiteral("Score moyen portefeuille"),
                               QStringLiteral("#1d4ed8")));
    cardsLay->addWidget(mkCard(QStringLiteral("supplierCardRisk"),
                               QStringLiteral("supplierKpiRiskTitle"),
                               QStringLiteral("supplierKpiRiskValue"),
                               QStringLiteral("supplierKpiRiskTrend"),
                               QStringLiteral("Risque operationnel"),
                               QStringLiteral("#b45309")));
    rootLayout->addWidget(cardsRow);

    auto *trendPanel = new QFrame(ui->stattabvente);
    trendPanel->setStyleSheet(QStringLiteral("QFrame { background:#ffffff; border:1px solid #dbe4ea; border-radius:10px; }"));
    auto *trendLay = new QVBoxLayout(trendPanel);
    trendLay->setContentsMargins(12, 12, 12, 12);
    trendLay->setSpacing(8);
    trendLay->addWidget(mkLabel(QStringLiteral("supplierTrendTitle"),
                                QStringLiteral("Onboarding fournisseurs (6 derniers mois)"),
                                11,
                                QFont::DemiBold,
                                QStringLiteral("#0f172a")));
    for (int i = 0; i < 6; ++i) {
        auto *row = new QWidget(trendPanel);
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->setSpacing(8);
        auto *mLbl = mkLabel(QStringLiteral("supplierMonthLabel%1").arg(i), QStringLiteral("M%1").arg(i + 1), 9, QFont::Medium, QStringLiteral("#334155"));
        mLbl->setMinimumWidth(36);
        auto *bar = new QProgressBar(row);
        bar->setObjectName(QStringLiteral("supplierMonthBar%1").arg(i));
        bar->setTextVisible(false);
        bar->setRange(0, 100);
        bar->setStyleSheet(QStringLiteral(
            "QProgressBar { background:#e2e8f0; border:none; border-radius:6px; min-height:8px; max-height:8px; }"
            "QProgressBar::chunk { background:#2e7d32; border-radius:6px; }"));
        auto *vLbl = mkLabel(QStringLiteral("supplierMonthValue%1").arg(i), QStringLiteral("0"), 9, QFont::Medium, QStringLiteral("#334155"));
        vLbl->setMinimumWidth(40);
        rowLay->addWidget(mLbl);
        rowLay->addWidget(bar, 1);
        rowLay->addWidget(vLbl);
        trendLay->addWidget(row);
    }
    rootLayout->addWidget(trendPanel);

    auto *classPanel = new QFrame(ui->stattabvente);
    classPanel->setStyleSheet(QStringLiteral("QFrame { background:#ffffff; border:1px solid #dbe4ea; border-radius:10px; }"));
    auto *classLay = new QVBoxLayout(classPanel);
    classLay->setContentsMargins(12, 12, 12, 12);
    classLay->setSpacing(8);
    classLay->addWidget(mkLabel(QStringLiteral("supplierClassTitle"),
                                QStringLiteral("Classification automatique"),
                                11,
                                QFont::DemiBold,
                                QStringLiteral("#0f172a")));
    for (int i = 0; i < 4; ++i) {
        auto *row = new QWidget(classPanel);
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->setSpacing(8);
        auto *cLbl = mkLabel(QStringLiteral("supplierClassLabel%1").arg(i), QStringLiteral("-"), 9, QFont::Medium, QStringLiteral("#334155"));
        cLbl->setMinimumWidth(180);
        auto *bar = new QProgressBar(row);
        bar->setObjectName(QStringLiteral("supplierClassBar%1").arg(i));
        bar->setTextVisible(false);
        bar->setRange(0, 100);
        bar->setStyleSheet(QStringLiteral(
            "QProgressBar { background:#e2e8f0; border:none; border-radius:6px; min-height:8px; max-height:8px; }"
            "QProgressBar::chunk { background:#2563eb; border-radius:6px; }"));
        auto *vLbl = mkLabel(QStringLiteral("supplierClassValue%1").arg(i), QStringLiteral("0"), 9, QFont::Medium, QStringLiteral("#334155"));
        vLbl->setMinimumWidth(90);
        rowLay->addWidget(cLbl);
        rowLay->addWidget(bar, 1);
        rowLay->addWidget(vLbl);
        classLay->addWidget(row);
    }
    rootLayout->addWidget(classPanel);

    auto *segmentPanel = new QFrame(ui->stattabvente);
    segmentPanel->setStyleSheet(QStringLiteral("QFrame { background:#ffffff; border:1px solid #dbe4ea; border-radius:10px; }"));
    auto *segmentLay = new QVBoxLayout(segmentPanel);
    segmentLay->setContentsMargins(12, 12, 12, 12);
    segmentLay->setSpacing(8);
    segmentLay->addWidget(mkLabel(QStringLiteral("supplierSegmentTitle"),
                                  QStringLiteral("Segmentation operationnelle"),
                                  11,
                                  QFont::DemiBold,
                                  QStringLiteral("#0f172a")));
    for (int i = 0; i < 4; ++i) {
        auto *row = new QWidget(segmentPanel);
        auto *rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(0, 0, 0, 0);
        rowLay->setSpacing(8);
        auto *sLbl = mkLabel(QStringLiteral("supplierSegmentLabel%1").arg(i), QStringLiteral("-"), 9, QFont::Medium, QStringLiteral("#334155"));
        sLbl->setMinimumWidth(180);
        auto *bar = new QProgressBar(row);
        bar->setObjectName(QStringLiteral("supplierSegmentBar%1").arg(i));
        bar->setTextVisible(false);
        bar->setRange(0, 100);
        bar->setStyleSheet(QStringLiteral(
            "QProgressBar { background:#e2e8f0; border:none; border-radius:6px; min-height:8px; max-height:8px; }"
            "QProgressBar::chunk { background:#0ea5e9; border-radius:6px; }"));
        auto *vLbl = mkLabel(QStringLiteral("supplierSegmentValue%1").arg(i), QStringLiteral("0"), 9, QFont::Medium, QStringLiteral("#334155"));
        vLbl->setMinimumWidth(90);
        rowLay->addWidget(sLbl);
        rowLay->addWidget(bar, 1);
        rowLay->addWidget(vLbl);
        segmentLay->addWidget(row);
    }
    rootLayout->addWidget(segmentPanel);

    auto *rankingPanel = new QFrame(ui->stattabvente);
    rankingPanel->setStyleSheet(QStringLiteral("QFrame { background:#ffffff; border:1px solid #dbe4ea; border-radius:10px; }"));
    auto *rankingLay = new QVBoxLayout(rankingPanel);
    rankingLay->setContentsMargins(12, 12, 12, 12);
    rankingLay->setSpacing(8);
    rankingLay->addWidget(mkLabel(QStringLiteral("supplierRankingTitle"),
                                  QStringLiteral("Classement detaille des fournisseurs"),
                                  11,
                                  QFont::DemiBold,
                                  QStringLiteral("#0f172a")));
    auto *rankingTable = new QTableWidget(rankingPanel);
    rankingTable->setObjectName(QStringLiteral("supplierRankingTable"));
    rankingTable->setColumnCount(3);
    rankingTable->setHorizontalHeaderLabels({QStringLiteral("Fournisseur"), QStringLiteral("Score (%)"), QStringLiteral("Classe")});
    rankingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    rankingTable->setSelectionMode(QAbstractItemView::SingleSelection);
    rankingTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rankingTable->setAlternatingRowColors(false);
    rankingTable->setShowGrid(false);
    rankingTable->verticalHeader()->setVisible(false);
    rankingTable->horizontalHeader()->setStretchLastSection(true);
    rankingTable->setMinimumHeight(220);
    rankingTable->setStyleSheet(QStringLiteral(
        "QTableWidget { border:1px solid #e2e8f0; border-radius:8px; background:#ffffff; }"
        "QHeaderView::section { background:#f8fafc; border:none; border-bottom:1px solid #e2e8f0; padding:6px; font-weight:600; }"
        "QTableWidget::item { border:none; border-bottom:1px solid #f1f5f9; padding:6px; }"));
    rankingLay->addWidget(rankingTable);
    rootLayout->addWidget(rankingPanel);
}

void MainWindow::mettreAJourStatistiquesFournisseur()
{
    if (!ui->stattabvente) {
        return;
    }
    initialiserStatistiquesFournisseur();

    int totalSuppliers = 0;
    int activeSuppliers = 0;
    int monthNewSuppliers = 0;
    int previousMonthNewSuppliers = 0;
    int classA = 0;
    int classB = 0;
    int classC = 0;
    int classSurveillance = 0;
    int natureOlive = 0;
    int natureHuile = 0;
    int statusActifCount = 0;
    int statusRisqueCount = 0;
    int natureOliveActif = 0;
    int natureHuileActif = 0;
    int dossierIncomplet = 0;
    int autreNature = 0;
    double avgScore = 0.0;
    struct SupplierScoreRow {
        QString nom;
        int score = 0;
        QString classe;
    };
    QVector<SupplierScoreRow> supplierRows;

    QSqlQuery monthlyQuery;
    if (monthlyQuery.exec(QStringLiteral(
            "SELECT "
            "SUM(CASE WHEN TRUNC(DATE_AJOUT, 'MM') = TRUNC(SYSDATE, 'MM') THEN 1 ELSE 0 END), "
            "SUM(CASE WHEN TRUNC(DATE_AJOUT, 'MM') = ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -1) THEN 1 ELSE 0 END) "
            "FROM FOURNISSEUR")) &&
        monthlyQuery.next()) {
        monthNewSuppliers = monthlyQuery.value(0).toInt();
        previousMonthNewSuppliers = monthlyQuery.value(1).toInt();
    }

    double scoreSum = 0.0;
    QSqlQuery statsQuery;
    if (statsQuery.exec(QStringLiteral(
            "SELECT NOM, STATUS_FOURNISSEUR, EMAIL, TELEPHONE, %1, %2, DATE_AJOUT "
            "FROM FOURNISSEUR")
            .arg(colonneNatureFournisseurStats(), colonneTypeFournisseurStats()))) {
        while (statsQuery.next()) {
            ++totalSuppliers;

            const QString nomFournisseur = statsQuery.value(0).toString().trimmed();
            const QString status = statsQuery.value(1).toString().trimmed().toUpper();
            const QString email = statsQuery.value(2).toString().trimmed();
            const QString telephone = statsQuery.value(3).toString().trimmed();
            const QString nature = statsQuery.value(4).toString().trimmed().toUpper();
            const QString typeFournisseur = statsQuery.value(5).toString().trimmed().toUpper();
            const QDate dateAjout = statsQuery.value(6).toDate();

            if (status.startsWith(QStringLiteral("ACTIF"))) {
                ++activeSuppliers;
                ++statusActifCount;
            }
            if (status.contains(QStringLiteral("RISQUE"))) {
                ++statusRisqueCount;
            }
            if (nature.contains(QStringLiteral("OLIVE"))) {
                ++natureOlive;
                if (status.startsWith(QStringLiteral("ACTIF"))) {
                    ++natureOliveActif;
                }
            } else if (nature.contains(QStringLiteral("HUILE"))) {
                ++natureHuile;
                if (status.startsWith(QStringLiteral("ACTIF"))) {
                    ++natureHuileActif;
                }
            } else {
                ++autreNature;
            }
            if (email.isEmpty() || telephone.isEmpty()) {
                ++dossierIncomplet;
            }

            int score = 0;
            if (status.startsWith(QStringLiteral("ACTIF"))) {
                score += 48;
            } else if (status.contains(QStringLiteral("RISQUE"))) {
                score += 14;
            } else {
                score += 24;
            }
            if (!email.isEmpty()) {
                score += 12;
            }
            if (!telephone.isEmpty()) {
                score += 12;
            }
            if (!nature.isEmpty()) {
                score += 8;
            }
            if (nature.contains(QStringLiteral("OLIVE")) || nature.contains(QStringLiteral("HUILE"))) {
                score += 6;
            }
            if (!typeFournisseur.isEmpty()) {
                // Pondération qualité par variété/type fournisseur.
                if (typeFournisseur.contains(QStringLiteral("CHETOUI"))
                    || typeFournisseur.contains(QStringLiteral("ZARRAZI"))) {
                    score += 22;
                } else if (typeFournisseur.contains(QStringLiteral("CHEMLI"))) {
                    score += 20;
                } else if (typeFournisseur.contains(QStringLiteral("SAHLI"))
                           || typeFournisseur.contains(QStringLiteral("OUSDI"))
                           || typeFournisseur.contains(QStringLiteral("PICHOLINE"))) {
                    score += 12;
                } else {
                    score += 8;
                }
            }
            const int daysSinceAdd = dateAjout.isValid() ? dateAjout.daysTo(QDate::currentDate()) : 0;
            if (daysSinceAdd >= 180) {
                score += 14;
            } else if (daysSinceAdd >= 90) {
                score += 8;
            } else {
                score += 2;
            }
            score = qBound(0, score, 100);
            scoreSum += static_cast<double>(score);

            if (score >= 80) {
                ++classA;
                supplierRows.push_back({nomFournisseur, score, QStringLiteral("Classe A")});
            } else if (score >= 65) {
                ++classB;
                supplierRows.push_back({nomFournisseur, score, QStringLiteral("Classe B")});
            } else if (score >= 50) {
                ++classC;
                supplierRows.push_back({nomFournisseur, score, QStringLiteral("Classe C")});
            } else {
                ++classSurveillance;
                supplierRows.push_back({nomFournisseur, score, QStringLiteral("Surveillance")});
            }
        }
    }
    avgScore = totalSuppliers > 0 ? (scoreSum / totalSuppliers) : 0.0;

    const double activeRate = totalSuppliers > 0 ? (activeSuppliers * 100.0 / totalSuppliers) : 0.0;
    const int qualifiedSuppliers = classA + classB;
    const double qualifiedRate = totalSuppliers > 0 ? (qualifiedSuppliers * 100.0 / totalSuppliers) : 0.0;
    const int criticalSuppliers = classC + classSurveillance;
    const double criticalRate = totalSuppliers > 0 ? (criticalSuppliers * 100.0 / totalSuppliers) : 0.0;
    const double dossierCompletRate = totalSuppliers > 0 ? ((totalSuppliers - dossierIncomplet) * 100.0 / totalSuppliers) : 0.0;
    const double natureCoverageRate = totalSuppliers > 0 ? ((natureOlive + natureHuile) * 100.0 / totalSuppliers) : 0.0;

    if (auto *v = ui->stattabvente->findChild<QLabel *>(QStringLiteral("supplierKpiTotalValue"))) v->setText(QString::number(totalSuppliers));
    if (auto *t = ui->stattabvente->findChild<QLabel *>(QStringLiteral("supplierKpiTotalTrend"))) t->setText(
        QString::number(qualifiedRate, 'f', 1) + QStringLiteral("% classes A/B | ")
        + QString::number(dossierCompletRate, 'f', 1) + QStringLiteral("% dossiers complets"));

    if (auto *v = ui->stattabvente->findChild<QLabel *>(QStringLiteral("supplierKpiScoreValue"))) v->setText(QString::number(avgScore, 'f', 1) + QStringLiteral("/100"));
    if (auto *t = ui->stattabvente->findChild<QLabel *>(QStringLiteral("supplierKpiScoreTrend"))) t->setText(
        QString::number(natureCoverageRate, 'f', 1) + QStringLiteral("% nature qualifiee | ")
        + QString::number(activeRate, 'f', 1) + QStringLiteral("% actifs"));

    if (auto *v = ui->stattabvente->findChild<QLabel *>(QStringLiteral("supplierKpiRiskValue"))) v->setText(QString::number(criticalSuppliers));
    if (auto *t = ui->stattabvente->findChild<QLabel *>(QStringLiteral("supplierKpiRiskTrend"))) t->setText(
        QString::number(criticalRate, 'f', 1) + QStringLiteral("% du panel | ")
        + trendText(static_cast<double>(monthNewSuppliers),
                    static_cast<double>(previousMonthNewSuppliers)));

    QMap<QString, int> suppliersByMonth;
    QSqlQuery monthQuery;
    monthQuery.prepare(QStringLiteral(
        "SELECT TO_CHAR(TRUNC(DATE_AJOUT, 'MM'), 'YYYY-MM') AS MONTH_KEY, COUNT(*) "
        "FROM FOURNISSEUR "
        "WHERE DATE_AJOUT >= ADD_MONTHS(TRUNC(SYSDATE, 'MM'), -5) "
        "GROUP BY TRUNC(DATE_AJOUT, 'MM') "
        "ORDER BY TRUNC(DATE_AJOUT, 'MM')"));
    if (monthQuery.exec()) {
        while (monthQuery.next()) {
            suppliersByMonth.insert(monthQuery.value(0).toString(), monthQuery.value(1).toInt());
        }
    }

    QLocale locale(QLocale::French, QLocale::Tunisia);
    QVector<int> orderedCounts;
    orderedCounts.reserve(6);
    QDate monthCursor = QDate::currentDate().addMonths(-5);
    int maxCount = 0;
    for (int i = 0; i < 6; ++i) {
        const QString key = monthCursor.toString(QStringLiteral("yyyy-MM"));
        const int count = suppliersByMonth.value(key, 0);
        orderedCounts.push_back(count);
        maxCount = qMax(maxCount, count);
        monthCursor = monthCursor.addMonths(1);
    }

    monthCursor = QDate::currentDate().addMonths(-5);
    for (int i = 0; i < 6; ++i) {
        QString shortMonth = locale.standaloneMonthName(monthCursor.month(), QLocale::ShortFormat);
        if (shortMonth.isEmpty()) {
            shortMonth = monthCursor.toString(QStringLiteral("MMM"));
        }
        shortMonth = shortMonth.left(3);
        if (!shortMonth.isEmpty()) {
            shortMonth[0] = shortMonth.at(0).toUpper();
        }
        const int count = orderedCounts.value(i);
        const int progress = maxCount > 0 ? qRound((count * 100.0) / maxCount) : 0;
        if (auto *lbl = ui->stattabvente->findChild<QLabel *>(QStringLiteral("supplierMonthLabel%1").arg(i))) lbl->setText(shortMonth);
        if (auto *bar = ui->stattabvente->findChild<QProgressBar *>(QStringLiteral("supplierMonthBar%1").arg(i))) bar->setValue(qBound(0, progress, 100));
        if (auto *val = ui->stattabvente->findChild<QLabel *>(QStringLiteral("supplierMonthValue%1").arg(i))) val->setText(QString::number(count));
        monthCursor = monthCursor.addMonths(1);
    }

    const QVector<QPair<QString, int>> supplierClasses = {
        {QStringLiteral("Classe A (>=80)"), classA},
        {QStringLiteral("Classe B (65-79)"), classB},
        {QStringLiteral("Classe C (50-64)"), classC},
        {QStringLiteral("Surveillance (<50)"), classSurveillance}
    };

    for (int i = 0; i < 4; ++i) {
        QLabel *regionLabel = ui->stattabvente->findChild<QLabel *>(QStringLiteral("supplierClassLabel%1").arg(i));
        QProgressBar *regionBar = ui->stattabvente->findChild<QProgressBar *>(QStringLiteral("supplierClassBar%1").arg(i));
        QLabel *regionValue = ui->stattabvente->findChild<QLabel *>(QStringLiteral("supplierClassValue%1").arg(i));
        if (!regionLabel || !regionBar || !regionValue) {
            continue;
        }
        if (i < supplierClasses.size() && totalSuppliers > 0) {
            const QString className = supplierClasses[i].first;
            const double share = (supplierClasses[i].second * 100.0) / totalSuppliers;
            regionLabel->setText(className);
            regionBar->setValue(qBound(0, qRound(share), 100));
            QString classColor = QStringLiteral("#2563eb");
            if (i == 0) {
                classColor = QStringLiteral("#15803d"); // Classe A
            } else if (i == 1) {
                classColor = QStringLiteral("#1d4ed8"); // Classe B
            } else if (i == 2) {
                classColor = QStringLiteral("#b45309"); // Classe C
            } else if (i == 3) {
                classColor = QStringLiteral("#b91c1c"); // Surveillance
            }
            regionBar->setStyleSheet(QStringLiteral(
                "QProgressBar { background:#e2e8f0; border:none; border-radius:6px; min-height:8px; max-height:8px; }"
                "QProgressBar::chunk { background:%1; border-radius:6px; }")
                .arg(classColor));
            regionLabel->setStyleSheet(QStringLiteral("QLabel { color:%1; font-weight:600; }").arg(classColor));
            regionValue->setText(
                QString::number(supplierClasses[i].second) + QStringLiteral(" (")
                + QString::number(share, 'f', 1) + QStringLiteral("%)"));
        } else {
            regionLabel->setText(QStringLiteral("-"));
            regionBar->setValue(0);
            regionBar->setStyleSheet(QStringLiteral(
                "QProgressBar { background:#e2e8f0; border:none; border-radius:6px; min-height:8px; max-height:8px; }"
                "QProgressBar::chunk { background:#94a3b8; border-radius:6px; }"));
            regionLabel->setStyleSheet(QStringLiteral("QLabel { color:#334155; font-weight:500; }"));
            regionValue->setText(QStringLiteral("0 (0.0%)"));
        }
    }

    const QVector<QPair<QString, int>> supplierSegments = {
        {QStringLiteral("Olive actifs"), natureOliveActif},
        {QStringLiteral("Huile actifs"), natureHuileActif},
        {QStringLiteral("Dossiers incomplets"), dossierIncomplet},
        {QStringLiteral("Natures non qualifiees"), autreNature}
    };
    for (int i = 0; i < 4; ++i) {
        QLabel *segmentLabel = ui->stattabvente->findChild<QLabel *>(QStringLiteral("supplierSegmentLabel%1").arg(i));
        QProgressBar *segmentBar = ui->stattabvente->findChild<QProgressBar *>(QStringLiteral("supplierSegmentBar%1").arg(i));
        QLabel *segmentValue = ui->stattabvente->findChild<QLabel *>(QStringLiteral("supplierSegmentValue%1").arg(i));
        if (!segmentLabel || !segmentBar || !segmentValue) {
            continue;
        }
        if (i < supplierSegments.size() && totalSuppliers > 0) {
            const QString segmentName = supplierSegments[i].first;
            const double share = (supplierSegments[i].second * 100.0) / totalSuppliers;
            segmentLabel->setText(segmentName);
            segmentBar->setValue(qBound(0, qRound(share), 100));
            segmentValue->setText(
                QString::number(supplierSegments[i].second) + QStringLiteral(" (")
                + QString::number(share, 'f', 1) + QStringLiteral("%)"));
        } else {
            segmentLabel->setText(QStringLiteral("-"));
            segmentBar->setValue(0);
            segmentValue->setText(QStringLiteral("0 (0.0%)"));
        }
    }

    if (QTableWidget *rankingTable = ui->stattabvente->findChild<QTableWidget *>(QStringLiteral("supplierRankingTable"))) {
        std::sort(supplierRows.begin(), supplierRows.end(), [](const SupplierScoreRow &a, const SupplierScoreRow &b) {
            return a.score > b.score;
        });

        rankingTable->setRowCount(supplierRows.size());
        for (int r = 0; r < supplierRows.size(); ++r) {
            const QString nom = supplierRows[r].nom.isEmpty() ? QStringLiteral("(Sans nom)") : supplierRows[r].nom;
            auto *nomItem = new QTableWidgetItem(nom);
            auto *scoreItem = new QTableWidgetItem(QString::number(supplierRows[r].score) + QStringLiteral("%"));
            auto *classeItem = new QTableWidgetItem(supplierRows[r].classe);
            rankingTable->setItem(r, 0, nomItem);
            rankingTable->setItem(r, 1, scoreItem);
            rankingTable->setItem(r, 2, classeItem);
        }
    }
}

void MainWindow::configurerTriAvanceFacture()
{
    m_menuTriFacture = new QMenu(this);
    m_menuTriFacture->setTitle("Tri avance");

    auto addTriAction = [this](QMenu *menu,
                               const QString &texteAction,
                               const QString &champSql,
                               bool asc,
                               const QString &libelle) {
        QAction *action = menu->addAction(texteAction);
        connect(action, &QAction::triggered, this, [this, champSql, asc, libelle]() {
            appliquerTriFacture(champSql, asc, libelle);
        });
    };

    QMenu *menuId = m_menuTriFacture->addMenu("ID facture");
    addTriAction(menuId, "ID croissant", "ID_FACTURE", true, "ID ASC");
    addTriAction(menuId, "ID decroissant", "ID_FACTURE", false, "ID DESC");

    QMenu *menuMatricule = m_menuTriFacture->addMenu("Matricule fiscal");
    addTriAction(menuMatricule, "A -> Z", "MATRICULE_FISCAL", true, "Matricule ASC");
    addTriAction(menuMatricule, "Z -> A", "MATRICULE_FISCAL", false, "Matricule DESC");

    QMenu *menuEntreprise = m_menuTriFacture->addMenu("Entreprise");
    addTriAction(menuEntreprise, "A -> Z", "NOM_ENTREPRISE", true, "Entreprise ASC");
    addTriAction(menuEntreprise, "Z -> A", "NOM_ENTREPRISE", false, "Entreprise DESC");

    QMenu *menuIdCf = m_menuTriFacture->addMenu("ID client/fournisseur");
    addTriAction(menuIdCf, "A -> Z", "NOM_RAISON_SOCIALE", true, "ID CF ASC");
    addTriAction(menuIdCf, "Z -> A", "NOM_RAISON_SOCIALE", false, "ID CF DESC");

    QMenu *menuType = m_menuTriFacture->addMenu("Type");
    addTriAction(menuType, "A -> Z", "TYPE_FACTURE", true, "Type ASC");
    addTriAction(menuType, "Z -> A", "TYPE_FACTURE", false, "Type DESC");

    QMenu *menuDate = m_menuTriFacture->addMenu("Date facturation");
    addTriAction(menuDate, "Plus anciennes", "DATE_FACTURATION", true, "Date ASC");
    addTriAction(menuDate, "Plus recentes", "DATE_FACTURATION", false, "Date DESC");

    QMenu *menuPrix = m_menuTriFacture->addMenu("Prix unitaire");
    addTriAction(menuPrix, "Prix croissant", "PRIX_UNITAIRE", true, "Prix ASC");
    addTriAction(menuPrix, "Prix decroissant", "PRIX_UNITAIRE", false, "Prix DESC");

    QMenu *menuQuantite = m_menuTriFacture->addMenu("Quantite");
    addTriAction(menuQuantite, "Quantite croissante", "QUANTITE", true, "Quantite ASC");
    addTriAction(menuQuantite, "Quantite decroissante", "QUANTITE", false, "Quantite DESC");

    QMenu *menuDescription = m_menuTriFacture->addMenu("Description");
    addTriAction(menuDescription, "A -> Z", "DESCRIPTION", true, "Description ASC");
    addTriAction(menuDescription, "Z -> A", "DESCRIPTION", false, "Description DESC");

    m_menuTriFacture->addSeparator();
    addTriAction(m_menuTriFacture, "Tri par defaut (ID ASC)", "ID_FACTURE", true, "ID ASC");
}

void MainWindow::configurerTriAvanceFournisseur()
{
    m_menuTriFournisseur = new QMenu(this);
    m_menuTriFournisseur->setTitle("Tri avance fournisseurs");

    auto addTriAction = [this](QMenu *menu,
                               const QString &texteAction,
                               int col,
                               bool asc,
                               const QString &libelle) {
        QAction *action = menu->addAction(texteAction);
        connect(action, &QAction::triggered, this, [this, col, asc, libelle]() {
            appliquerTriFournisseur(col, asc, libelle);
        });
    };

    QMenu *menuId = m_menuTriFournisseur->addMenu("ID");
    addTriAction(menuId, "Croissant", 0, true, "ID ASC");
    addTriAction(menuId, "Decroissant", 0, false, "ID DESC");

    QMenu *menuNom = m_menuTriFournisseur->addMenu("Nom");
    addTriAction(menuNom, "A -> Z", 1, true, "Nom ASC");
    addTriAction(menuNom, "Z -> A", 1, false, "Nom DESC");

    QMenu *menuRegion = m_menuTriFournisseur->addMenu("Region");
    addTriAction(menuRegion, "A -> Z", 2, true, "Region ASC");
    addTriAction(menuRegion, "Z -> A", 2, false, "Region DESC");

    QMenu *menuEmail = m_menuTriFournisseur->addMenu("Email");
    addTriAction(menuEmail, "A -> Z", 3, true, "Email ASC");
    addTriAction(menuEmail, "Z -> A", 3, false, "Email DESC");

    QMenu *menuType = m_menuTriFournisseur->addMenu("Type");
    addTriAction(menuType, "A -> Z", 4, true, "Type ASC");
    addTriAction(menuType, "Z -> A", 4, false, "Type DESC");

    QMenu *menuDate = m_menuTriFournisseur->addMenu("Date ajout");
    addTriAction(menuDate, "Plus anciennes", 5, true, "Date ASC");
    addTriAction(menuDate, "Plus recentes", 5, false, "Date DESC");

    QMenu *menuStatus = m_menuTriFournisseur->addMenu("Status");
    addTriAction(menuStatus, "A -> Z", 6, true, "Status ASC");
    addTriAction(menuStatus, "Z -> A", 6, false, "Status DESC");

    QMenu *menuTel = m_menuTriFournisseur->addMenu("Telephone");
    addTriAction(menuTel, "Croissant", 7, true, "Telephone ASC");
    addTriAction(menuTel, "Decroissant", 7, false, "Telephone DESC");

    m_menuTriFournisseur->addSeparator();
    addTriAction(m_menuTriFournisseur, "Tri par defaut (ID ASC)", 0, true, "ID ASC");
}

void MainWindow::appliquerTriFacture(const QString &champSql, bool asc, const QString &libelle)
{
    m_champTriFacture = champSql.toUpper();
    m_triFactureAsc = asc;
    m_libelleTriFacture = libelle;
    actualiserLibelleTri();
    chargerFactures(ui->searchInput_5 ? ui->searchInput_5->text().trimmed() : QString());
}

void MainWindow::appliquerTriFournisseur(int colonne, bool asc, const QString &libelle)
{
    if (!ui->employeeTable_4) {
        return;
    }

    m_colTriFournisseur = qMax(0, qMin(colonne, ui->employeeTable_4->columnCount() - 1));
    m_triFournisseurAsc = asc;
    m_libelleTriFournisseur = libelle;

    ui->employeeTable_4->sortItems(m_colTriFournisseur,
                                   m_triFournisseurAsc ? Qt::AscendingOrder : Qt::DescendingOrder);
    actualiserLibelleTriFournisseur();
    filtrerFournisseurs(ui->searchInput_4 ? ui->searchInput_4->text().trimmed() : QString());
}

QString MainWindow::ordreSqlTriFacture() const
{
    static const QSet<QString> champsAutorises = {
        QStringLiteral("ID_FACTURE"),
        QStringLiteral("MATRICULE_FISCAL"),
        QStringLiteral("NOM_ENTREPRISE"),
        QStringLiteral("NOM_RAISON_SOCIALE"),
        QStringLiteral("TYPE_FACTURE"),
        QStringLiteral("DATE_FACTURATION"),
        QStringLiteral("PRIX_UNITAIRE"),
        QStringLiteral("QUANTITE"),
        QStringLiteral("DESCRIPTION"),
    };

    QString champ = m_champTriFacture.toUpper();
    if (!champsAutorises.contains(champ)) {
        champ = QStringLiteral("ID_FACTURE");
    }

    return champ + (m_triFactureAsc ? QStringLiteral(" ASC, ID_FACTURE ASC")
                                    : QStringLiteral(" DESC, ID_FACTURE ASC"));
}

void MainWindow::actualiserLibelleTri()
{
    if (!ui->sortBtn_5) {
        return;
    }
    ui->sortBtn_5->setText("Tri: " + m_libelleTriFacture);
}

void MainWindow::actualiserLibelleTriFournisseur()
{
    if (!ui->sortBtn_4) {
        return;
    }
    ui->sortBtn_4->setText("Tri: " + m_libelleTriFournisseur);
}

void MainWindow::filtrerFournisseurs(const QString &filtre)
{
    if (!ui->employeeTable_4) {
        return;
    }

    const QString f = filtre.trimmed();
    for (int row = 0; row < ui->employeeTable_4->rowCount(); ++row) {
        bool visible = f.isEmpty();
        if (!visible) {
            for (int col = 0; col < ui->employeeTable_4->columnCount(); ++col) {
                QTableWidgetItem *it = ui->employeeTable_4->item(row, col);
                if (it && it->text().contains(f, Qt::CaseInsensitive)) {
                    visible = true;
                    break;
                }
            }
        }
        ui->employeeTable_4->setRowHidden(row, !visible);
    }
}

void MainWindow::chargerFournisseurs()
{
    QSqlQueryModel *model = Fournisseur::afficher();
    if (!model) {
        ui->employeeTable_4->setRowCount(0);
        return;
    }

    while (model->canFetchMore()) {
        model->fetchMore();
    }

    if (!Fournisseur::dernierErreur().isEmpty()) {
        QMessageBox::critical(this, "Erreur SQL", "Impossible de charger les fournisseurs:\n" + Fournisseur::dernierErreur());
        ui->employeeTable_4->setRowCount(0);
        delete model;
        return;
    }

    if (model->lastError().isValid()) {
        QMessageBox::critical(this, "Erreur SQL", "Impossible de charger les fournisseurs:\n" + model->lastError().text());
        ui->employeeTable_4->setRowCount(0);
        delete model;
        return;
    }

    ui->employeeTable_4->setRowCount(0);
    const int rowCount = model->rowCount();
    const int colCount = qMin(8, model->columnCount());

    for (int row = 0; row < rowCount; ++row) {
        ui->employeeTable_4->insertRow(row);
        for (int col = 0; col < colCount; ++col) {
            const QString text = model->data(model->index(row, col)).toString();
            ui->employeeTable_4->setItem(row, col, new QTableWidgetItem(text));
        }
    }

    ui->modifyBtn_4->setEnabled(false);
    ui->deleteBtn_4->setEnabled(false);
    mettreAJourStatistiquesFournisseur();
    delete model;
}

int MainWindow::prochainIdFournisseur()
{
    const int id = Fournisseur::prochainId();
    if (id <= 0) {
        QMessageBox::critical(this,
                              "Erreur SQL",
                              "Impossible de calculer id_fournisseur:\n" + Fournisseur::dernierErreur());
    }
    return id;
}

void MainWindow::exporterFacturesPdf()
{
    if (!ui->employeeTable_5 || ui->employeeTable_5->rowCount() == 0) {
        QMessageBox::information(this, "Export PDF", "Aucune donnee a exporter.");
        return;
    }

    const QString fileName = QFileDialog::getSaveFileName(
        this,
        "Exporter les factures en PDF",
        QDir::homePath() + "/factures_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".pdf",
        "PDF (*.pdf)");
    if (fileName.isEmpty()) {
        return;
    }

    QPdfWriter writer(fileName);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Landscape);
    writer.setResolution(120);

    QPainter painter(&writer);
    if (!painter.isActive()) {
        QMessageBox::critical(this, "Export PDF", "Impossible de creer le fichier PDF.");
        return;
    }

    const QRect pageRect = writer.pageLayout().paintRectPixels(writer.resolution());
    const int margin = 28;
    const int left = pageRect.left() + margin;
    const int right = pageRect.right() - margin;
    const int top = pageRect.top() + margin;
    const int bottom = pageRect.bottom() - margin;
    const int tableWidth = right - left;
    const int colCount = ui->employeeTable_5->columnCount();

    if (colCount <= 0 || tableWidth <= 0) {
        QMessageBox::warning(this, "Export PDF", "Tableau invalide pour export.");
        return;
    }

    QVector<int> sourceWidths(colCount);
    int sourceTotal = 0;
    for (int c = 0; c < colCount; ++c) {
        sourceWidths[c] = qMax(1, ui->employeeTable_5->columnWidth(c));
        sourceTotal += sourceWidths[c];
    }
    if (sourceTotal <= 0) {
        sourceTotal = colCount;
    }

    QVector<int> colWidths(colCount);
    int used = 0;
    for (int c = 0; c < colCount; ++c) {
        colWidths[c] = (c == colCount - 1)
        ? (tableWidth - used)
        : qMax(45, (tableWidth * sourceWidths[c]) / sourceTotal);
        used += colWidths[c];
    }

    QFont titleFont("Poppins", 12, QFont::Bold);
    QFont headerFont("Poppins", 8, QFont::Bold);
    QFont cellFont("Poppins", 8);

    int y = top;
    painter.setFont(titleFont);
    painter.drawText(left, y, "Liste des factures");
    y += 20;
    painter.setFont(cellFont);
    painter.drawText(left, y, "Date export: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
    y += 18;
    const QString filtre = ui->searchInput_5 ? ui->searchInput_5->text().trimmed() : QString();
    painter.drawText(left, y, "Filtre: " + (filtre.isEmpty() ? QString("Aucun") : filtre));
    y += 16;
    painter.drawText(left, y, "Tri: " + m_libelleTriFacture);
    y += 18;

    const int headerHeight = 24;
    const int rowHeight = 22;

    auto drawHeader = [&]() {
        int x = left;
        painter.setFont(headerFont);
        painter.setPen(QPen(Qt::black, 1));
        painter.setBrush(QColor(232, 245, 233));
        for (int c = 0; c < colCount; ++c) {
            QRect cellRect(x, y, colWidths[c], headerHeight);
            painter.drawRect(cellRect);
            const QString headerText = ui->employeeTable_5->horizontalHeaderItem(c)
                                           ? ui->employeeTable_5->horizontalHeaderItem(c)->text()
                                           : QString("Col %1").arg(c + 1);
            const QString elided = QFontMetrics(headerFont).elidedText(headerText, Qt::ElideRight, cellRect.width() - 6);
            painter.drawText(cellRect.adjusted(3, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft, elided);
            x += colWidths[c];
        }
        y += headerHeight;
    };

    drawHeader();

    painter.setFont(cellFont);
    for (int r = 0; r < ui->employeeTable_5->rowCount(); ++r) {
        if (y + rowHeight > bottom) {
            writer.newPage();
            y = top;
            drawHeader();
            painter.setFont(cellFont);
        }

        int x = left;
        for (int c = 0; c < colCount; ++c) {
            QRect cellRect(x, y, colWidths[c], rowHeight);
            painter.setPen(QPen(QColor(180, 180, 180), 1));
            painter.drawRect(cellRect);
            const QString value = ui->employeeTable_5->item(r, c) ? ui->employeeTable_5->item(r, c)->text() : QString();
            const QString elided = QFontMetrics(cellFont).elidedText(value, Qt::ElideRight, cellRect.width() - 6);
            painter.drawText(cellRect.adjusted(3, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft, elided);
            x += colWidths[c];
        }
        y += rowHeight;
    }

    painter.end();
    QMessageBox::information(this, "Export PDF", "Export termine:\n" + fileName);
}

void MainWindow::exporterFournisseursPdf()
{
    if (!ui->employeeTable_4 || ui->employeeTable_4->rowCount() == 0) {
        QMessageBox::information(this, "Export PDF", "Aucune donnee a exporter.");
        return;
    }

    const QString fileName = QFileDialog::getSaveFileName(
        this,
        "Exporter les fournisseurs en PDF",
        QDir::homePath() + "/fournisseurs_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".pdf",
        "PDF (*.pdf)");
    if (fileName.isEmpty()) {
        return;
    }

    QPdfWriter writer(fileName);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Landscape);
    writer.setResolution(120);

    QPainter painter(&writer);
    if (!painter.isActive()) {
        QMessageBox::critical(this, "Export PDF", "Impossible de creer le fichier PDF.");
        return;
    }

    const QRect pageRect = writer.pageLayout().paintRectPixels(writer.resolution());
    const int margin = 28;
    const int left = pageRect.left() + margin;
    const int right = pageRect.right() - margin;
    const int top = pageRect.top() + margin;
    const int bottom = pageRect.bottom() - margin;
    const int tableWidth = right - left;
    const int colCount = ui->employeeTable_4->columnCount();

    if (colCount <= 0 || tableWidth <= 0) {
        QMessageBox::warning(this, "Export PDF", "Tableau invalide pour export.");
        return;
    }

    QVector<int> sourceWidths(colCount);
    int sourceTotal = 0;
    for (int c = 0; c < colCount; ++c) {
        sourceWidths[c] = qMax(1, ui->employeeTable_4->columnWidth(c));
        sourceTotal += sourceWidths[c];
    }
    if (sourceTotal <= 0) {
        sourceTotal = colCount;
    }

    QVector<int> colWidths(colCount);
    int used = 0;
    for (int c = 0; c < colCount; ++c) {
        colWidths[c] = (c == colCount - 1)
        ? (tableWidth - used)
        : qMax(45, (tableWidth * sourceWidths[c]) / sourceTotal);
        used += colWidths[c];
    }

    QFont titleFont("Poppins", 12, QFont::Bold);
    QFont headerFont("Poppins", 8, QFont::Bold);
    QFont cellFont("Poppins", 8);

    int y = top;
    painter.setFont(titleFont);
    painter.drawText(left, y, "Liste des fournisseurs");
    y += 20;
    painter.setFont(cellFont);
    painter.drawText(left, y, "Date export: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
    y += 18;
    const QString filtre = ui->searchInput_4 ? ui->searchInput_4->text().trimmed() : QString();
    painter.drawText(left, y, "Filtre: " + (filtre.isEmpty() ? QString("Aucun") : filtre));
    y += 16;
    painter.drawText(left, y, "Tri: " + m_libelleTriFournisseur);
    y += 18;

    const int headerHeight = 24;
    const int rowHeight = 22;

    auto drawHeader = [&]() {
        int x = left;
        painter.setFont(headerFont);
        painter.setPen(QPen(Qt::black, 1));
        painter.setBrush(QColor(232, 245, 233));
        for (int c = 0; c < colCount; ++c) {
            QRect cellRect(x, y, colWidths[c], headerHeight);
            painter.drawRect(cellRect);
            const QString headerText = ui->employeeTable_4->horizontalHeaderItem(c)
                                           ? ui->employeeTable_4->horizontalHeaderItem(c)->text()
                                           : QString("Col %1").arg(c + 1);
            const QString elided = QFontMetrics(headerFont).elidedText(headerText, Qt::ElideRight, cellRect.width() - 6);
            painter.drawText(cellRect.adjusted(3, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft, elided);
            x += colWidths[c];
        }
        y += headerHeight;
    };

    drawHeader();

    painter.setFont(cellFont);
    for (int r = 0; r < ui->employeeTable_4->rowCount(); ++r) {
        if (ui->employeeTable_4->isRowHidden(r)) {
            continue;
        }
        if (y + rowHeight > bottom) {
            writer.newPage();
            y = top;
            drawHeader();
            painter.setFont(cellFont);
        }

        int x = left;
        for (int c = 0; c < colCount; ++c) {
            QRect cellRect(x, y, colWidths[c], rowHeight);
            painter.setPen(QPen(QColor(180, 180, 180), 1));
            painter.drawRect(cellRect);
            const QString value = ui->employeeTable_4->item(r, c) ? ui->employeeTable_4->item(r, c)->text() : QString();
            const QString elided = QFontMetrics(cellFont).elidedText(value, Qt::ElideRight, cellRect.width() - 6);
            painter.drawText(cellRect.adjusted(3, 0, -3, 0), Qt::AlignVCenter | Qt::AlignLeft, elided);
            x += colWidths[c];
        }
        y += rowHeight;
    }

    painter.end();
    QMessageBox::information(this, "Export PDF", "Export termine:\n" + fileName);
}

void MainWindow::configurerControleSaisieFournisseur()
{
    ui->emailEdit_8->setMaxLength(100);
    ui->emailEdit_4->setMaxLength(100);
    ui->emailEdit_5->setMaxLength(120);
    ui->emailEdit_16->setMaxLength(15);

    auto *nomValidator = new QRegularExpressionValidator(
        QRegularExpression("^[A-Za-z\\x{00C0}-\\x{00FF}\\s\\-']{0,100}$"), this);
    ui->emailEdit_8->setValidator(nomValidator);

    auto *regionValidator = new QRegularExpressionValidator(
        QRegularExpression("^[A-Za-z\\x{00C0}-\\x{00FF}\\s\\-']{0,100}$"), this);
    ui->emailEdit_4->setValidator(regionValidator);

    auto *emailValidator = new QRegularExpressionValidator(
        QRegularExpression("^[A-Za-z0-9._%+\\-@]{0,120}$"), this);
    ui->emailEdit_5->setValidator(emailValidator);

    auto *telephoneValidator = new QRegularExpressionValidator(
        QRegularExpression("^\\d{0,15}$"), this);
    ui->emailEdit_16->setValidator(telephoneValidator);
}

bool MainWindow::validerSaisieFournisseur(QString &nom,
                                          QString &region,
                                          QString &email,
                                          QString &type,
                                          QString &status,
                                          QString &telephone,
                                          QString &dateAjout)
{
    nom = ui->emailEdit_8->text().trimmed();
    region = ui->emailEdit_4->text().trimmed();
    email = ui->emailEdit_5->text().trimmed();
    type = ui->departmentComboBox_4->currentText().trimmed();
    status = ui->departmentComboBox_6->currentText().trimmed();
    telephone = ui->emailEdit_16->text().trimmed();
    dateAjout = ui->startDateEdit_4->date().toString("yyyy-MM-dd");

    if (nom.isEmpty() || region.isEmpty() || email.isEmpty() || telephone.isEmpty()) {
        QMessageBox::warning(this, "Validation", "Nom, region, email et telephone sont obligatoires.");
        return false;
    }

    const QRegularExpression textePattern("^[A-Za-z\\x{00C0}-\\x{00FF}\\s\\-']{2,100}$");
    if (!textePattern.match(nom).hasMatch()) {
        QMessageBox::warning(this, "Validation", "Nom invalide: lettres uniquement.");
        ui->emailEdit_8->setFocus();
        return false;
    }
    if (!textePattern.match(region).hasMatch()) {
        QMessageBox::warning(this, "Validation", "Region invalide: lettres uniquement.");
        ui->emailEdit_4->setFocus();
        return false;
    }

    const QRegularExpression emailPattern("^[A-Za-z0-9._%+\\-]+@[A-Za-z0-9\\-.]+\\.[A-Za-z]{2,}$");
    if (!emailPattern.match(email).hasMatch()) {
        QMessageBox::warning(this, "Validation", "Email invalide.");
        ui->emailEdit_5->setFocus();
        return false;
    }

    const QRegularExpression telPattern("^\\d{8,15}$");
    if (!telPattern.match(telephone).hasMatch()) {
        QMessageBox::warning(this, "Validation", "Telephone invalide: 8 a 15 chiffres.");
        ui->emailEdit_16->setFocus();
        return false;
    }

    if (type.isEmpty() || status.isEmpty()) {
        QMessageBox::warning(this, "Validation", "Nature et status sont obligatoires.");
        return false;
    }

    return true;
}

void MainWindow::reinitialiserEditionFournisseur()
{
    m_idFournisseurEditionLocal = -1;

    ui->emailEdit_8->clear();
    ui->emailEdit_4->clear();
    ui->emailEdit_5->clear();
    ui->emailEdit_16->clear();
    ui->departmentComboBox_4->setCurrentIndex(0);
    ui->departmentComboBox_6->setCurrentIndex(0);
    ui->startDateEdit_4->setDate(QDate::currentDate());
    ui->saveclientBtn_4->setText("Enregistrer");
}

int MainWindow::trouverLigneFournisseurParId(int id) const
{
    if (!ui->employeeTable_4) {
        return -1;
    }
    for (int row = 0; row < ui->employeeTable_4->rowCount(); ++row) {
        QTableWidgetItem *idItem = ui->employeeTable_4->item(row, 0);
        bool ok = false;
        const int currentId = idItem ? idItem->text().toInt(&ok) : -1;
        if (ok && currentId == id) {
            return row;
        }
    }
    return -1;
}

void MainWindow::configurerControleSaisieFacture()
{
    ui->emailEdit_13->setMaxLength(20);
    ui->emailEdit_15->setMaxLength(100);
    ui->emailEdit_7->setMaxLength(20);
    ui->emailEdit_14->setMaxLength(255);

    auto *matriculeValidator = new QRegularExpressionValidator(
        QRegularExpression("^[A-Za-z0-9\\-/]{0,20}$"), this);
    ui->emailEdit_13->setValidator(matriculeValidator);

    auto *nomEntrepriseValidator = new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("^[A-Za-z\\x{00C0}-\\x{00FF}0-9\\s\\-'.&,]{0,100}$")), this);
    ui->emailEdit_15->setValidator(nomEntrepriseValidator);

    // ID client/fournisseur : numerique uniquement
    auto *idCfValidator = new QIntValidator(1, 2147483647, this);
    ui->emailEdit_7->setValidator(idCfValidator);

    auto *prixValidator = new QRegularExpressionValidator(
        QRegularExpression("^\\d{0,9}([\\.,]\\d{0,2})?$"), this);
    ui->emailEdit_10->setValidator(prixValidator);

    auto *quantiteValidator = new QIntValidator(0, 1000000, this);
    ui->emailEdit_9->setValidator(quantiteValidator);
}

bool MainWindow::validerSaisieFacture(QString &matriculeFiscal,
                                      QString &nomEntreprise,
                                      QString &raisonSociale,
                                      QString &typeFacture,
                                      double &prixUnitaire,
                                      int &quantite,
                                      QString &description)
{
    matriculeFiscal = ui->emailEdit_13->text().trimmed();
    nomEntreprise = ui->emailEdit_15->text().trimmed();
    raisonSociale = ui->emailEdit_7->text().trimmed();
    typeFacture = ui->departmentComboBox_5->currentText().trimmed();
    description = ui->emailEdit_14->text().trimmed();

    if (matriculeFiscal.isEmpty() || nomEntreprise.isEmpty() || raisonSociale.isEmpty()) {
        QMessageBox::warning(this,
                             "Validation",
                             "Matricule fiscal, nom de l'entreprise et ID client/fournisseur sont obligatoires.");
        return false;
    }

    const QRegularExpression matriculePattern("^[A-Za-z0-9\\-/]{4,20}$");
    if (!matriculePattern.match(matriculeFiscal).hasMatch()) {
        QMessageBox::warning(this,
                             "Validation",
                             "Matricule fiscal invalide (4 a 20 caracteres: lettres, chiffres, - ou /).");
        ui->emailEdit_13->setFocus();
        return false;
    }

    const QRegularExpression nomEntreprisePattern(QStringLiteral("^[A-Za-z\\x{00C0}-\\x{00FF}0-9\\s\\-'.&,]{2,100}$"));
    if (!nomEntreprisePattern.match(nomEntreprise).hasMatch()) {
        QMessageBox::warning(this,
                             "Validation",
                             "Nom de l'entreprise invalide (2 à 100 caractères).");
        ui->emailEdit_15->setFocus();
        return false;
    }

    bool idCfOk = false;
    const int idCf = raisonSociale.toInt(&idCfOk);
    if (!idCfOk || idCf <= 0) {
        QMessageBox::warning(this,
                             "Validation",
                             "ID client/fournisseur invalide (entier positif).");
        ui->emailEdit_7->setFocus();
        return false;
    }

    if (typeFacture.isEmpty()) {
        QMessageBox::warning(this,
                             "Validation",
                             "Le type de facture est obligatoire.");
        return false;
    }

    QString prixTexte = ui->emailEdit_10->text().trimmed();
    prixTexte.replace(',', '.');
    bool prixOk = false;
    prixUnitaire = prixTexte.toDouble(&prixOk);
    if (!prixOk || prixUnitaire <= 0.0) {
        QMessageBox::warning(this,
                             "Validation",
                             "Prix unitaire invalide. La valeur doit etre strictement positive.");
        ui->emailEdit_10->setFocus();
        return false;
    }
    // Oracle: PRIX_UNITAIRE NUMBER(14,2) -> max 999999999999.99
    const double kMaxPrixUnitaire = 999999999999.99;
    const double prixArrondi2Dec = std::round(prixUnitaire * 100.0) / 100.0;
    if (std::fabs(prixUnitaire - prixArrondi2Dec) > 0.000001) {
        QMessageBox::warning(this,
                             "Validation",
                             "Prix unitaire invalide. Maximum 2 decimales autorisees.");
        ui->emailEdit_10->setFocus();
        return false;
    }
    if (prixArrondi2Dec > kMaxPrixUnitaire) {
        QMessageBox::warning(this,
                             "Validation",
                             "Prix unitaire trop grand pour la base Oracle (max: 999999999999.99).");
        ui->emailEdit_10->setFocus();
        return false;
    }
    prixUnitaire = prixArrondi2Dec;

    bool quantiteOk = false;
    quantite = ui->emailEdit_9->text().trimmed().toInt(&quantiteOk);
    if (!quantiteOk || quantite <= 0) {
        QMessageBox::warning(this,
                             "Validation",
                             "Quantite invalide. La valeur doit etre un entier positif.");
        ui->emailEdit_9->setFocus();
        return false;
    }
    // Oracle: QUANTITE NUMBER(10)
    const int kMaxQuantiteOracleSafe = 999999999;
    if (quantite > kMaxQuantiteOracleSafe) {
        QMessageBox::warning(this,
                             "Validation",
                             "Quantite trop grande pour la base Oracle.");
        ui->emailEdit_9->setFocus();
        return false;
    }

    if (description.length() > 255) {
        QMessageBox::warning(this,
                             "Validation",
                             "Description trop longue (255 caracteres maximum).");
        return false;
    }

    return true;
}

void MainWindow::chargerFactures(const QString &filtre)
{
    const bool signatureReady = Facture::ensureSignatureColumns();

    QSqlQuery query;
    QString sql =
        "SELECT ID_FACTURE, MATRICULE_FISCAL, NOM_ENTREPRISE, NOM_RAISON_SOCIALE AS id_cf, "
        "TYPE_FACTURE, TO_CHAR(DATE_FACTURATION, 'YYYY-MM-DD') AS date_facturation, "
        "PRIX_UNITAIRE, QUANTITE, DESCRIPTION, ";
    sql += signatureReady ? "NVL(SIGNATURE_ELEC, '') AS SIGNATURE_ELEC, NVL(SIGNATURE_DRAW, '') AS SIGNATURE_DRAW_RAW "
                          : "'' AS SIGNATURE_ELEC, '' AS SIGNATURE_DRAW_RAW ";
    sql +=
        "FROM FACTURE";

    if (!filtre.isEmpty()) {
        sql += " WHERE TO_CHAR(ID_FACTURE) LIKE :f OR UPPER(MATRICULE_FISCAL) LIKE UPPER(:f) "
               "OR UPPER(NOM_ENTREPRISE) LIKE UPPER(:f) OR UPPER(NOM_RAISON_SOCIALE) LIKE UPPER(:f) "
               "OR UPPER(TYPE_FACTURE) LIKE UPPER(:f) OR TO_CHAR(DATE_FACTURATION, 'YYYY-MM-DD') LIKE :f "
               "OR TO_CHAR(PRIX_UNITAIRE) LIKE :f OR TO_CHAR(QUANTITE) LIKE :f "
               "OR UPPER(DESCRIPTION) LIKE UPPER(:f)";
        if (signatureReady) {
            sql += " OR UPPER(NVL(SIGNATURE_ELEC, '')) LIKE UPPER(:f)";
        }
    }
    sql += " ORDER BY " + ordreSqlTriFacture();

    query.prepare(sql);
    if (!filtre.isEmpty()) {
        query.bindValue(":f", "%" + filtre + "%");
    }

    if (!query.exec()) {
        QMessageBox::critical(this, "Erreur SQL", "Impossible de charger les factures:\n" + query.lastError().text());
        ui->employeeTable_5->setRowCount(0);
        return;
    }

    ui->employeeTable_5->setRowCount(0);

    int row = 0;
    while (query.next()) {
        ui->employeeTable_5->insertRow(row);
        const QString signatureRaw = query.value(10).toString();
        const int idFacture = query.value(0).toInt();
        const QString matriculeFiscal = query.value(1).toString();
        const QString nomEntreprise = query.value(2).toString();
        const QString raisonSociale = query.value(3).toString();
        const QString typeFacture = query.value(4).toString();
        const QString dateFacture = query.value(5).toString();
        const double prixUnitaire = query.value(6).toDouble();
        const int quantite = query.value(7).toInt();
        const QString description = query.value(8).toString();
        const QString signatureToken = query.value(9).toString();
        QString invoiceHtml;
        QString localInvoiceFilePath;
        const QString qrPayload = writeInvoiceHtmlAndBuildUrl(idFacture,
                                                              matriculeFiscal,
                                                              nomEntreprise,
                                                              raisonSociale,
                                                              typeFacture,
                                                              dateFacture,
                                                              prixUnitaire,
                                                              quantite,
                                                              description,
                                                              signatureToken,
                                                              signatureRaw,
                                                              &invoiceHtml,
                                                              &localInvoiceFilePath);

        for (int col = 0; col < 11; ++col) {
            QString text;
            if (col <= 8) {
                text = query.value(col).toString();
            }
            if (col == 6) {
                text = QString::number(query.value(col).toDouble(), 'f', 2);
            }
            auto *item = new QTableWidgetItem;
            if (col == kFactureSignatureColumn) {
                item->setData(Qt::UserRole, signatureRaw);
                item->setData(Qt::UserRole + 1, qrPayload);
                item->setData(Qt::UserRole + 2, invoiceHtml);
                item->setData(Qt::UserRole + 3, localInvoiceFilePath);
                if (signatureRaw.trimmed().isEmpty()) {
                    item->setText("Non");
                    item->setTextAlignment(Qt::AlignCenter);
                    item->setForeground(QColor(185, 28, 28));
                } else {
                    item->setText("Voir");
                    item->setTextAlignment(Qt::AlignCenter);
                    item->setForeground(QColor(22, 101, 52));
                    item->setToolTip(QStringLiteral("Double-clic pour afficher la signature"));
                    const QByteArray imgBytes = QByteArray::fromBase64(signatureRaw.toLatin1());
                    QImage sigImg;
                    if (sigImg.loadFromData(imgBytes)) {
                        item->setIcon(QIcon(QPixmap::fromImage(sigImg.scaled(42, 18, Qt::KeepAspectRatio, Qt::SmoothTransformation))));
                    }
                }
            } else if (col == kFactureQrColumn) {
                item->setData(Qt::UserRole, qrPayload);
                item->setText(QStringLiteral("QR"));
                item->setTextAlignment(Qt::AlignCenter);
                item->setForeground(QColor("#166534"));
                item->setToolTip(QStringLiteral("Double-clic pour ouvrir la facture web/QR"));
                const QPixmap tinyQr = factureQrPixmap(qrPayload, 48);
                if (!tinyQr.isNull()) {
                    item->setIcon(QIcon(tinyQr));
                }
            } else {
                item->setText(text);
            }
            ui->employeeTable_5->setItem(row, col, item);
        }
        ++row;
    }

    ui->modifyBtn_5->setEnabled(false);
    ui->deleteBtn_5->setEnabled(false);
    mettreAJourStatistiquesFinance();
}

void MainWindow::chargerFactureParId(int idFacture)
{
    QSqlQuery query;
    QString sql = "SELECT ID_FACTURE, MATRICULE_FISCAL, NOM_ENTREPRISE, NOM_RAISON_SOCIALE, "
                  "TYPE_FACTURE, DATE_FACTURATION, PRIX_UNITAIRE, QUANTITE, DESCRIPTION, "
                  "ID_CLIENT, ID_FOURNISSEUR";
    sql += Facture::ensureSignatureColumns() ? ", NVL(SIGNATURE_ELEC, '') AS SIGNATURE_ELEC" : ", '' AS SIGNATURE_ELEC";
    sql += " FROM FACTURE WHERE ID_FACTURE = :id";
    query.prepare(sql);
    query.bindValue(":id", idFacture);

    if (!query.exec()) {
        QMessageBox::critical(this, "Erreur SQL", "Impossible de charger la facture:\n" + query.lastError().text());
        return;
    }
    if (!query.next()) {
        QMessageBox::warning(this, "Facture", "Facture introuvable.");
        return;
    }

    m_idFactureEnEdition = query.value(0).toInt();
    m_idClientEnEdition = query.value(9).isNull() ? -1 : query.value(9).toInt();
    m_idFournisseurEnEdition = query.value(10).isNull() ? -1 : query.value(10).toInt();

    ui->emailEdit_13->setText(query.value(1).toString());
    ui->emailEdit_15->setText(query.value(2).toString());
    ui->emailEdit_7->setText(query.value(3).toString());
    ui->emailEdit_10->setText(QString::number(query.value(6).toDouble(), 'f', 2));
    ui->emailEdit_9->setText(QString::number(query.value(7).toInt()));
    ui->emailEdit_14->setText(query.value(8).toString());

    const QString typeFacture = query.value(4).toString();
    int typeIndex = ui->departmentComboBox_5->findText(typeFacture);
    if (typeIndex < 0 && !typeFacture.isEmpty()) {
        ui->departmentComboBox_5->addItem(typeFacture);
        typeIndex = ui->departmentComboBox_5->findText(typeFacture);
    }
    if (typeIndex >= 0) {
        ui->departmentComboBox_5->setCurrentIndex(typeIndex);
    }

    const QVariant dateValue = query.value(5);
    QDate dateFacturation = dateValue.toDate();
    if (!dateFacturation.isValid()) {
        dateFacturation = dateValue.toDateTime().date();
    }
    if (!dateFacturation.isValid()) {
        dateFacturation = QDate::fromString(dateValue.toString(), "yyyy-MM-dd");
    }
    if (!dateFacturation.isValid()) {
        dateFacturation = QDate::currentDate();
    }
    ui->startDateEdit_5->setDate(dateFacturation);

    ui->saveclientBtn_5->setText("Modifier");
}

void MainWindow::viderFormulaireFacture()
{
    m_idFactureEnEdition = -1;
    m_idClientEnEdition = -1;
    m_idFournisseurEnEdition = -1;

    ui->emailEdit_13->clear();
    ui->emailEdit_15->clear();
    ui->emailEdit_7->clear();
    ui->emailEdit_10->clear();
    ui->emailEdit_9->clear();
    ui->emailEdit_14->clear();
    ui->departmentComboBox_5->setCurrentIndex(0);
    ui->startDateEdit_5->setDate(QDate::currentDate());
    ui->saveclientBtn_5->setText("Enregistrer");
}

void MainWindow::afficherClients()
{
    if (!ui->employeeTable_2) {
        return;
    }

    std::unique_ptr<QSqlQueryModel> model(Client::afficher());
    if (!model || model->lastError().isValid()) {
        QMessageBox::warning(this, "Clients", "Impossible de charger la liste des clients.");
        ui->employeeTable_2->setRowCount(0);
        return;
    }

    while (model->canFetchMore()) {
        model->fetchMore();
    }

    ui->employeeTable_2->setRowCount(0);
    const int rowCount = model->rowCount();
    const int colCount = qMin(8, model->columnCount());
    for (int row = 0; row < rowCount; ++row) {
        ui->employeeTable_2->insertRow(row);
        for (int col = 0; col < colCount; ++col) {
            const QString text = model->data(model->index(row, col)).toString();
            ui->employeeTable_2->setItem(row, col, new QTableWidgetItem(text));
        }
    }

    idClientSelectionne = -1;
    activerBoutonsClient(false);
}

void MainWindow::viderChampsClient()
{
    // Ne faites que vider les champs, ne modifiez pas la structure
    ui->firstNameEdit_2->clear();
    ui->lastNameEdit_2->clear();
    ui->emailEdit_2->clear();
    ui->phoneEdit_2->clear();
    if (auto *cinEdit = clientCinEdit(this))
        cinEdit->clear();
    if (auto *genreCombo = clientGenreCombo(this))
        genreCombo->setCurrentIndex(0);
    ui->startDateEdit_2->setDate(QDate::currentDate());
    ui->departmentComboBox_8->setCurrentIndex(0);
    idClientSelectionne = -1;
    activerBoutonsClient(false);
    ui->saveEmployeeBtn_2->setText("Enregistrer");
    resetStyles();
}
void MainWindow::configurerTriAvanceClient()
{
    m_menuTriClient = new QMenu(this);
    m_menuTriClient->setTitle("Tri avance clients");

    auto addTriAction = [this](QMenu *menu,
                               const QString &texteAction,
                               int col,
                               bool asc,
                               const QString &libelle) {
        QAction *action = menu->addAction(texteAction);
        connect(action, &QAction::triggered, this, [this, col, asc, libelle]() {
            appliquerTriClient(col, asc, libelle);
        });
    };

    // Menu ID
    QMenu *menuId = m_menuTriClient->addMenu("ID Client");
    addTriAction(menuId, "ID croissant", 0, true, "ID ASC");
    addTriAction(menuId, "ID decroissant", 0, false, "ID DESC");

    // Menu Nom
    QMenu *menuNom = m_menuTriClient->addMenu("Nom");
    addTriAction(menuNom, "A -> Z", 1, true, "Nom ASC");
    addTriAction(menuNom, "Z -> A", 1, false, "Nom DESC");

    // Menu Prénom
    QMenu *menuPrenom = m_menuTriClient->addMenu("Prénom");
    addTriAction(menuPrenom, "A -> Z", 2, true, "Prénom ASC");
    addTriAction(menuPrenom, "Z -> A", 2, false, "Prénom DESC");

    // Menu Email
    QMenu *menuEmail = m_menuTriClient->addMenu("Email");
    addTriAction(menuEmail, "A -> Z", 3, true, "Email ASC");
    addTriAction(menuEmail, "Z -> A", 3, false, "Email DESC");

    // Menu Téléphone
    QMenu *menuTel = m_menuTriClient->addMenu("Téléphone");
    addTriAction(menuTel, "Croissant", 4, true, "Téléphone ASC");
    addTriAction(menuTel, "Decroissant", 4, false, "Téléphone DESC");

    // Menu Date d'enregistrement
    QMenu *menuDate = m_menuTriClient->addMenu("Date d'enregistrement");
    addTriAction(menuDate, "Plus anciennes", 5, true, "Date ASC");
    addTriAction(menuDate, "Plus recentes", 5, false, "Date DESC");

    // Menu Achats
    QMenu *menuAchats = m_menuTriClient->addMenu("Type d'achats");
    addTriAction(menuAchats, "A -> Z", 6, true, "Achats ASC");
    addTriAction(menuAchats, "Z -> A", 6, false, "Achats DESC");

    // Séparateur
    m_menuTriClient->addSeparator();

    // Tri par défaut
    addTriAction(m_menuTriClient, "Tri par defaut (ID ASC)", 0, true, "ID ASC");
}


void MainWindow::appliquerTriClient(int colonne, bool asc, const QString &libelle)
{
    if (!ui->employeeTable_2) {
        return;
    }

    // Sauvegarder les paramètres de tri
    m_colTriClient = qMax(0, qMin(colonne, ui->employeeTable_2->columnCount() - 1));
    m_triClientAsc = asc;
    m_libelleTriClient = libelle;

    // Effectuer le tri du tableau
    ui->employeeTable_2->sortItems(m_colTriClient,
                                   m_triClientAsc ? Qt::AscendingOrder : Qt::DescendingOrder);

    // Mettre à jour le libellé du bouton de tri
    actualiserLibelleTriClient();

    // Réappliquer le filtre existant après le tri
    if (ui->searchInput_2) {
        filtrerClients(ui->searchInput_2->text().trimmed());
    }
}

void MainWindow::actualiserLibelleTriClient()
{
    if (!ui->sortBtn_2) {
        return;
    }
    ui->sortBtn_2->setText("Tri: " + m_libelleTriClient);
}

void MainWindow::filtrerClients(const QString &filtre)
{
    if (!ui->employeeTable_2) {
        return;
    }

    const QString f = filtre.trimmed();
    for (int row = 0; row < ui->employeeTable_2->rowCount(); ++row) {
        bool visible = f.isEmpty();
        if (!visible) {
            for (int col = 0; col < ui->employeeTable_2->columnCount(); ++col) {
                QTableWidgetItem *it = ui->employeeTable_2->item(row, col);
                if (it && it->text().contains(f, Qt::CaseInsensitive)) {
                    visible = true;
                    break;
                }
            }
        }
        ui->employeeTable_2->setRowHidden(row, !visible);
    }
}

void MainWindow::setupCommandesClientsSection()
{
    if (m_commandesClientsTable) {
        return;
    }
    m_commandesClientsTable = commandesClientsTableWidget(this);
    m_searchCommandesClients = commandesClientsSearchInput(this);
    m_sortCommandesClientsBtn = commandesClientsSortBtn(this);

    if (!m_commandesClientsTable || !m_searchCommandesClients || !m_sortCommandesClientsBtn) {
        return;
    }

    m_commandesClientsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_commandesClientsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_commandesClientsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_commandesClientsTable->setShowGrid(false);
    m_commandesClientsTable->verticalHeader()->setVisible(false);
    m_commandesClientsTable->horizontalHeader()->setStretchLastSection(true);
    m_commandesClientsTable->setMinimumHeight(220);
    if (ui->employeeTable_2) {
        m_commandesClientsTable->setStyleSheet(ui->employeeTable_2->styleSheet());
        m_commandesClientsTable->setFont(ui->employeeTable_2->font());
        m_commandesClientsTable->setAlternatingRowColors(ui->employeeTable_2->alternatingRowColors());
    }

    m_menuTriCommandesClients = new QMenu(this);
    auto addTri = [this](const QString &text, int col, bool asc, const QString &label) {
        QAction *a = m_menuTriCommandesClients->addAction(text);
        connect(a, &QAction::triggered, this, [this, col, asc, label]() {
            appliquerTriCommandesClients(col, asc, label);
        });
    };
    addTri(QStringLiteral("Date commande - recentes"), 8, false, QStringLiteral("Date commande DESC"));
    addTri(QStringLiteral("Date commande - anciennes"), 8, true, QStringLiteral("Date commande ASC"));
    addTri(QStringLiteral("Nom A->Z"), 2, true, QStringLiteral("Nom ASC"));
    addTri(QStringLiteral("Nom Z->A"), 2, false, QStringLiteral("Nom DESC"));
    addTri(QStringLiteral("ID client ASC"), 1, true, QStringLiteral("ID client ASC"));
    addTri(QStringLiteral("ID client DESC"), 1, false, QStringLiteral("ID client DESC"));

    connect(m_sortCommandesClientsBtn, &QPushButton::clicked, this, [this]() {
        if (!m_menuTriCommandesClients || !m_sortCommandesClientsBtn) {
            return;
        }
        const QPoint p = m_sortCommandesClientsBtn->mapToGlobal(QPoint(0, m_sortCommandesClientsBtn->height()));
        m_menuTriCommandesClients->popup(p);
    });

    connect(m_searchCommandesClients, &QLineEdit::textChanged, this, [this](const QString &text) {
        filtrerCommandesClients(text.trimmed());
    });
}

void MainWindow::chargerCommandesClients()
{
    if (!m_commandesClientsTable) {
        return;
    }

    QSqlQuery query(QSqlDatabase::database());
    const QString sql = QStringLiteral(
        "SELECT ID_CMD, ID_CLIENT, NOM, PRENOM, CIN, EMAIL, TELEPHONE, ACHAT, DATE_CMD "
        "FROM ( "
        "    SELECT TO_CHAR(F.ID_FACTURE) AS ID_CMD, "
        "           C.ID_CLIENT, C.NOM, C.PRENOM, C.CIN, C.EMAIL, C.TELEPHONE, C.ACHATS AS ACHAT, "
        "           F.TYPE_FACTURE AS TYPE_CMD, TO_CHAR(F.DATE_FACTURATION,'DD/MM/YYYY') AS DATE_CMD, "
        "           TO_CHAR(F.QUANTITE) AS QUANTITE, TO_CHAR(F.PRIX_UNITAIRE) AS PRIX, "
        "           F.DATE_FACTURATION AS DATE_SORT, 1 AS SRC_ORDER "
        "    FROM FACTURE F JOIN CLIENT C ON C.ID_CLIENT = F.ID_CLIENT "
        "    WHERE F.ID_CLIENT IS NOT NULL "
        "      AND UPPER(C.ACHATS) IN ('ACHAT OLIVE','PRESSION OLIVE') "
        "    UNION ALL "
        "    SELECT 'CLIENT-' || TO_CHAR(C.ID_CLIENT) AS ID_CMD, "
        "           C.ID_CLIENT, C.NOM, C.PRENOM, C.CIN, C.EMAIL, C.TELEPHONE, C.ACHATS AS ACHAT, "
        "           C.ACHATS AS TYPE_CMD, TO_CHAR(C.DATE_ENREGISTREMENT,'DD/MM/YYYY') AS DATE_CMD, "
        "           '-' AS QUANTITE, '-' AS PRIX, "
        "           C.DATE_ENREGISTREMENT AS DATE_SORT, 2 AS SRC_ORDER "
        "    FROM CLIENT C "
        "    WHERE UPPER(C.ACHATS) IN ('ACHAT OLIVE','PRESSION OLIVE') "
        "      AND NOT EXISTS (SELECT 1 FROM FACTURE F2 WHERE F2.ID_CLIENT = C.ID_CLIENT) "
        ") T "
        "ORDER BY T.DATE_SORT DESC, T.SRC_ORDER ASC, T.ID_CLIENT DESC");

    if (!query.exec(sql)) {
        m_commandesClientsTable->setRowCount(0);
        return;
    }

    m_commandesClientsTable->setRowCount(0);
    int row = 0;
    while (query.next()) {
        m_commandesClientsTable->insertRow(row);
        for (int col = 0; col < m_commandesClientsTable->columnCount(); ++col) {
            const QString text = query.value(col).toString();
            m_commandesClientsTable->setItem(row, col, new QTableWidgetItem(text));
        }
        ++row;
    }

    if (m_searchCommandesClients) {
        filtrerCommandesClients(m_searchCommandesClients->text().trimmed());
    }
    appliquerTriCommandesClients(m_colTriCommandesClients, m_triCommandesClientsAsc, m_libelleTriCommandesClients);
}

void MainWindow::filtrerCommandesClients(const QString &filtre)
{
    if (!m_commandesClientsTable) {
        return;
    }
    const QString f = filtre.trimmed();
    for (int row = 0; row < m_commandesClientsTable->rowCount(); ++row) {
        bool visible = f.isEmpty();
        if (!visible) {
            for (int col = 0; col < m_commandesClientsTable->columnCount(); ++col) {
                QTableWidgetItem *it = m_commandesClientsTable->item(row, col);
                if (it && it->text().contains(f, Qt::CaseInsensitive)) {
                    visible = true;
                    break;
                }
            }
        }
        m_commandesClientsTable->setRowHidden(row, !visible);
    }
}

void MainWindow::appliquerTriCommandesClients(int colonne, bool asc, const QString &libelle)
{
    if (!m_commandesClientsTable || m_commandesClientsTable->columnCount() <= 0) {
        return;
    }
    m_colTriCommandesClients = qMax(0, qMin(colonne, m_commandesClientsTable->columnCount() - 1));
    m_triCommandesClientsAsc = asc;
    m_libelleTriCommandesClients = libelle;
    m_commandesClientsTable->sortItems(m_colTriCommandesClients,
                                       m_triCommandesClientsAsc ? Qt::AscendingOrder : Qt::DescendingOrder);
    actualiserLibelleTriCommandesClients();
}

void MainWindow::actualiserLibelleTriCommandesClients()
{
    if (m_sortCommandesClientsBtn) {
        m_sortCommandesClientsBtn->setText(QStringLiteral("Tri: ") + m_libelleTriCommandesClients);
    }
}



void MainWindow::activerBoutonsClient(bool etat)
{
    if (ui->modifyBtn_2) ui->modifyBtn_2->setEnabled(etat);
    if (ui->deleteBtn_2) ui->deleteBtn_2->setEnabled(etat);
}

void MainWindow::on_employerBtn_clicked()
{
    if (ui->stackedWidget && ui->employerPage) {
        ui->stackedWidget->setCurrentWidget(ui->employerPage);
    }
    if (ui->employerTabWidget && ui->addTab) {
        ui->employerTabWidget->setCurrentWidget(ui->addTab);
    }
    if (ui->searchAndActionsWidget) {
        ui->searchAndActionsWidget->setVisible(true);
    }
    loadEmployees();
}

void MainWindow::on_profileBtn_clicked()
{
    if (ui->stackedWidget && ui->profilePage) {
        ui->stackedWidget->setCurrentWidget(ui->profilePage);
    }

    if (!ui->profileInfoContent || !ui->profilePhotoLabel)
        return;

    const QString identifier = m_loggedInEmail.trimmed().toLower();
    if (identifier.isEmpty()) {
        ui->profileInfoContent->setText(QStringLiteral("Aucun utilisateur connecté."));
        ui->profilePhotoLabel->setText(QStringLiteral("Aucune photo"));
        return;
    }

    QSqlQuery q;
    q.prepare(QStringLiteral(
        "SELECT ID, NOM, EMAIL, TELEPHONE, DEPART, POSTE, SALAIRE, DATE_DEBUT "
        "FROM EMPLOYEE "
        "WHERE LOWER(TRIM(EMAIL)) = :id OR LOWER(TRIM(NOM)) = :id"));
    q.bindValue(QStringLiteral(":id"), identifier);

    if (!q.exec() || !q.next()) {
        ui->profileInfoContent->setText(
            QStringLiteral("Profil introuvable pour l'identifiant: %1").arg(m_loggedInEmail));
        ui->profilePhotoLabel->setText(QStringLiteral("Aucune photo"));
        return;
    }

    const QString fullName = q.value(QStringLiteral("NOM")).toString().trimmed();
    const QString email = q.value(QStringLiteral("EMAIL")).toString().trimmed();
    const QString phone = q.value(QStringLiteral("TELEPHONE")).toString().trimmed();
    const QString depart = q.value(QStringLiteral("DEPART")).toString().trimmed();
    const QString poste = q.value(QStringLiteral("POSTE")).toString().trimmed();
    const QString salaire = q.value(QStringLiteral("SALAIRE")).toString().trimmed();
    const QDate startDate = q.value(QStringLiteral("DATE_DEBUT")).toDate();
    const QString initials = fullName.isEmpty()
                                 ? QStringLiteral("?")
                                 : fullName.left(1).toUpper();
    const QString safeEmail = email.isEmpty() ? identifier : email.toLower().trimmed();
    const QString photosRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir photosDir(photosRoot);
    photosDir.mkpath(QStringLiteral("profile_photos"));
    const QString photoPath = photosDir.filePath(
        QStringLiteral("profile_photos/%1.png")
            .arg(QString(safeEmail).replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]")), QStringLiteral("_"))));

    auto refreshProfilePhoto = [this, photoPath, initials]() {
        QPixmap pix;
        if (QFileInfo::exists(photoPath) && pix.load(photoPath)) {
            ui->profilePhotoLabel->setPixmap(
                pix.scaled(ui->profilePhotoLabel->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            ui->profilePhotoLabel->setText(QString());
        } else {
            ui->profilePhotoLabel->setPixmap(QPixmap());
            ui->profilePhotoLabel->setText(initials);
        }
    };

    refreshProfilePhoto();
    ui->profileInfoContent->setText(
        QStringLiteral("Nom complet : %1\n"
                       "Email : %2\n"
                       "Téléphone : %3\n"
                       "Département : %4\n"
                       "Poste : %5\n"
                       "Salaire : %6\n"
                       "Date de début : %7")
            .arg(fullName.isEmpty() ? QStringLiteral("-") : fullName,
                 email.isEmpty() ? QStringLiteral("-") : email,
                 phone.isEmpty() ? QStringLiteral("-") : phone,
                 depart.isEmpty() ? QStringLiteral("-") : depart,
                 poste.isEmpty() ? QStringLiteral("-") : poste,
                 salaire.isEmpty() ? QStringLiteral("-") : salaire,
                 startDate.isValid() ? startDate.toString(QStringLiteral("dd/MM/yyyy")) : QStringLiteral("-")));

    if (ui->uploadPhotoBtn) {
        disconnect(ui->uploadPhotoBtn, nullptr, this, nullptr);
        connect(ui->uploadPhotoBtn, &QPushButton::clicked, this, [this, photoPath, refreshProfilePhoto]() {
            const QString selected = QFileDialog::getOpenFileName(
                this,
                QStringLiteral("Choisir une photo de profil"),
                QString(),
                QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
            if (selected.isEmpty())
                return;

            QPixmap pix(selected);
            if (pix.isNull()) {
                QMessageBox::warning(this, QStringLiteral("Photo"), QStringLiteral("Impossible de charger cette image."));
                return;
            }

            if (!pix.save(photoPath, "PNG")) {
                QMessageBox::warning(this, QStringLiteral("Photo"), QStringLiteral("Impossible d'enregistrer la photo."));
                return;
            }
            refreshProfilePhoto();
        });
    }

    if (ui->removePhotoBtn) {
        disconnect(ui->removePhotoBtn, nullptr, this, nullptr);
        connect(ui->removePhotoBtn, &QPushButton::clicked, this, [this, photoPath, refreshProfilePhoto]() {
            if (QFileInfo::exists(photoPath))
                QFile::remove(photoPath);
            refreshProfilePhoto();
        });
    }
}

void MainWindow::on_clientsBtn_clicked()
{
    if (ui->stackedWidget && ui->clientsPage) {
        ui->stackedWidget->setCurrentWidget(ui->clientsPage);
    }
    if (ui->clientTabWidget_2 && ui->addTab_2) {
        ui->clientTabWidget_2->setCurrentWidget(ui->addTab_2);
    }
    chargerCommandesClients();
    afficherClients();
}

void MainWindow::on_equipmentBtn_clicked()
{
    if (ui->stackedWidget && ui->equipmentsPage) {
        ui->stackedWidget->setCurrentWidget(ui->equipmentsPage);
    }
    if (ui->ProductsTab && ui->addTab_3) {
        ui->ProductsTab->setCurrentWidget(ui->addTab_3);
    }
    loadEquipmentTable();
}

void MainWindow::on_financeBtn_clicked()
{
    if (ui->stackedWidget && ui->financePage) {
        ui->stackedWidget->setCurrentWidget(ui->financePage);
    }
    if (ui->VentesTab_2 && ui->addTab_5) {
        ui->VentesTab_2->setCurrentWidget(ui->addTab_5);
    }
    mettreAJourStatistiquesFinance();
}

void MainWindow::on_fournisseursBtn_clicked()
{
    if (ui->stackedWidget && ui->fournisseurPage) {
        ui->stackedWidget->setCurrentWidget(ui->fournisseurPage);
    }
    if (ui->VentesTab && ui->addTab_4) {
        ui->VentesTab->setCurrentWidget(ui->addTab_4);
    }
    mettreAJourStatistiquesFournisseur();
}

void MainWindow::on_stockBtn_clicked()
{
    if (ui->stackedWidget && ui->stockPage) {
        ui->stackedWidget->setCurrentWidget(ui->stockPage);
    }
    if (ui->stockTabWidget && ui->stockAddTab) {
        ui->stockTabWidget->setCurrentWidget(ui->stockAddTab);
    }
    loadStockTable();
}

void MainWindow::on_AproposBtn_clicked()
{
    if (ui->stackedWidget && ui->aboutPage) {
        ui->stackedWidget->setCurrentWidget(ui->aboutPage);
    }
}

void MainWindow::on_saveEmployeeBtn_2_clicked()
{
    // Validation complète des données
    if (!validerSaisieClient()) {
        return;
    }

    // Récupération des données validées
    QString nom = ui->firstNameEdit_2->text().trimmed();
    QString prenom = ui->lastNameEdit_2->text().trimmed();
    QString email = ui->emailEdit_2->text().trimmed();
    QString telephone = nettoyerTelephone(ui->phoneEdit_2->text().trimmed());
    const QString cin = clientCinEdit(this) ? clientCinEdit(this)->text().trimmed() : QString();
    const QString genre = clientGenreCombo(this) ? clientGenreCombo(this)->currentText().trimmed() : QString();
    QDate date = ui->startDateEdit_2->date();
    QString achats = ui->departmentComboBox_8->currentText().trimmed();

    if (idClientSelectionne == -1) {
        if (cinExisteDeja(cin, -1)) {
            const ExistingClientInfo existing = fetchClientByCin(cin);
            QString details;
            if (existing.found) {
                details = QStringLiteral("ID: %1\nNom: %2\nPrenom: %3\nCIN: %4\nEmail: %5\nTelephone: %6\nGenre: %7\nDate: %8\nAchats: %9")
                              .arg(existing.id)
                              .arg(existing.nom)
                              .arg(existing.prenom)
                              .arg(existing.cin)
                              .arg(existing.email)
                              .arg(existing.telephone)
                              .arg(existing.genre)
                              .arg(existing.dateEnregistrement.isValid() ? existing.dateEnregistrement.toString("dd/MM/yyyy")
                                                                         : QStringLiteral("-"))
                              .arg(existing.achats);
            } else {
                details = QStringLiteral("Le client existe deja, mais les details n'ont pas pu etre recuperes.");
            }

            QMessageBox duplicateBox(this);
            duplicateBox.setWindowTitle(QStringLiteral("CIN deja utilise"));
            duplicateBox.setIcon(QMessageBox::Information);
            duplicateBox.setText(QStringLiteral("Ce CIN est déjà utilisé. Voici les informations du client existant."));
            duplicateBox.setInformativeText(details);
            QPushButton *editBtn = duplicateBox.addButton(QStringLiteral("Modifier les données saisies"), QMessageBox::RejectRole);
            QPushButton *orderBtn = duplicateBox.addButton(QStringLiteral("Ajouter une commande pour ce client"), QMessageBox::AcceptRole);
            duplicateBox.exec();

            if (duplicateBox.clickedButton() == orderBtn && existing.found) {
                m_idClientEnEdition = existing.id;
                m_idFactureEnEdition = -1;
                m_idFournisseurEnEdition = -1;

                // Créer directement une nouvelle commande liée au client existant
                // (même ID client, aucun nouveau client).
                const QString typeCommande = achats.isEmpty() ? existing.achats : achats;
                const QString nomComplet = (existing.nom + QLatin1Char(' ') + existing.prenom).trimmed();
                QString insertErr;
                if (!insererCommandeClient(existing.id,
                                           typeCommande,
                                           nomComplet,
                                           existing.cin,
                                           &insertErr)) {
                    QMessageBox::critical(this,
                                          QStringLiteral("Commande"),
                                          QStringLiteral("Impossible d'enregistrer la commande pour ce client existant.\n%1")
                                              .arg(insertErr));
                    return;
                }

                // Ne pas rediriger vers la page facture.
                // On garde juste l'ID client existant pour réutiliser le même id_client
                // lors d'un enregistrement de commande.
                if (ui->stackedWidget && ui->clientsPage) {
                    ui->stackedWidget->setCurrentWidget(ui->clientsPage);
                }
                if (ui->clientTabWidget_2 && ui->addTab_2) {
                    ui->clientTabWidget_2->setCurrentWidget(ui->addTab_2);
                }
                afficherClients();
                chargerCommandesClients();
                if (ui->searchInput_2) {
                    ui->searchInput_2->clear();
                }
                filtrerClients(QString());
                if (ui->employeeTable_2) {
                    for (int row = 0; row < ui->employeeTable_2->rowCount(); ++row) {
                        QTableWidgetItem *idItem = ui->employeeTable_2->item(row, 0);
                        if (!idItem) {
                            continue;
                        }
                        bool okId = false;
                        const int idValue = idItem->text().toInt(&okId);
                        if (okId && idValue == existing.id) {
                            ui->employeeTable_2->setCurrentCell(row, 0);
                            ui->employeeTable_2->selectRow(row);
                            ui->employeeTable_2->scrollToItem(idItem, QAbstractItemView::PositionAtCenter);
                            break;
                        }
                    }
                }
                // Réafficher aussi le client existant dans le formulaire (sous la table)
                if (ui->firstNameEdit_2)
                    ui->firstNameEdit_2->setText(existing.nom);
                if (ui->lastNameEdit_2)
                    ui->lastNameEdit_2->setText(existing.prenom);
                if (ui->emailEdit_2)
                    ui->emailEdit_2->setText(existing.email);
                if (ui->phoneEdit_2)
                    ui->phoneEdit_2->setText(existing.telephone);
                if (auto *cinEdit = clientCinEdit(this))
                    cinEdit->setText(existing.cin);
                if (auto *genreCombo = clientGenreCombo(this)) {
                    int idxGenre = genreCombo->findText(existing.genre.trimmed().toUpper());
                    if (idxGenre >= 0)
                        genreCombo->setCurrentIndex(idxGenre);
                }
                if (ui->startDateEdit_2 && existing.dateEnregistrement.isValid())
                    ui->startDateEdit_2->setDate(existing.dateEnregistrement);
                if (ui->departmentComboBox_8) {
                    int idxAchats = ui->departmentComboBox_8->findText(existing.achats);
                    if (idxAchats >= 0)
                        ui->departmentComboBox_8->setCurrentIndex(idxAchats);
                }
                // Rester en mode ajout client (ne pas basculer en modification d'un client existant).
                idClientSelectionne = -1;
                activerBoutonsClient(false);
                if (ui->saveEmployeeBtn_2)
                    ui->saveEmployeeBtn_2->setText("Enregistrer");
                QMessageBox::information(this,
                                         QStringLiteral("Commande"),
                                         QStringLiteral("Le client existant a ete recharge (ID %1) dans la table et le formulaire.\n"
                                                        "Aucun nouveau client n'a ete cree. Le meme ID en base est conserve.")
                                             .arg(existing.id));
            } else {
                if (editBtn)
                    editBtn->setFocus();
            }
            return;
        }

        // Mode AJOUT (asynchrone pour garder l'UI réactive)
        setClientBusyUi(this, true, QStringLiteral("Ajout du client en cours..."));

        ClientInsertPayload payload{nom, prenom, email, telephone, cin, genre, date, achats};
        auto *watcher = new QFutureWatcher<DbTaskResult>(this);
        connect(watcher, &QFutureWatcher<DbTaskResult>::finished, this, [this, watcher, nom, prenom, cin, email, achats]() {
            const DbTaskResult result = watcher->result();
            watcher->deleteLater();

            setClientBusyUi(this, false);

            if (result.ok) {
                // Ajouter obligatoirement une commande initiale liée au client créé.
                const int createdId = result.createdClientId;
                QString orderErr;
                const QString libelleClient = (nom + QLatin1Char(' ') + prenom).trimmed();
                if (createdId > 0
                    && !insererCommandeClient(createdId, achats, libelleClient, cin, &orderErr)) {
                    QMessageBox::warning(this,
                                         QStringLiteral("Commande client"),
                                         QStringLiteral("Le client est ajouté, mais la commande initiale n'a pas pu être créée.\n%1")
                                             .arg(orderErr));
                }
                QMessageBox::information(this, "Succès",
                                         QString("Client ajoute avec succes.\n\n"
                                                 "Details :\n"
                                                 "- Nom : %1\n"
                                                 "- Prenom : %2\n"
                                                 "- CIN : %3\n"
                                                 "- Email : %4")
                                             .arg(nom).arg(prenom).arg(cin).arg(email));
                viderChampsClient();
                afficherClients();
                chargerCommandesClients();
                mettreAJourStatistiques();
                resetStyles();
            } else {
                QMessageBox::critical(this, "Erreur",
                                      "Echec de l'ajout du client.\n\n"
                                      "Details Oracle/ODBC:\n" + result.error);
            }
        });
        watcher->setFuture(QtConcurrent::run(insertClientInBackground, payload));
    } else {
        // Mode MODIFICATION
        QMessageBox::StandardButton confirmation = QMessageBox::question(
            this, "Confirmer la modification",
            QString("Etes-vous sur de vouloir modifier ce client ?\n\n"
                    "Client ID : %1\n"
                    "Nouveau nom : %2\n"
                    "Nouveau prénom : %3")
                .arg(idClientSelectionne).arg(nom).arg(prenom),
            QMessageBox::Yes | QMessageBox::No);

        if (confirmation == QMessageBox::Yes) {
            setClientBusyUi(this, true, QStringLiteral("Modification du client en cours..."));

            const int idToUpdate = idClientSelectionne;
            ClientUpdatePayload payload{idToUpdate, nom, prenom, email, telephone, cin, genre, date, achats};
            auto *watcher = new QFutureWatcher<DbTaskResult>(this);
            connect(watcher, &QFutureWatcher<DbTaskResult>::finished, this, [this, watcher, idToUpdate]() {
                const DbTaskResult result = watcher->result();
                watcher->deleteLater();
                setClientBusyUi(this, false);

                if (result.ok) {
                    QMessageBox::information(this, "Succès",
                                             QString("Client modifie avec succes.\n\n"
                                                     "Client ID : %1").arg(idToUpdate));
                    viderChampsClient();
                    afficherClients();
                    chargerCommandesClients();
                    mettreAJourStatistiques();
                    resetStyles();
                    ui->saveEmployeeBtn_2->setText("Enregistrer");
                } else {
                    QMessageBox::critical(this, "Erreur",
                                          "Echec de la modification du client.\n\n"
                                          "Details Oracle/ODBC:\n" + result.error);
                }
            });
            watcher->setFuture(QtConcurrent::run(updateClientInBackground, payload));
        }
    }
}

void MainWindow::on_cancelBtn_2_clicked()
{
    viderChampsClient();
    activerBoutonsClient(false);
}

void MainWindow::handleClientModifyRequested()
{
    if (!ui->employeeTable_2) {
        return;
    }
    int row = ui->employeeTable_2->currentRow();
    if (row < 0 || ui->employeeTable_2->isRowHidden(row)) {
        QMessageBox::warning(this, "Modification", "Sélectionnez un client dans la liste.");
        return;
    }

    QTableWidgetItem *idItem = ui->employeeTable_2->item(row, 0);
    bool ok = false;
    const int id = idItem ? idItem->text().trimmed().toInt(&ok) : -1;
    if (!ok || id <= 0) {
        QMessageBox::warning(this, "Modification", "ID client invalide.");
        return;
    }

    Client c;
    if (!c.charger(id)) {
        QMessageBox::warning(this, "Modification", "Impossible de charger ce client depuis la base.");
        return;
    }

    idClientSelectionne = id;
    if (ui->firstNameEdit_2)
        ui->firstNameEdit_2->setText(c.getNom());
    if (ui->lastNameEdit_2)
        ui->lastNameEdit_2->setText(c.getPrenom());
    if (ui->emailEdit_2)
        ui->emailEdit_2->setText(c.getEmail());
    if (ui->phoneEdit_2)
        ui->phoneEdit_2->setText(c.getTelephone());
    if (auto *cinEdit = clientCinEdit(this))
        cinEdit->setText(c.getCin());
    if (auto *genreCombo = clientGenreCombo(this)) {
        const QString g = c.getGenre().trimmed().toUpper();
        int idx = genreCombo->findText(g.isEmpty() ? QStringLiteral("HOMME") : g);
        if (idx < 0) {
            idx = genreCombo->findText(QStringLiteral("HOMME"));
        }
        genreCombo->setCurrentIndex(qMax(0, idx));
    }
    if (ui->startDateEdit_2) {
        QDate d = c.getDateEnregistrement();
        if (!d.isValid())
            d = QDate::currentDate();
        ui->startDateEdit_2->setDate(d);
    }
    if (ui->departmentComboBox_8) {
        const QString achats = c.getAchats().trimmed();
        int idx = ui->departmentComboBox_8->findText(achats);
        if (idx < 0 && !achats.isEmpty()) {
            ui->departmentComboBox_8->addItem(achats);
            idx = ui->departmentComboBox_8->findText(achats);
        }
        if (idx >= 0)
            ui->departmentComboBox_8->setCurrentIndex(idx);
    }

    ui->saveEmployeeBtn_2->setText("Modifier");
    activerBoutonsClient(true);
}

void MainWindow::on_deleteBtn_2_clicked()
{
    if (idClientSelectionne <= 0) {
        QMessageBox::warning(this, "Suppression", "Selectionnez un client a supprimer.");
        return;
    }

    const auto rep = QMessageBox::question(this, "Suppression", "Voulez-vous supprimer ce client ?");
    if (rep != QMessageBox::Yes) {
        return;
    }

    const int idToDelete = idClientSelectionne;
    setClientBusyUi(this, true, QStringLiteral("Suppression du client en cours..."));

    auto *watcher = new QFutureWatcher<DbTaskResult>(this);
    connect(watcher, &QFutureWatcher<DbTaskResult>::finished, this, [this, watcher, idToDelete]() {
        const DbTaskResult result = watcher->result();
        watcher->deleteLater();
        setClientBusyUi(this, false);

        if (!result.ok) {
            QMessageBox::critical(this, "Suppression",
                                  "Echec de la suppression du client.\n\n"
                                  "Details Oracle/ODBC:\n" + result.error);
            return;
        }

        QMessageBox::information(this, "Suppression",
                                 QString("Client supprime avec succes.\n\nClient ID : %1").arg(idToDelete));
        afficherClients();
        chargerCommandesClients();
        viderChampsClient();
    });
    watcher->setFuture(QtConcurrent::run(deleteClientInBackground, idToDelete));
}

void MainWindow::on_searchBtn_2_clicked()
{
    filtrerClients(ui->searchInput_2->text().trimmed());
}

void MainWindow::on_sortBtn_2_clicked()
{
    if (!m_menuTriClient || !ui->sortBtn_2) {
        return;
    }
    const QPoint p = ui->sortBtn_2->mapToGlobal(QPoint(0, ui->sortBtn_2->height()));
    m_menuTriClient->popup(p);
}

void MainWindow::on_exportBtn_2_clicked()
{
    if (!ui->employeeTable_2 || ui->employeeTable_2->rowCount() == 0) {
        QMessageBox::information(this, "Export", "Aucune donnee a exporter.");
        return;
    }

    const QString fileName = QFileDialog::getSaveFileName(
        this,
        "Exporter clients (CSV)",
        QDir::homePath() + "/clients_export.csv",
        "CSV (*.csv)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Export", "Impossible de creer le fichier.");
        return;
    }

    QTextStream out(&file);
    QStringList headers;
    for (int c = 0; c < ui->employeeTable_2->columnCount(); ++c) {
        QTableWidgetItem *h = ui->employeeTable_2->horizontalHeaderItem(c);
        headers << (h ? h->text() : QString("Col%1").arg(c + 1));
    }
    out << headers.join(';') << '\n';

    for (int r = 0; r < ui->employeeTable_2->rowCount(); ++r) {
        if (ui->employeeTable_2->isRowHidden(r)) continue;
        QStringList rowValues;
        for (int c = 0; c < ui->employeeTable_2->columnCount(); ++c) {
            QTableWidgetItem *it = ui->employeeTable_2->item(r, c);
            QString v = it ? it->text() : QString();
            v.replace('"', "\"\"");
            rowValues << ('"' + v + '"');
        }
        out << rowValues.join(';') << '\n';
    }
    file.close();
    QMessageBox::information(this, "Export", "Export termine:\n" + fileName);
}

void MainWindow::on_searchInput_2_textChanged(const QString &text)
{
    filtrerClients(text.trimmed());
}

void MainWindow::on_employeeTable_2_itemClicked(QTableWidgetItem *item)
{
    Q_UNUSED(item);
    if (!ui->employeeTable_2) {
        return;
    }
    const int row = ui->employeeTable_2->currentRow();
    if (row < 0) {
        idClientSelectionne = -1;
        activerBoutonsClient(false);
        return;
    }
    QTableWidgetItem *idItem = ui->employeeTable_2->item(row, 0);
    bool ok = false;
    const int id = idItem ? idItem->text().toInt(&ok) : -1;
    idClientSelectionne = ok ? id : -1;
    activerBoutonsClient(idClientSelectionne > 0);
}


// ==================== METHODES DE CONTROLE DE SAISIE CLIENTS ====================

void MainWindow::resetStyles()
{
    QString defaultStyle = "";
    ui->firstNameEdit_2->setStyleSheet(defaultStyle);
    ui->lastNameEdit_2->setStyleSheet(defaultStyle);
    ui->emailEdit_2->setStyleSheet(defaultStyle);
    ui->phoneEdit_2->setStyleSheet(defaultStyle);
    if (auto *cinEdit = clientCinEdit(this))
        cinEdit->setStyleSheet(defaultStyle);
    ui->emailEdit_17->setStyleSheet(defaultStyle);
    ui->startDateEdit_2->setStyleSheet(defaultStyle);
    ui->departmentComboBox_8->setStyleSheet(defaultStyle);
}

bool MainWindow::validerFormatEmail(const QString& email)
{
    QRegularExpression regex(R"(^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$)");
    return regex.match(email).hasMatch();
}

bool MainWindow::validerFormatTelephone(const QString& telephone)
{
    // Formats acceptés:
    // - 12345678 (8 chiffres)
    // - +21612345678 (indicatif +216)
    // - 0021612345678 (indicatif 00216)
    QRegularExpression telRegex(R"(^(\+216|00216)?[2-9][0-9]{7}$)");
    return telRegex.match(telephone).hasMatch();
}

bool MainWindow::validerFormatNomPrenom(const QString& texte)
{
    // Lettres, espaces, tirets, apostrophes - minimum 2 caractères
    QRegularExpression nomRegex("^[A-Za-z\\x{00C0}-\\x{00FF}\\s\\-']{2,50}$");
    return nomRegex.match(texte).hasMatch();
}

bool MainWindow::validerFormatCin(const QString &cin)
{
    QRegularExpression cinRegex(QStringLiteral("^[0-9]{8}$"));
    return cinRegex.match(cin).hasMatch();
}

QString MainWindow::nettoyerTelephone(const QString& telephone)
{
    QString clean = telephone;
    clean.remove(QRegularExpression("[\\s\\-\\.\\(\\)]"));
    return clean;
}

bool MainWindow::emailExisteDeja(const QString& email, int idClientExclu)
{
    QSqlQuery query;
    if (idClientExclu > 0) {
        query.prepare("SELECT COUNT(*) FROM CLIENT WHERE EMAIL = :email AND ID_CLIENT != :id");
        query.bindValue(":id", idClientExclu);
    } else {
        query.prepare("SELECT COUNT(*) FROM CLIENT WHERE EMAIL = :email");
    }
    query.bindValue(":email", email);

    if (query.exec() && query.next()) {
        return query.value(0).toInt() > 0;
    }
    return false;
}

bool MainWindow::telephoneExisteDeja(const QString& telephone, int idClientExclu)
{
    QSqlQuery query;
    QString telClean = nettoyerTelephone(telephone);
    if (idClientExclu > 0) {
        query.prepare("SELECT COUNT(*) FROM CLIENT WHERE TELEPHONE = :telephone AND ID_CLIENT != :id");
        query.bindValue(":id", idClientExclu);
    } else {
        query.prepare("SELECT COUNT(*) FROM CLIENT WHERE TELEPHONE = :telephone");
    }
    query.bindValue(":telephone", telClean);

    if (query.exec() && query.next()) {
        return query.value(0).toInt() > 0;
    }
    return false;
}

bool MainWindow::cinExisteDeja(const QString &cin, int idClientExclu)
{
    QSqlQuery query;
    if (idClientExclu > 0) {
        query.prepare("SELECT COUNT(*) FROM CLIENT WHERE CIN = :cin AND ID_CLIENT != :id");
        query.bindValue(":id", idClientExclu);
    } else {
        query.prepare("SELECT COUNT(*) FROM CLIENT WHERE CIN = :cin");
    }
    query.bindValue(":cin", cin);

    if (query.exec() && query.next()) {
        return query.value(0).toInt() > 0;
    }
    return false;
}

bool MainWindow::validerSaisieClient()
{
    // Récupération des données
    QString nom = ui->firstNameEdit_2->text().trimmed();
    QString prenom = ui->lastNameEdit_2->text().trimmed();
    QString email = ui->emailEdit_2->text().trimmed();
    QString telephone = ui->phoneEdit_2->text().trimmed();
    const QString cin = clientCinEdit(this) ? clientCinEdit(this)->text().trimmed() : QString();
    const QString genre = clientGenreCombo(this) ? clientGenreCombo(this)->currentText().trimmed() : QString();
    QDate date = ui->startDateEdit_2->date();
    QString achats = ui->departmentComboBox_8->currentText().trimmed();

    // ==================== 1. CONTROLE DES CHAMPS OBLIGATOIRES ====================
    QStringList champsManquants;
    if (nom.isEmpty()) champsManquants << "Nom";
    if (prenom.isEmpty()) champsManquants << "Prénom";
    if (email.isEmpty()) champsManquants << "Email";
    if (telephone.isEmpty()) champsManquants << "Téléphone";
    if (cin.isEmpty()) champsManquants << "CIN";
    if (genre.isEmpty()) champsManquants << "Genre";

    if (!champsManquants.isEmpty()) {
        QMessageBox::warning(this, "Champs obligatoires",
                             "Veuillez remplir les champs suivants :\n- " + champsManquants.join("\n- "));
        return false;
    }

    // ==================== 2. VALIDATION DU NOM ====================
    if (!validerFormatNomPrenom(nom)) {
        QMessageBox::warning(this, "Nom invalide",
                             "Le nom doit contenir uniquement des lettres (minimum 2 caractères).\n"
                             "Caractères autorisés : lettres, espaces, tirets et apostrophes.");
        ui->firstNameEdit_2->setFocus();
        ui->firstNameEdit_2->selectAll();
        return false;
    }

    // ==================== 3. VALIDATION DU PRENOM ====================
    if (!validerFormatNomPrenom(prenom)) {
        QMessageBox::warning(this, "Prénom invalide",
                             "Le prénom doit contenir uniquement des lettres (minimum 2 caractères).\n"
                             "Caractères autorisés : lettres, espaces, tirets et apostrophes.");
        ui->lastNameEdit_2->setFocus();
        ui->lastNameEdit_2->selectAll();
        return false;
    }

    // ==================== 4. VALIDATION DE L'EMAIL ====================
    if (!validerFormatEmail(email)) {
        QMessageBox::warning(this, "Email invalide",
                             "Veuillez saisir une adresse email valide.\n"
                             "Exemple : utilisateur@domaine.com");
        ui->emailEdit_2->setFocus();
        ui->emailEdit_2->selectAll();
        return false;
    }

    // ==================== 5. VALIDATION DU TELEPHONE ====================
    QString telephoneClean = nettoyerTelephone(telephone);
    if (!validerFormatTelephone(telephoneClean)) {
        QMessageBox::warning(this, "Téléphone invalide",
                             "Veuillez saisir un numéro de téléphone valide.\n\n"
                             "Formats acceptés :\n"
                             "- 12345678 (8 chiffres)\n"
                             "- +21612345678 (indicatif +216)\n"
                             "- 0021612345678 (indicatif 00216)");
        ui->phoneEdit_2->setFocus();
        ui->phoneEdit_2->selectAll();
        return false;
    }

    // ==================== 6. VALIDATION DU CIN ====================
    if (!validerFormatCin(cin)) {
        QMessageBox::warning(this, "CIN invalide",
                             "Le CIN doit contenir exactement 8 chiffres.");
        if (auto *cinEdit = clientCinEdit(this)) {
            cinEdit->setFocus();
            cinEdit->selectAll();
        }
        return false;
    }

    // ==================== 7. VALIDATION DU GENRE ====================
    if (genre != "HOMME" && genre != "FEMME") {
        QMessageBox::warning(this, "Genre invalide",
                             "Veuillez sélectionner un genre valide (HOMME ou FEMME).");
        ui->emailEdit_17->setFocus();
        return false;
    }

    // ==================== 8. VALIDATION DE LA DATE ====================
    QDate currentDate = QDate::currentDate();
    if (date > currentDate) {
        QMessageBox::warning(this, "Date invalide",
                             "La date d'enregistrement ne peut pas être dans le futur.\n"
                             "Veuillez sélectionner une date valide.");
        ui->startDateEdit_2->setFocus();
        return false;
    }

    // ==================== 9. VALIDATION DU TYPE D'ACHATS ====================
    if (achats.isEmpty()) {
        QMessageBox::warning(this, "Type d'achats invalide",
                             "Veuillez sélectionner un type d'achats dans la liste.");
        ui->departmentComboBox_8->setFocus();
        return false;
    }

    // ==================== 10. VERIFICATION DES DOUBLONS ====================
    if (emailExisteDeja(email, idClientSelectionne)) {
        QMessageBox::warning(this, "Email déjà utilisé",
                             "Cette adresse email est déjà associée à un autre client.\n"
                             "Veuillez utiliser une adresse email différente.");
        ui->emailEdit_2->setFocus();
        ui->emailEdit_2->selectAll();
        return false;
    }

    if (telephoneExisteDeja(telephoneClean, idClientSelectionne)) {
        QMessageBox::warning(this, "Téléphone déjà utilisé",
                             "Ce numéro de téléphone est déjà associé à un autre client.\n"
                             "Veuillez utiliser un numéro différent.");
        ui->phoneEdit_2->setFocus();
        ui->phoneEdit_2->selectAll();
        return false;
    }

    if (idClientSelectionne > 0 && cinExisteDeja(cin, idClientSelectionne)) {
        QMessageBox::warning(this, "CIN déjà utilisé",
                             "Ce CIN est déjà associé à un autre client.\n"
                             "Veuillez utiliser un CIN différent.");
        if (auto *cinEdit = clientCinEdit(this)) {
            cinEdit->setFocus();
            cinEdit->selectAll();
        }
        return false;
    }

    // ==================== 11. CONFIRMATION VISUELLE ====================
    QString validStyle = "border: 2px solid #4CAF50; border-radius: 6px; padding: 8px;";
    ui->firstNameEdit_2->setStyleSheet(validStyle);
    ui->lastNameEdit_2->setStyleSheet(validStyle);
    ui->emailEdit_2->setStyleSheet(validStyle);
    ui->phoneEdit_2->setStyleSheet(validStyle);
    if (auto *cinEdit = clientCinEdit(this))
        cinEdit->setStyleSheet(validStyle);
    ui->emailEdit_17->setStyleSheet(validStyle);
    ui->startDateEdit_2->setStyleSheet(validStyle);
    ui->departmentComboBox_8->setStyleSheet(validStyle);

    return true;
}



// ==================== STATISTIQUES PROFESSIONNELLES ====================  ******* client ----------

void MainWindow::initialiserStatistiques()
{
    // Créer un layout propre pour l'onglet statistique
    if (ui->StatistiqueempTab->layout()) {
        delete ui->StatistiqueempTab->layout();
    }

    QVBoxLayout *mainLayout = new QVBoxLayout(ui->StatistiqueempTab);
    mainLayout->setSpacing(25);
    mainLayout->setContentsMargins(30, 30, 30, 30);

    // En-tête avec titre et description
    QWidget *headerWidget = new QWidget();
    QVBoxLayout *headerLayout = new QVBoxLayout(headerWidget);
    headerLayout->setSpacing(8);

    QLabel *titleLabel = new QLabel("ANALYSE DEMOGRAPHIQUE");
    titleLabel->setFont(QFont("Poppins", 22, QFont::Bold));
    titleLabel->setStyleSheet("color: #1e293b; letter-spacing: -0.5px;");
    titleLabel->setAlignment(Qt::AlignLeft);

    QLabel *subtitleLabel = new QLabel("Répartition des clients par genre");
    subtitleLabel->setFont(QFont("Poppins", 13));
    subtitleLabel->setStyleSheet("color: #64748b; margin-top: -5px;");
    subtitleLabel->setAlignment(Qt::AlignLeft);

    headerLayout->addWidget(titleLabel);
    headerLayout->addWidget(subtitleLabel);
    mainLayout->addWidget(headerWidget);

    // Conteneur principal avec ombre
    QFrame *mainCard = new QFrame();
    mainCard->setStyleSheet("QFrame { background-color: white; border-radius: 24px; }");

    // Ajouter une ombre portée
    QGraphicsDropShadowEffect *shadowEffect = new QGraphicsDropShadowEffect();
    shadowEffect->setBlurRadius(40);
    shadowEffect->setColor(QColor(0, 0, 0, 30));
    shadowEffect->setOffset(0, 10);
    mainCard->setGraphicsEffect(shadowEffect);

    QVBoxLayout *cardLayout = new QVBoxLayout(mainCard);
    cardLayout->setSpacing(25);
    cardLayout->setContentsMargins(30, 30, 30, 30);

    // Layout pour les cartes KPI
    QHBoxLayout *kpiLayout = new QHBoxLayout();
    kpiLayout->setSpacing(20);

    // Ces cartes seront remplies après le calcul
    mainLayout->addWidget(mainCard);

    // Bouton de rafraîchissement stylisé
    QPushButton *refreshBtn = new QPushButton("Actualiser les donnees");
    refreshBtn->setFont(QFont("Poppins", 11));
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #f8fafc;
            color: #334155;
            border: 1.5px solid #e2e8f0;
            border-radius: 14px;
            padding: 12px 25px;
            font-weight: 500;
        }
        QPushButton:hover {
            background-color: #f1f5f9;
            border-color: #94a3b8;
        }
        QPushButton:pressed {
            background-color: #e2e8f0;
        }
    )");
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::mettreAJourStatistiques);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(refreshBtn);
    btnLayout->addStretch();

    mainLayout->addLayout(btnLayout);
    mainLayout->addStretch();

    // Charger les données
    calculerStatistiquesGenre();
}

void MainWindow::calculerStatistiquesGenre()
{
    QSqlQuery query;
    int hommes = 0, femmes = 0, total = 0;

    // Requête optimisée pour compter les hommes (insensible à la casse)
    if (query.exec("SELECT COUNT(*) FROM CLIENT WHERE UPPER(GENRE) IN ('HOMME', 'H', 'MASCULIN', 'M')")) {
        if (query.next()) hommes = query.value(0).toInt();
    }

    // Requête optimisée pour compter les femmes (insensible à la casse)
    if (query.exec("SELECT COUNT(*) FROM CLIENT WHERE UPPER(GENRE) IN ('FEMME', 'F', 'FEMININ')")) {
        if (query.next()) femmes = query.value(0).toInt();
    }

    // Total des clients
    if (query.exec("SELECT COUNT(*) FROM CLIENT")) {
        if (query.next()) total = query.value(0).toInt();
    }

    qDebug() << "Statistiques - H:" << hommes << "F:" << femmes << "T:" << total;

    // Mettre à jour l'interface
    creerDiagrammeCirculaire(hommes, femmes, total);
}

QWidget* MainWindow::creerCarteKPI(const QString& titre, int valeur, double pourcentage,
                                   const QString& couleur, const QString& icone)
{
    QFrame *card = new QFrame();
    card->setFixedHeight(160);
    card->setStyleSheet(QString(R"(
        QFrame {
            background-color: white;
            border: 1px solid #eef2f6;
            border-radius: 20px;
            border-left: 6px solid %1;
        }
        QFrame:hover {
            border-color: %1;
            background-color: #fcfdff;
        }
    )").arg(couleur));

    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setSpacing(12);
    layout->setContentsMargins(20, 20, 20, 20);

    // Titre avec icône
    QHBoxLayout *titleLayout = new QHBoxLayout();
    QLabel *iconLabel = new QLabel(icone);
    iconLabel->setFont(QFont("Segoe UI Emoji", 20));

    QLabel *titleLabel = new QLabel(titre);
    titleLabel->setFont(QFont("Poppins", 14, QFont::Medium));
    titleLabel->setStyleSheet("color: #475569;");

    titleLayout->addWidget(iconLabel);
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();

    // Valeur principale
    QLabel *valueLabel = new QLabel(formaterNombre(valeur));
    valueLabel->setFont(QFont("Poppins", 32, QFont::Bold));
    valueLabel->setStyleSheet(QString("color: %1;").arg(couleur));
    valueLabel->setAlignment(Qt::AlignLeft);

    // Pourcentage
    QLabel *percentLabel = new QLabel(QString("%1% du total").arg(QString::number(pourcentage, 'f', 1)));
    percentLabel->setFont(QFont("Poppins", 12));
    percentLabel->setStyleSheet("color: #64748b;");

    layout->addLayout(titleLayout);
    layout->addWidget(valueLabel);
    layout->addWidget(percentLabel);

    return card;
}

void MainWindow::creerDiagrammeCirculaire(int hommes, int femmes, int total)
{
    if (total == 0) {
        QWidget *noDataWidget = new QWidget();
        QVBoxLayout *noDataLayout = new QVBoxLayout(noDataWidget);
        noDataLayout->setAlignment(Qt::AlignCenter);

        QLabel *iconLabel = new QLabel("STAT");
        iconLabel->setFont(QFont("Segoe UI Emoji", 48));
        iconLabel->setAlignment(Qt::AlignCenter);

        QLabel *noDataLabel = new QLabel("Aucune donnée disponible");
        noDataLabel->setFont(QFont("Poppins", 16));
        noDataLabel->setStyleSheet("color: #94a3b8;");
        noDataLabel->setAlignment(Qt::AlignCenter);

        QLabel *subLabel = new QLabel("Ajoutez des clients pour voir les statistiques");
        subLabel->setFont(QFont("Poppins", 12));
        subLabel->setStyleSheet("color: #cbd5e1;");
        subLabel->setAlignment(Qt::AlignCenter);

        noDataLayout->addWidget(iconLabel);
        noDataLayout->addWidget(noDataLabel);
        noDataLayout->addWidget(subLabel);

        QLayout *oldLayout = ui->StatistiqueempTab->layout();
        if (oldLayout) delete oldLayout;

        QVBoxLayout *newLayout = new QVBoxLayout(ui->StatistiqueempTab);
        newLayout->addWidget(noDataWidget);
        return;
    }

    double pourcentageHommes = (hommes * 100.0) / total;
    double pourcentageFemmes = (femmes * 100.0) / total;

    // Widget principal
    QWidget *mainWidget = new QWidget();
    mainWidget->setStyleSheet("background: transparent;");

    QVBoxLayout *mainLayout = new QVBoxLayout(mainWidget);
    mainLayout->setSpacing(20);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // ==================== EN-TETE ====================
    QFrame *headerFrame = new QFrame();
    headerFrame->setStyleSheet(R"(
        QFrame {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                       stop:0 #1e293b, stop:1 #0f172a);
            border-radius: 16px;
            border: none;
        }
    )");
    headerFrame->setFixedHeight(100);

    QHBoxLayout *headerLayout = new QHBoxLayout(headerFrame);
    headerLayout->setContentsMargins(30, 0, 30, 0);

    // Titre à gauche
    QVBoxLayout *titleLayout = new QVBoxLayout();
    QLabel *mainTitle = new QLabel("ANALYSE CLIENTELE");
    mainTitle->setFont(QFont("Poppins", 18, QFont::Bold));
    mainTitle->setStyleSheet("color: white; letter-spacing: 2px;");

    QLabel *subTitle = new QLabel("Tableau de bord démographique");
    subTitle->setFont(QFont("Poppins", 12));
    subTitle->setStyleSheet("color: #94a3b8;");

    titleLayout->addWidget(mainTitle);
    titleLayout->addWidget(subTitle);

    // Date à droite
    QLabel *dateLabel = new QLabel(QDate::currentDate().toString("dd/MM/yyyy"));
    dateLabel->setFont(QFont("Poppins", 12));
    dateLabel->setStyleSheet("color: #94a3b8; background-color: rgba(255,255,255,0.1); padding: 8px 16px; border-radius: 20px;");

    headerLayout->addLayout(titleLayout);
    headerLayout->addStretch();
    headerLayout->addWidget(dateLabel);

    mainLayout->addWidget(headerFrame);

    // ==================== 3 CARTES KPI ====================
    QHBoxLayout *kpiLayout = new QHBoxLayout();
    kpiLayout->setSpacing(20);

    // Carte 1: Total Clients
    QFrame *totalCard = new QFrame();
    totalCard->setFixedHeight(130);
    totalCard->setStyleSheet(R"(
        QFrame {
            background-color: white;
            border-radius: 20px;
            border: 1px solid #e2e8f0;
        }
        QFrame:hover {
            border-color: #3b82f6;
            background-color: #f8fafc;
        }
    )");

    QHBoxLayout *totalCardLayout = new QHBoxLayout(totalCard);
    totalCardLayout->setContentsMargins(20, 15, 20, 15);

    QLabel *totalIcon = new QLabel("C");
    totalIcon->setFont(QFont("Segoe UI Emoji", 32));

    QVBoxLayout *totalTextLayout = new QVBoxLayout();
    QLabel *totalLabel = new QLabel("TOTAL CLIENTS");
    totalLabel->setFont(QFont("Poppins", 11));
    totalLabel->setStyleSheet("color: #64748b; letter-spacing: 1px;");

    QLabel *totalValue = new QLabel(QString::number(total));
    totalValue->setFont(QFont("Poppins", 28, QFont::Bold));
    totalValue->setStyleSheet("color: #1e293b;");

    totalTextLayout->addWidget(totalLabel);
    totalTextLayout->addWidget(totalValue);

    totalCardLayout->addWidget(totalIcon);
    totalCardLayout->addSpacing(15);
    totalCardLayout->addLayout(totalTextLayout);
    totalCardLayout->addStretch();

    // Carte 2: Hommes
    QFrame *hommesCard = new QFrame();
    hommesCard->setFixedHeight(130);
    hommesCard->setStyleSheet(R"(
        QFrame {
            background-color: white;
            border-radius: 20px;
            border: 1px solid #e2e8f0;
        }
        QFrame:hover {
            border-color: #3b82f6;
            background-color: #eff6ff;
        }
    )");

    QHBoxLayout *hommesCardLayout = new QHBoxLayout(hommesCard);
    hommesCardLayout->setContentsMargins(20, 15, 20, 15);

    QLabel *hommesIcon = new QLabel("H");
    hommesIcon->setFont(QFont("Segoe UI Emoji", 32));

    QVBoxLayout *hommesTextLayout = new QVBoxLayout();
    QLabel *hommesLabel = new QLabel("HOMMES");
    hommesLabel->setFont(QFont("Poppins", 11));
    hommesLabel->setStyleSheet("color: #64748b; letter-spacing: 1px;");

    QLabel *hommesValue = new QLabel(QString::number(hommes));
    hommesValue->setFont(QFont("Poppins", 28, QFont::Bold));
    hommesValue->setStyleSheet("color: #3b82f6;");

    QLabel *hommesPercent = new QLabel(QString("%1%").arg(pourcentageHommes, 0, 'f', 1));
    hommesPercent->setFont(QFont("Poppins", 11));
    hommesPercent->setStyleSheet("color: #22c55e;");

    hommesTextLayout->addWidget(hommesLabel);
    hommesTextLayout->addWidget(hommesValue);
    hommesTextLayout->addWidget(hommesPercent);

    hommesCardLayout->addWidget(hommesIcon);
    hommesCardLayout->addSpacing(15);
    hommesCardLayout->addLayout(hommesTextLayout);
    hommesCardLayout->addStretch();

    // Carte 3: Femmes
    QFrame *femmesCard = new QFrame();
    femmesCard->setFixedHeight(130);
    femmesCard->setStyleSheet(R"(
        QFrame {
            background-color: white;
            border-radius: 20px;
            border: 1px solid #e2e8f0;
        }
        QFrame:hover {
            border-color: #ec4899;
            background-color: #fdf2f8;
        }
    )");

    QHBoxLayout *femmesCardLayout = new QHBoxLayout(femmesCard);
    femmesCardLayout->setContentsMargins(20, 15, 20, 15);

    QLabel *femmesIcon = new QLabel("F");
    femmesIcon->setFont(QFont("Segoe UI Emoji", 32));

    QVBoxLayout *femmesTextLayout = new QVBoxLayout();
    QLabel *femmesLabel = new QLabel("FEMMES");
    femmesLabel->setFont(QFont("Poppins", 11));
    femmesLabel->setStyleSheet("color: #64748b; letter-spacing: 1px;");

    QLabel *femmesValue = new QLabel(QString::number(femmes));
    femmesValue->setFont(QFont("Poppins", 28, QFont::Bold));
    femmesValue->setStyleSheet("color: #ec4899;");

    QLabel *femmesPercent = new QLabel(QString("%1%").arg(pourcentageFemmes, 0, 'f', 1));
    femmesPercent->setFont(QFont("Poppins", 11));
    femmesPercent->setStyleSheet("color: #22c55e;");

    femmesTextLayout->addWidget(femmesLabel);
    femmesTextLayout->addWidget(femmesValue);
    femmesTextLayout->addWidget(femmesPercent);

    femmesCardLayout->addWidget(femmesIcon);
    femmesCardLayout->addSpacing(15);
    femmesCardLayout->addLayout(femmesTextLayout);
    femmesCardLayout->addStretch();

    kpiLayout->addWidget(totalCard);
    kpiLayout->addWidget(hommesCard);
    kpiLayout->addWidget(femmesCard);

    mainLayout->addLayout(kpiLayout);

    // ==================== SECTION GRAPHIQUE ====================
    QFrame *chartContainer = new QFrame();
    chartContainer->setStyleSheet(R"(
        QFrame {
            background-color: white;
            border-radius: 20px;
            border: 1px solid #e2e8f0;
        }
    )");

    QHBoxLayout *chartContainerLayout = new QHBoxLayout(chartContainer);
    chartContainerLayout->setContentsMargins(30, 25, 30, 25);
    chartContainerLayout->setSpacing(40);

    // Diagramme circulaire
    QPieSeries *series = new QPieSeries();

    QPieSlice *sliceHommes = series->append("Hommes", hommes);
    QPieSlice *sliceFemmes = series->append("Femmes", femmes);

    sliceHommes->setColor(QColor(59, 130, 246));
    sliceHommes->setLabelVisible(true);
    sliceHommes->setLabelColor(QColor(30, 41, 59));
    sliceHommes->setLabelPosition(QPieSlice::LabelOutside);
    sliceHommes->setLabelArmLengthFactor(0.15);
    sliceHommes->setBorderColor(Qt::white);
    sliceHommes->setBorderWidth(3);

    sliceFemmes->setColor(QColor(236, 72, 153));
    sliceFemmes->setLabelVisible(true);
    sliceFemmes->setLabelColor(QColor(30, 41, 59));
    sliceFemmes->setLabelPosition(QPieSlice::LabelOutside);
    sliceFemmes->setLabelArmLengthFactor(0.15);
    sliceFemmes->setBorderColor(Qt::white);
    sliceFemmes->setBorderWidth(3);

    // Labels personnalisés
    sliceHommes->setLabel(QString("Hommes\n%1\n%2%").arg(hommes).arg(pourcentageHommes, 0, 'f', 1));
    sliceFemmes->setLabel(QString("Femmes\n%1\n%2%").arg(femmes).arg(pourcentageFemmes, 0, 'f', 1));

    QChart *chart = new QChart();
    chart->addSeries(series);
    chart->setTitle("");
    chart->setAnimationOptions(QChart::SeriesAnimations);
    chart->setBackgroundVisible(false);
    chart->setPlotAreaBackgroundVisible(false);
    chart->legend()->setVisible(false);

    QChartView *chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setMinimumSize(400, 320);
    chartView->setMaximumSize(500, 350);
    chartView->setStyleSheet("background: transparent; border: none;");

    // Légende détaillée à droite
    QWidget *legendPanel = new QWidget();
    QVBoxLayout *legendPanelLayout = new QVBoxLayout(legendPanel);
    legendPanelLayout->setSpacing(15);

    QLabel *legendTitle = new QLabel("DETAILS");
    legendTitle->setFont(QFont("Poppins", 13, QFont::Bold));
    legendTitle->setStyleSheet("color: #1e293b;");

    legendPanelLayout->addWidget(legendTitle);
    legendPanelLayout->addSpacing(5);

    // Hommes détail
    QFrame *hommeDetail = new QFrame();
    hommeDetail->setStyleSheet("background-color: #eff6ff; border-radius: 12px; border: none;");
    QHBoxLayout *hommeDetailLayout = new QHBoxLayout(hommeDetail);
    hommeDetailLayout->setContentsMargins(15, 12, 15, 12);

    QFrame *hommeColor = new QFrame();
    hommeColor->setFixedSize(12, 12);
    hommeColor->setStyleSheet("background-color: #3b82f6; border-radius: 6px;");

    QLabel *hommeDetailLabel = new QLabel("Hommes");
    hommeDetailLabel->setFont(QFont("Poppins", 12));
    hommeDetailLabel->setStyleSheet("color: #1e293b;");

    hommeDetailLayout->addWidget(hommeColor);
    hommeDetailLayout->addSpacing(10);
    hommeDetailLayout->addWidget(hommeDetailLabel);
    hommeDetailLayout->addStretch();
    hommeDetailLayout->addWidget(new QLabel(QString("%1 clients").arg(hommes)));

    // Femmes détail
    QFrame *femmeDetail = new QFrame();
    femmeDetail->setStyleSheet("background-color: #fdf2f8; border-radius: 12px; border: none;");
    QHBoxLayout *femmeDetailLayout = new QHBoxLayout(femmeDetail);
    femmeDetailLayout->setContentsMargins(15, 12, 15, 12);

    QFrame *femmeColor = new QFrame();
    femmeColor->setFixedSize(12, 12);
    femmeColor->setStyleSheet("background-color: #ec4899; border-radius: 6px;");

    QLabel *femmeDetailLabel = new QLabel("Femmes");
    femmeDetailLabel->setFont(QFont("Poppins", 12));
    femmeDetailLabel->setStyleSheet("color: #1e293b;");

    femmeDetailLayout->addWidget(femmeColor);
    femmeDetailLayout->addSpacing(10);
    femmeDetailLayout->addWidget(femmeDetailLabel);
    femmeDetailLayout->addStretch();
    femmeDetailLayout->addWidget(new QLabel(QString("%1 clients").arg(femmes)));

    legendPanelLayout->addWidget(hommeDetail);
    legendPanelLayout->addWidget(femmeDetail);
    legendPanelLayout->addStretch();

    chartContainerLayout->addWidget(chartView, 2);
    chartContainerLayout->addWidget(legendPanel, 1);

    mainLayout->addWidget(chartContainer);

    // ==================== BARRE D'ACTIONS ====================
    QFrame *actionBar = new QFrame();
    actionBar->setStyleSheet("background: transparent;");

    QHBoxLayout *actionLayout = new QHBoxLayout(actionBar);
    actionLayout->setContentsMargins(0, 10, 0, 0);

    // Bouton actualiser
    QPushButton *refreshBtn = new QPushButton("Actualiser");
    refreshBtn->setFont(QFont("Poppins", 11));
    refreshBtn->setCursor(Qt::PointingHandCursor);
    refreshBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #f1f5f9;
            color: #475569;
            border: 1px solid #e2e8f0;
            border-radius: 12px;
            padding: 10px 20px;
            font-weight: 500;
        }
        QPushButton:hover {
            background-color: #e2e8f0;
            border-color: #cbd5e1;
        }
    )");
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::mettreAJourStatistiques);

    // Bouton exporter
    QPushButton *exportBtn = new QPushButton("Exporter rapport");
    exportBtn->setFont(QFont("Poppins", 11));
    exportBtn->setCursor(Qt::PointingHandCursor);
    exportBtn->setStyleSheet(R"(
    QPushButton {
        background-color: #2E7D32;
        color: white;
        border: none;
        border-radius: 12px;
        padding: 10px 24px;
        font-weight: 600;
    }
    QPushButton:hover {
        background-color: #1B5E20;
    }
)");

    connect(exportBtn, &QPushButton::clicked, this,
            [this, hommes, femmes, total, pourcentageHommes, pourcentageFemmes]() {
                exporterRapportPDF(hommes, femmes, total, pourcentageHommes, pourcentageFemmes);
            });

    actionLayout->addStretch();
    actionLayout->addWidget(refreshBtn);
    actionLayout->addSpacing(15);
    actionLayout->addWidget(exportBtn);
    actionLayout->addStretch();

    mainLayout->addWidget(actionBar);

    // Appliquer le layout
    QLayout *oldLayout = ui->StatistiqueempTab->layout();
    if (oldLayout) delete oldLayout;

    ui->StatistiqueempTab->setLayout(mainLayout);
}

void MainWindow::calculerStatistiquesJournalieres()
{
    QDate startDate = QDate::currentDate().addMonths(-6).addDays(1);
    QDate endDate = QDate::currentDate();

    QMap<QDate, int> dailyCounts;
    QSqlQuery query;
    query.prepare(
        "SELECT TRUNC(DATE_ENREGISTREMENT) AS d, COUNT(*) AS c "
        "FROM CLIENT "
        "WHERE DATE_ENREGISTREMENT >= :startDate "
        "GROUP BY TRUNC(DATE_ENREGISTREMENT) "
        "ORDER BY d ASC");
    query.bindValue(":startDate", startDate);

    if (query.exec()) {
        while (query.next()) {
            const QDate d = query.value(0).toDate();
            const int c = query.value(1).toInt();
            if (d.isValid()) {
                dailyCounts[d] = c;
            }
        }
    }

    m_datesHistorique.clear();
    m_nombresHistorique.clear();
    QVector<double> smoothed;
    for (QDate d = startDate; d <= endDate; d = d.addDays(1)) {
        m_datesHistorique.push_back(d);
        m_nombresHistorique.push_back(dailyCounts.value(d, 0));
    }

    const int n = m_nombresHistorique.size();
    smoothed.resize(n);
    for (int i = 0; i < n; ++i) {
        const int left = qMax(0, i - 3);
        const int right = qMin(n - 1, i + 3);
        double sum = 0.0;
        int cnt = 0;
        for (int j = left; j <= right; ++j) {
            sum += m_nombresHistorique[j];
            ++cnt;
        }
        smoothed[i] = cnt > 0 ? (sum / cnt) : 0.0;
    }

    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = static_cast<double>(i);
        const double y = smoothed[i];
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    const double den = (n * sxx) - (sx * sx);
    const double pente = (qFuzzyIsNull(den) ? 0.0 : ((n * sxy - sx * sy) / den));
    const double intercept = n > 0 ? ((sy - pente * sx) / n) : 0.0;

    QVector<double> prevision;
    prevision.reserve(90);
    for (int i = 0; i < 90; ++i) {
        const double x = static_cast<double>(n + i);
        prevision.push_back(qMax(0.0, intercept + pente * x));
    }

    afficherCourbeAcquisition(m_datesHistorique, m_nombresHistorique, prevision);

    int totalPrevus = 0;
    for (double v : prevision) {
        totalPrevus += qRound(v);
    }
    afficherCarteTendance(totalPrevus, pente);
}

void MainWindow::afficherCourbeAcquisition(const QVector<QDate>& dates,
                                           const QVector<int>& nombres,
                                           const QVector<double>& prevision)
{
    if (!ui->StatistiqueempTab || !ui->StatistiqueempTab->layout() || dates.isEmpty()) {
        return;
    }

    auto *container = new QFrame();
    container->setObjectName("clientAcquisitionContainer");
    container->setStyleSheet("QFrame#clientAcquisitionContainer { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px; }");
    auto *vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(16, 16, 16, 16);
    vbox->setSpacing(10);

    auto *title = new QLabel("Courbe d'acquisition clients (6 mois + prevision 3 mois)");
    title->setStyleSheet("QLabel { color: #1f2937; font-weight: 600; }");
    vbox->addWidget(title);

    auto *scatter = new QScatterSeries();
    scatter->setName("Historique");
    scatter->setMarkerSize(5.0);
    scatter->setColor(QColor("#2563eb"));

    auto *smooth = new QLineSeries();
    smooth->setName("Historique lisse");
    smooth->setColor(QColor("#1d4ed8"));
    QPen smoothPen(QColor("#1d4ed8"));
    smoothPen.setWidth(2);
    smooth->setPen(smoothPen);

    const int n = nombres.size();
    QVector<double> smoothed(n, 0.0);
    for (int i = 0; i < n; ++i) {
        const int left = qMax(0, i - 3);
        const int right = qMin(n - 1, i + 3);
        double sum = 0.0;
        int cnt = 0;
        for (int j = left; j <= right; ++j) {
            sum += nombres[j];
            ++cnt;
        }
        smoothed[i] = cnt > 0 ? sum / cnt : 0.0;
    }

    qint64 minX = QDateTime(dates.first(), QTime(0, 0)).toMSecsSinceEpoch();
    qint64 maxX = QDateTime(dates.last().addDays(prevision.size()), QTime(0, 0)).toMSecsSinceEpoch();
    int yMax = 1;

    for (int i = 0; i < dates.size(); ++i) {
        const qint64 x = QDateTime(dates[i], QTime(0, 0)).toMSecsSinceEpoch();
        scatter->append(x, nombres[i]);
        smooth->append(x, smoothed[i]);
        yMax = qMax(yMax, nombres[i]);
    }

    auto *forecast = new QLineSeries();
    forecast->setName("Prevision");
    QPen predPen(QColor("#dc2626"));
    predPen.setWidth(2);
    predPen.setStyle(Qt::DashLine);
    forecast->setPen(predPen);

    for (int i = 0; i < prevision.size(); ++i) {
        const QDate d = dates.last().addDays(i + 1);
        const qint64 x = QDateTime(d, QTime(0, 0)).toMSecsSinceEpoch();
        forecast->append(x, prevision[i]);
        yMax = qMax(yMax, qRound(prevision[i]));
    }

    auto *chart = new QChart();
    chart->addSeries(scatter);
    chart->addSeries(smooth);
    chart->addSeries(forecast);
    chart->legend()->setVisible(true);
    chart->legend()->setAlignment(Qt::AlignRight);
    chart->setBackgroundVisible(false);
    chart->setAnimationOptions(QChart::SeriesAnimations);

    auto *axisX = new QDateTimeAxis();
    axisX->setFormat("dd/MM");
    axisX->setTitleText("Dates");
    axisX->setRange(QDateTime::fromMSecsSinceEpoch(minX), QDateTime::fromMSecsSinceEpoch(maxX));
    axisX->setTickCount(10);

    auto *axisY = new QValueAxis();
    axisY->setTitleText("Nb clients / jour");
    axisY->setRange(0, qMax(10, static_cast<int>(yMax * 1.2)));
    axisY->setLabelFormat("%d");

    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    scatter->attachAxis(axisX);
    scatter->attachAxis(axisY);
    smooth->attachAxis(axisX);
    smooth->attachAxis(axisY);
    forecast->attachAxis(axisX);
    forecast->attachAxis(axisY);

    auto *chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setMinimumHeight(320);
    vbox->addWidget(chartView);

    ui->StatistiqueempTab->layout()->addWidget(container);
}

void MainWindow::afficherCarteTendance(int totalPrevus, double pente)
{
    if (!ui->StatistiqueempTab || !ui->StatistiqueempTab->layout()) {
        return;
    }

    QString tendanceTexte = "Stagnante";
    QString tendanceCouleur = "#ca8a04";
    if (pente > 0.03) {
        tendanceTexte = "Positive";
        tendanceCouleur = "#16a34a";
    } else if (pente < -0.03) {
        tendanceTexte = "Negative";
        tendanceCouleur = "#dc2626";
    }

    auto *card = new QFrame();
    card->setObjectName("clientTrendCard");
    card->setStyleSheet("QFrame#clientTrendCard { background: #ffffff; border: 1px solid #e5e7eb; border-radius: 12px; }");
    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(20);

    auto *lblTrend = new QLabel("Tendance: " + tendanceTexte);
    lblTrend->setStyleSheet(QString("QLabel { font-weight: 600; color: %1; }").arg(tendanceCouleur));
    auto *lblForecast = new QLabel("Prevision 3 mois: " + QString::number(totalPrevus) + " clients");
    lblForecast->setStyleSheet("QLabel { color: #1f2937; font-weight: 600; }");
    auto *lblSlope = new QLabel("Pente: " + QString::number(pente, 'f', 4));
    lblSlope->setStyleSheet("QLabel { color: #6b7280; }");

    layout->addWidget(lblTrend);
    layout->addWidget(lblForecast);
    layout->addWidget(lblSlope);
    layout->addStretch();

    ui->StatistiqueempTab->layout()->addWidget(card);
}

void MainWindow::mettreAJourStatistiques()
{
    calculerStatistiquesGenre();
    calculerStatistiquesJournalieres();
}
// ==================== FONCTION FORMATER NOMBRE ====================
QString MainWindow::formaterNombre(int nombre)
{
    if (nombre < 1000) {
        return QString::number(nombre);
    }
    else if (nombre < 1000000) {
        double enMilliers = nombre / 1000.0;
        return QString::number(enMilliers, 'f', 1) + "k";
    }
    else {
        double enMillions = nombre / 1000000.0;
        return QString::number(enMillions, 'f', 1) + "M";
    }
}

void MainWindow::exporterRapportPDF(int hommes, int femmes, int total,
                                    double pourcentageHommes, double pourcentageFemmes)
{
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    "Exporter le rapport",
                                                    QDir::homePath() + "/rapport_clients_" + QDate::currentDate().toString("yyyy-MM-dd") + ".pdf",
                                                    "PDF Files (*.pdf)");

    if (fileName.isEmpty()) return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(fileName);
    printer.setPageSize(QPageSize::A4);
    printer.setPageMargins(QMarginsF(20, 20, 20, 20), QPageLayout::Millimeter);

    QTextDocument doc;
    doc.setHtml(genererRapportHTML(hommes, femmes, total, pourcentageHommes, pourcentageFemmes));
    doc.print(&printer);
    QMessageBox::information(this, "Export PDF", "Rapport exporte avec succes:\n" + fileName);
}

QString MainWindow::genererRapportHTML(int hommes, int femmes, int total,
                                       double pourcentageHommes, double pourcentageFemmes)
{
    return QString(R"(
        <html>
          <head>
            <meta charset="utf-8"/>
            <style>
              body { font-family: Arial, sans-serif; color: #1f2937; }
              h1 { color: #2E7D32; margin-bottom: 4px; }
              .muted { color: #6b7280; margin-bottom: 16px; }
              table { width: 100%%; border-collapse: collapse; margin-top: 12px; }
              th, td { border: 1px solid #d1d5db; padding: 10px; text-align: left; }
              th { background: #f3f4f6; }
            </style>
          </head>
          <body>
            <h1>Rapport Statistiques Clients</h1>
            <div class="muted">Date: %1</div>
            <table>
              <tr><th>Indicateur</th><th>Valeur</th></tr>
              <tr><td>Total clients</td><td>%2</td></tr>
              <tr><td>Hommes</td><td>%3 (%4%%)</td></tr>
              <tr><td>Femmes</td><td>%5 (%6%%)</td></tr>
            </table>
          </body>
        </html>
    )")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"))
        .arg(total)
        .arg(hommes)
        .arg(QString::number(pourcentageHommes, 'f', 1))
        .arg(femmes)
        .arg(QString::number(pourcentageFemmes, 'f', 1));
}


