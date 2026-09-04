// The spawn-the-real-binary-over-MCP harness the shutdown suites share
// (the import.shutdown / open.responsive pattern, factored once because two
// suites in this directory need it verbatim).
#ifndef SHUTDOWN_MCPHARNESS_H
#define SHUTDOWN_MCPHARNESS_H

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <cstdio>

namespace shutdownharness {

struct McpClient
{
    QNetworkAccessManager net;
    QUrl url;
    QString token;
    int id = 0;

    QJsonObject post(const QJsonObject &body)
    {
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
        QNetworkReply *reply = net.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        return QJsonDocument::fromJson(data).object();
    }

    void initialize()
    {
        post(QJsonObject{ { "jsonrpc", "2.0" }, { "id", ++id }, { "method", "initialize" },
                          { "params", QJsonObject{
                                { "protocolVersion", "2025-06-18" },
                                { "capabilities", QJsonObject{} },
                                { "clientInfo", QJsonObject{ { "name", "shutdown-test" }, { "version", "0" } } } } } });
        post(QJsonObject{ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } });
    }

    QJsonObject runScript(const QString &script)
    {
        const QJsonObject reply = post(QJsonObject{
            { "jsonrpc", "2.0" }, { "id", ++id }, { "method", "tools/call" },
            { "params", QJsonObject{ { "name", "run_script" },
                                     { "arguments", QJsonObject{ { "script", script } } } } } });
        const QJsonArray content = reply.value("result").toObject().value("content").toArray();
        if (content.isEmpty()) return {};
        return QJsonDocument::fromJson(
            content.first().toObject().value("text").toString().toUtf8()).object();
    }
};

/// Same reason as import.shutdown: keep the close path prompt-free. In a
/// QT_DEBUG build jahsettings.ini lives beside the BINARY (applicationDirPath),
/// which a scratch HOME does not isolate — this is the shared file every e2e
/// suite in this tree has to seed.
inline void seedSettings(const QString &binary)
{
    QStringList inis;
    inis << QFileInfo(binary).dir().filePath("jahsettings.ini");
#ifndef QT_DEBUG
    const QString testName = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(QStringLiteral("Jahshaka"));
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QCoreApplication::setApplicationName(testName);
    if (!appData.isEmpty() && QDir().mkpath(appData))
        inis << QDir(appData).filePath("jahsettings.ini");
#endif
    for (const QString &ini : inis) {
        QSettings settings(ini, QSettings::IniFormat);
        settings.setValue("ddialog_seen", true);
        settings.setValue("auto_save", true);
        settings.sync();
    }
}

inline quint16 freePort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

/// Spawns the app on `port` and returns once it printed its session token.
/// Everything read on the way is appended to `log` — these suites assert on
/// the process's own output, so nothing may be thrown away.
inline bool spawn(QProcess &jahshaka, quint16 port, QString *tokenOut, QByteArray *log,
                  const QStringList &extraArgs = QStringList())
{
    jahshaka.setProcessChannelMode(QProcess::MergedChannels);
    jahshaka.start(QStringLiteral(JAHSHAKA_BINARY),
                   QStringList{ QStringLiteral("--mcp-port=%1").arg(port) } + extraArgs);
    if (!jahshaka.waitForStarted(15000)) return false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 120000 && jahshaka.state() == QProcess::Running) {
        jahshaka.waitForReadyRead(500);
        *log += jahshaka.readAll();
        const int at = log->indexOf("MCP: token ");
        if (at >= 0) {
            const int end = log->indexOf('\n', at);
            if (end > at) {
                *tokenOut = QString::fromUtf8(log->mid(at + 11, end - at - 11)).trimmed();
                return true;
            }
        }
    }
    std::printf("---- boot log ----\n%s\n", log->constData());
    return false;
}

}   // namespace shutdownharness

#endif   // SHUTDOWN_MCPHARNESS_H
