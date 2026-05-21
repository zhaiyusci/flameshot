// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "markerocrservice.h"

#include "utils/abstractlogger.h"
#include "utils/confighandler.h"

#include <algorithm>
#include <utility>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>

namespace {
int markerOcrIdleTimeoutMs()
{
    bool ok = false;
    const int timeout =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_IDLE_TIMEOUT_MS"))
        .toInt(&ok);
    return ok && timeout > 0 ? timeout : 30 * 60 * 1000;
}

int markerOcrThreads()
{
    bool ok = false;
    const int configured =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_THREADS"))
        .toInt(&ok);
    int threads = ok ? configured : ConfigHandler().markerOcrThreads();
    return std::max(1, std::min(128, threads));
}

int markerOcrParallelThreads()
{
    bool ok = false;
    const int configured =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_PARALLEL_THREADS"))
        .toInt(&ok);
    int threads = ok ? configured : ConfigHandler().markerOcrParallelThreads();
    return std::max(1, std::min(128, threads));
}

bool markerOcrParallelSmallImages()
{
    const QString configured =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_PARALLEL_SMALL_IMAGES"))
        .trimmed()
        .toLower();
    if (!configured.isEmpty()) {
        return configured != QStringLiteral("0") &&
               configured != QStringLiteral("false") &&
               configured != QStringLiteral("no") &&
               configured != QStringLiteral("off");
    }
    return ConfigHandler().markerOcrParallelSmallImages();
}

QString resolvedExecutable(const QString& executable)
{
    const QString resolved = QStandardPaths::findExecutable(executable);
    return resolved.isEmpty() ? executable : resolved;
}

QString pythonFromScriptShebang(const QString& scriptPath)
{
    if (scriptPath.isEmpty()) {
        return {};
    }

    QFile file(scriptPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QByteArray firstLine = file.readLine(512).trimmed();
    if (!firstLine.startsWith("#!")) {
        return {};
    }

    const QStringList parts =
      QProcess::splitCommand(QString::fromUtf8(firstLine.mid(2)));
    if (parts.isEmpty()) {
        return {};
    }

    if (QFileInfo(parts.first()).fileName() == QStringLiteral("env") &&
        parts.size() > 1) {
        return QStandardPaths::findExecutable(parts.at(1));
    }

    return resolvedExecutable(parts.first());
}

QString executablePathIfUsable(const QString& path)
{
    QFileInfo info(path);
    if (info.exists() && info.isExecutable()) {
        return info.absoluteFilePath();
    }
    return {};
}

QString markerExecutable()
{
    return QStandardPaths::findExecutable(QStringLiteral("marker_single"));
}

QString markerOcrPython()
{
    const QString configured =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_PYTHON"))
        .trimmed();
    if (!configured.isEmpty()) {
        const QString executable = executablePathIfUsable(configured);
        if (!executable.isEmpty()) {
            return executable;
        }
        return configured;
    }

    const QString configuredInRc = ConfigHandler().markerOcrPython().trimmed();
    if (!configuredInRc.isEmpty()) {
        const QString executable = executablePathIfUsable(configuredInRc);
        if (!executable.isEmpty()) {
            return executable;
        }
        return configuredInRc;
    }

    const QString shebangPython = pythonFromScriptShebang(markerExecutable());
    if (!shebangPython.isEmpty()) {
        return shebangPython;
    }

    return {};
}

QString markerOcrCacheHome()
{
    const QString configured =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_CACHE"))
        .trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    const QString configuredInRc = ConfigHandler().markerOcrCache().trimmed();
    if (!configuredInRc.isEmpty()) {
        return configuredInRc;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QDir currentDir(QDir::currentPath());
    const QStringList candidates = {
        appDir.absoluteFilePath(QStringLiteral("../../../.cache/datalab/models")),
        currentDir.absoluteFilePath(QStringLiteral("../.cache/datalab/models")),
        currentDir.absoluteFilePath(QStringLiteral(".cache/datalab/models")),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isDir()) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return QDir::home().filePath(
      QStringLiteral(".cache/flameshot/datalab/models"));
}

QString resourceText(const QString& path)
{
    QFile scriptFile(path);
    if (!scriptFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(scriptFile.readAll());
}

QString markerOcrServicePythonScript()
{
    return resourceText(QStringLiteral(":/scripts/marker_ocr_worker.py"));
}

QString markerOcrRouteWorkerPythonScript()
{
    return resourceText(QStringLiteral(":/scripts/marker_ocr_route_worker.py"));
}

QString markerOcrCommonPythonScript()
{
    return resourceText(QStringLiteral(":/scripts/marker_ocr_common.py"));
}

QProcessEnvironment ocrProcessEnvironment()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString keepTransformersCache =
      environment
        .value(QStringLiteral("FLAMESHOT_LATEX_OCR_KEEP_TRANSFORMERS_CACHE"))
        .trimmed()
        .toLower();
    if (keepTransformersCache != QStringLiteral("1") &&
        keepTransformersCache != QStringLiteral("true") &&
        keepTransformersCache != QStringLiteral("yes") &&
        keepTransformersCache != QStringLiteral("on")) {
        environment.remove(QStringLiteral("TRANSFORMERS_CACHE"));
    }
    environment.insert(QStringLiteral("NO_ALBUMENTATIONS_UPDATE"),
                       QStringLiteral("1"));
    environment.insert(QStringLiteral("HF_HUB_DISABLE_TELEMETRY"),
                       QStringLiteral("1"));
    environment.insert(QStringLiteral("TOKENIZERS_PARALLELISM"),
                       QStringLiteral("false"));
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    if (!environment.contains(QStringLiteral("TRANSFORMERS_VERBOSITY"))) {
        environment.insert(QStringLiteral("TRANSFORMERS_VERBOSITY"),
                           QStringLiteral("error"));
    }
    if (!environment.contains(QStringLiteral("HF_HUB_OFFLINE"))) {
        environment.insert(QStringLiteral("HF_HUB_OFFLINE"),
                           QStringLiteral("1"));
    }
    if (!environment.contains(QStringLiteral("TRANSFORMERS_OFFLINE"))) {
        environment.insert(QStringLiteral("TRANSFORMERS_OFFLINE"),
                           QStringLiteral("1"));
    }
    if (!environment.contains(QStringLiteral("HF_HOME"))) {
        environment.insert(QStringLiteral("HF_HOME"),
                           QDir::homePath() +
                             QStringLiteral("/.cache/flameshot/huggingface"));
    }
    if (!environment.contains(QStringLiteral("MPLCONFIGDIR"))) {
        environment.insert(QStringLiteral("MPLCONFIGDIR"),
                           QDir::homePath() +
                             QStringLiteral("/.cache/flameshot/matplotlib"));
    }
    if (!environment.contains(QStringLiteral("YOLO_CONFIG_DIR"))) {
        environment.insert(QStringLiteral("YOLO_CONFIG_DIR"),
                           QDir::homePath() +
                             QStringLiteral("/.cache/flameshot/ultralytics"));
    }
    if (!environment.contains(QStringLiteral("MODEL_CACHE_DIR"))) {
        environment.insert(QStringLiteral("MODEL_CACHE_DIR"),
                           markerOcrCacheHome());
    }
    if (!environment.contains(QStringLiteral("TORCH_DEVICE"))) {
        environment.insert(QStringLiteral("TORCH_DEVICE"),
                           QStringLiteral("cpu"));
    }
    if (!environment.contains(QStringLiteral("MODELSCOPE_CACHE"))) {
        environment.insert(QStringLiteral("MODELSCOPE_CACHE"),
                           QDir::homePath() +
                             QStringLiteral("/.cache/flameshot/modelscope"));
    }
    QDir().mkpath(environment.value(QStringLiteral("MODEL_CACHE_DIR")));
    QDir().mkpath(environment.value(QStringLiteral("HF_HOME")));
    QDir().mkpath(environment.value(QStringLiteral("MPLCONFIGDIR")));
    QDir().mkpath(environment.value(QStringLiteral("YOLO_CONFIG_DIR")));
    QDir().mkpath(environment.value(QStringLiteral("MODELSCOPE_CACHE")));
    return environment;
}

QProcessEnvironment markerOcrProcessEnvironment()
{
    QProcessEnvironment environment = ocrProcessEnvironment();
    const QString threads = QString::number(markerOcrThreads());
    const QString parallelThreads = QString::number(markerOcrParallelThreads());
    environment.insert(QStringLiteral("FLAMESHOT_MARKER_OCR_THREADS"), threads);
    environment.insert(QStringLiteral("FLAMESHOT_MARKER_OCR_PARALLEL_THREADS"),
                       parallelThreads);
    environment.insert(
      QStringLiteral("FLAMESHOT_MARKER_OCR_PARALLEL_SMALL_IMAGES"),
      markerOcrParallelSmallImages() ? QStringLiteral("1")
                                     : QStringLiteral("0"));
    if (!environment.contains(QStringLiteral("OMP_NUM_THREADS"))) {
        environment.insert(QStringLiteral("OMP_NUM_THREADS"), threads);
    }
    if (!environment.contains(QStringLiteral("MKL_NUM_THREADS"))) {
        environment.insert(QStringLiteral("MKL_NUM_THREADS"), threads);
    }
    if (!environment.contains(QStringLiteral("NUMEXPR_NUM_THREADS"))) {
        environment.insert(QStringLiteral("NUMEXPR_NUM_THREADS"), threads);
    }
    return environment;
}

MarkerOcr::Result failedMarkerOcrResult(const QString& error)
{
    MarkerOcr::Result result;
    result.error = error;
    return result;
}

MarkerOcr::Result markerOcrResultFromJson(const QJsonObject& object)
{
    MarkerOcr::Result result;
    result.ok = object.value(QStringLiteral("ok")).toBool();
    result.text = object.value(QStringLiteral("text")).toString();
    result.latex = object.value(QStringLiteral("latex")).toString();
    result.fallbackText =
      object.value(QStringLiteral("fallback_text")).toString();
    result.fallbackLatex =
      object.value(QStringLiteral("fallback_latex")).toString();
    result.resultInfo = object.value(QStringLiteral("result_info")).toString();
    result.fallbackInfo =
      object.value(QStringLiteral("fallback_info")).toString();
    result.extraText = object.value(QStringLiteral("extra_text")).toString();
    result.extraLatex = object.value(QStringLiteral("extra_latex")).toString();
    result.extraInfo = object.value(QStringLiteral("extra_info")).toString();
    result.error = object.value(QStringLiteral("error")).toString();
    if (!result.ok && result.error.isEmpty()) {
        result.error = QObject::tr("Marker OCR failed.");
    }
    return result;
}

class MarkerOcrService : public QObject
{
public:
    using Callback = MarkerOcr::Callback;

    explicit MarkerOcrService(QObject* parent = nullptr)
      : QObject(parent)
      , m_idleTimer(new QTimer(this))
    {
        m_idleTimer->setSingleShot(true);
        connect(m_idleTimer, &QTimer::timeout, this, [this]() {
            AbstractLogger::info(AbstractLogger::Stderr) << QObject::tr(
              "Marker OCR worker idle timeout reached; stopping.");
            stopProcess();
        });
    }

    int recognize(const QString& imagePath, Callback callback)
    {
        return enqueue(imagePath, false, std::move(callback));
    }

    int recognizeFormula(const QString& imagePath, Callback callback)
    {
        return enqueue(imagePath, true, std::move(callback));
    }

    int enqueue(const QString& imagePath, bool formulaOnly, Callback callback)
    {
        m_idleTimer->stop();
        Request request;
        request.id = m_nextRequestId++;
        request.imagePath = imagePath;
        request.formulaOnly = formulaOnly;
        request.callback = std::move(callback);
        m_queue.append(request);
        ensureProcess();
        startNextRequest();
        return request.id;
    }

    void cancel(int id)
    {
        for (int i = 0; i < m_queue.size(); ++i) {
            if (m_queue.at(i).id != id) {
                continue;
            }
            const Request request = m_queue.takeAt(i);
            if (request.callback) {
                request.callback(failedMarkerOcrResult(
                  QObject::tr("Marker OCR task cancelled")));
            }
            return;
        }

        if (m_current.id == id) {
            const Callback callback = m_current.callback;
            m_current = {};
            if (callback) {
                callback(failedMarkerOcrResult(
                  QObject::tr("Marker OCR task cancelled")));
            }
            stopProcess();
            if (!m_queue.isEmpty()) {
                ensureProcess();
            }
            startNextRequest();
        }
    }

    void stop()
    {
        for (const Request& request : m_queue) {
            if (request.callback) {
                request.callback(failedMarkerOcrResult(
                  QObject::tr("Marker OCR worker was stopped")));
            }
        }
        m_queue.clear();
        if (m_current.id != 0 && m_current.callback) {
            m_current.callback(failedMarkerOcrResult(
              QObject::tr("Marker OCR worker was stopped")));
        }
        m_current = {};
        stopProcess();
    }

    bool isRunning() const
    {
        return m_process && m_process->state() != QProcess::NotRunning;
    }

private:
    struct Request
    {
        int id = 0;
        QString imagePath;
        bool formulaOnly = false;
        Callback callback;
    };

    void ensureProcess()
    {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            return;
        }

        const QString python = markerOcrPython();
        if (python.isEmpty()) {
            failPending(QObject::tr(
              "Marker OCR requires Python with marker-pdf installed."));
            return;
        }
        const QString workerScript = markerOcrServicePythonScript();
        const QString routeWorkerScript = markerOcrRouteWorkerPythonScript();
        const QString commonWorkerScript = markerOcrCommonPythonScript();
        if (workerScript.isEmpty() || routeWorkerScript.isEmpty() ||
            commonWorkerScript.isEmpty()) {
            failPending(QObject::tr("Marker OCR worker script is missing."));
            return;
        }

        m_idleTimer->stop();
        m_ready = false;
        m_stdoutBuffer.clear();
        m_process = new QProcess(this);
        m_process->setProcessEnvironment(markerOcrProcessEnvironment());

        connect(m_process, &QProcess::started, this, [this]() {
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("Marker OCR worker started: pid=%1.")
                   .arg(QString::number(m_process->processId()));
        });
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
            drainStdout();
        });
        connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
            drainStderr();
        });
        connect(m_process,
                &QProcess::finished,
                this,
                [this](int exitCode, QProcess::ExitStatus exitStatus) {
                    handleFinished(exitCode, exitStatus);
                });

        AbstractLogger::info(AbstractLogger::Stderr)
          << QObject::tr(
               "Marker OCR worker launching: program=%1, threads=%2, "
               "parallelThreads=%3.")
               .arg(python,
                    QString::number(markerOcrThreads()),
                    QString::number(markerOcrParallelThreads()));
        m_process->start(python,
                         { QStringLiteral("-u"),
                           QStringLiteral("-c"),
                           workerScript,
                           routeWorkerScript,
                           commonWorkerScript });
    }

    void startNextRequest()
    {
        if (!m_ready || m_current.id != 0 || m_queue.isEmpty() || !m_process ||
            m_process->state() != QProcess::Running) {
            return;
        }

        m_current = m_queue.takeFirst();
        QJsonObject request;
        request.insert(QStringLiteral("cmd"),
                       m_current.formulaOnly
                         ? QStringLiteral("recognize_formula")
                         : QStringLiteral("recognize"));
        request.insert(QStringLiteral("id"), m_current.id);
        request.insert(QStringLiteral("image"), m_current.imagePath);
        const QByteArray line =
          QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        m_process->write(line);
    }

    void drainStdout()
    {
        if (!m_process) {
            return;
        }

        m_stdoutBuffer += QString::fromUtf8(m_process->readAllStandardOutput());
        int newline = m_stdoutBuffer.indexOf(QLatin1Char('\n'));
        while (newline >= 0) {
            const QString line = m_stdoutBuffer.left(newline).trimmed();
            m_stdoutBuffer.remove(0, newline + 1);
            handleProtocolLine(line);
            newline = m_stdoutBuffer.indexOf(QLatin1Char('\n'));
        }
    }

    void drainStderr()
    {
        if (!m_process) {
            return;
        }

        const QString chunk =
          QString::fromLocal8Bit(m_process->readAllStandardError());
        const QStringList lines =
          chunk.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (QString line : lines) {
            line = line.trimmed();
            if (line.isEmpty()) {
                continue;
            }
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("Marker OCR worker stderr: %1")
                   .arg(line.left(500));
        }
    }

    void handleProtocolLine(const QString& line)
    {
        if (line.isEmpty()) {
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document =
          QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !document.isObject()) {
            AbstractLogger::warning(AbstractLogger::Stderr)
              << QObject::tr("Marker OCR worker returned non-JSON output: %1")
                   .arg(line.left(500));
            return;
        }

        const QJsonObject object = document.object();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("ready")) {
            m_ready = true;
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("Marker OCR worker ready.");
            startNextRequest();
            return;
        }

        if (type != QStringLiteral("result")) {
            return;
        }

        const int id = object.value(QStringLiteral("id")).toInt();
        if (m_current.id == 0 || id != m_current.id) {
            AbstractLogger::warning(AbstractLogger::Stderr)
              << QObject::tr(
                   "Marker OCR worker returned an unexpected result id: %1")
                   .arg(QString::number(id));
            return;
        }

        const Callback callback = m_current.callback;
        const MarkerOcr::Result result = markerOcrResultFromJson(object);
        const QString traceback =
          object.value(QStringLiteral("traceback")).toString();
        if (!traceback.isEmpty()) {
            AbstractLogger::warning(AbstractLogger::Stderr)
              << QObject::tr("Marker OCR worker traceback: %1")
                   .arg(traceback.left(1200));
        }

        m_current = {};
        if (callback) {
            callback(result);
        }

        if (m_queue.isEmpty()) {
            const int idleTimeout = markerOcrIdleTimeoutMs();
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("Marker OCR worker idle timer started: %1 ms.")
                   .arg(QString::number(idleTimeout));
            m_idleTimer->start(idleTimeout);
        }
        startNextRequest();
    }

    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
    {
        Q_UNUSED(exitCode)
        Q_UNUSED(exitStatus)

        if (m_process) {
            m_process->deleteLater();
            m_process = nullptr;
        }
        m_ready = false;
        m_stdoutBuffer.clear();

        if (m_current.id != 0) {
            const Callback callback = m_current.callback;
            m_current = {};
            if (callback) {
                callback(failedMarkerOcrResult(
                  QObject::tr("Marker OCR worker exited unexpectedly")));
            }
        }

        if (!m_queue.isEmpty()) {
            ensureProcess();
        }
    }

    void failPending(const QString& error)
    {
        for (const Request& request : m_queue) {
            if (request.callback) {
                request.callback(failedMarkerOcrResult(error));
            }
        }
        m_queue.clear();
        if (m_current.id != 0 && m_current.callback) {
            m_current.callback(failedMarkerOcrResult(error));
        }
        m_current = {};
    }

    void stopProcess()
    {
        m_idleTimer->stop();
        if (!m_process) {
            m_ready = false;
            return;
        }

        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->write("{\"cmd\":\"quit\"}\n");
            m_process->closeWriteChannel();
            if (!m_process->waitForFinished(1500)) {
                m_process->kill();
                m_process->waitForFinished(1000);
            }
        }
        m_process->deleteLater();
        m_process = nullptr;
        m_ready = false;
        m_stdoutBuffer.clear();
    }

    QProcess* m_process = nullptr;
    QTimer* m_idleTimer = nullptr;
    QList<Request> m_queue;
    Request m_current;
    QString m_stdoutBuffer;
    int m_nextRequestId = 1;
    bool m_ready = false;
};

MarkerOcrService* markerOcrService()
{
    static MarkerOcrService* service = new MarkerOcrService(qApp);
    return service;
}

}

namespace MarkerOcr {
int timeoutMs()
{
    bool ok = false;
    const int timeout =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_TIMEOUT_MS"))
        .toInt(&ok);
    return ok && timeout > 0 ? timeout : 300000;
}

int recognize(const QString& imagePath, Callback callback)
{
    return markerOcrService()->recognize(imagePath, std::move(callback));
}

int recognizeFormula(const QString& imagePath, Callback callback)
{
    return markerOcrService()->recognizeFormula(imagePath, std::move(callback));
}

void cancel(int requestId)
{
    markerOcrService()->cancel(requestId);
}

void stop()
{
    markerOcrService()->stop();
}

bool isRunning()
{
    return markerOcrService()->isRunning();
}
}
