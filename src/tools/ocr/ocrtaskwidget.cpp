// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "ocrtaskwidget.h"

#include "tools/barcode/barcodereader.h"
#include "tools/ocr/markerocrservice.h"
#include "utils/abstractlogger.h"
#include "utils/confighandler.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMetaObject>
#include <QPointer>
#include <QProcessEnvironment>
#include <QTemporaryFile>
#include <QTimer>

#include <utility>

namespace {
QString configuredOcrBackend()
{
    QString backend = QProcessEnvironment::systemEnvironment()
                        .value(QStringLiteral("FLAMESHOT_OCR_BACKEND"))
                        .trimmed()
                        .toLower();
    if (backend.isEmpty()) {
        backend = ConfigHandler().ocrBackend().trimmed().toLower();
    }
    backend.replace(QLatin1Char('-'), QLatin1Char('_'));
    return backend.isEmpty() ? QStringLiteral("auto") : backend;
}

bool keepOcrTempImage()
{
    const QString keep = QProcessEnvironment::systemEnvironment()
                           .value(QStringLiteral("FLAMESHOT_OCR_KEEP_TEMP"))
                           .trimmed()
                           .toLower();
    return keep == QStringLiteral("1") || keep == QStringLiteral("true") ||
           keep == QStringLiteral("yes") || keep == QStringLiteral("on");
}

QString latexPreview(const QString& latex)
{
    QString preview = latex.simplified();
    if (preview.size() > 120) {
        preview = preview.left(120) + QStringLiteral("...");
    }
    return preview;
}

QString ocrTextPreview(const QString& text)
{
    QString preview = text.simplified();
    if (preview.size() > 120) {
        preview = preview.left(120) + QStringLiteral("...");
    }
    return preview;
}
}

OcrTaskWidget::OcrTaskWidget(Kind kind, const QPixmap& capture, QWidget* parent)
  : QWidget(parent)
  , m_kind(kind)
  , m_capture(capture)
{
    setAttribute(Qt::WA_DeleteOnClose);
}

OcrTaskWidget::~OcrTaskWidget()
{
    cleanupProcess();
    cleanupImage();
}

void OcrTaskWidget::stopMarkerOcrService()
{
    MarkerOcr::stop();
}

bool OcrTaskWidget::isMarkerOcrServiceRunning()
{
    return MarkerOcr::isRunning();
}

int OcrTaskWidget::requestMarkerFormulaOcr(const QPixmap& capture,
                                           MarkerFormulaCallback callback)
{
    if (capture.isNull()) {
        QTimer::singleShot(0, qApp, [callback = std::move(callback)]() {
            if (callback) {
                callback(false,
                         QString(),
                         QString(),
                         QString(),
                         QObject::tr("Unable to prepare image."));
            }
        });
        return 0;
    }

    QTemporaryFile imageFile(
      QDir::tempPath() +
      QStringLiteral("/flameshot-marker-formula-route-XXXXXX.png"));
    imageFile.setAutoRemove(false);
    if (!imageFile.open() || !capture.toImage()
                                .convertToFormat(QImage::Format_RGB32)
                                .save(&imageFile, "PNG")) {
        QFile::remove(imageFile.fileName());
        QTimer::singleShot(0, qApp, [callback = std::move(callback)]() {
            if (callback) {
                callback(false,
                         QString(),
                         QString(),
                         QString(),
                         QObject::tr("Unable to create a temporary image for "
                                     "formula OCR."));
            }
        });
        return 0;
    }
    imageFile.close();
    const QString imagePath = QFileInfo(imageFile.fileName()).absoluteFilePath();

    AbstractLogger::info(AbstractLogger::Stderr)
      << QObject::tr("Marker formula route queued in background: image=%1, "
                     "timeout=%2 ms.")
           .arg(imagePath, QString::number(MarkerOcr::timeoutMs()));

    return MarkerOcr::recognizeFormula(
      imagePath,
      [imagePath, callback = std::move(callback)](
        bool ok,
        const QString& text,
        const QString& latex,
        const QString& fallbackText,
        const QString& fallbackLatex,
        const QString& resultInfo,
        const QString& fallbackInfo,
        const QString& extraText,
        const QString& extraLatex,
        const QString& extraInfo,
        const QString& error) {
          Q_UNUSED(fallbackText)
          Q_UNUSED(fallbackLatex)
          Q_UNUSED(fallbackInfo)
          Q_UNUSED(extraText)
          Q_UNUSED(extraLatex)
          Q_UNUSED(extraInfo)
          if (keepOcrTempImage()) {
              AbstractLogger::info(AbstractLogger::Stderr)
                << QObject::tr("Keeping OCR temporary image: %1")
                     .arg(imagePath);
          } else {
              QFile::remove(imagePath);
          }
          if (callback) {
              callback(ok, text, latex, resultInfo, error);
          }
      });
}

void OcrTaskWidget::cancelMarkerOcrRequest(int requestId)
{
    if (requestId != 0) {
        MarkerOcr::cancel(requestId);
    }
}

void OcrTaskWidget::start()
{
    if (m_capture.isNull()) {
        failTask(tr("Unable to prepare image."));
        return;
    }

    if (m_kind == Kind::Barcode) {
        startBarcodeScan();
        return;
    }

    const QString backend = configuredOcrBackend();
    if (backend == QStringLiteral("marker") ||
        backend == QStringLiteral("auto")) {
        startMarkerOcr();
        return;
    }

    failTask(tr("Unsupported OCR backend '%1'. Marker is the only supported "
                "OCR backend.")
               .arg(backend));
}

void OcrTaskWidget::startBarcodeScan()
{
    setStatus(tr("Scanning barcode..."));
    const QImage image = m_capture.toImage();
    auto* thread =
      QThread::create([guard = QPointer<OcrTaskWidget>(this), image]() {
          const BarcodeReader::ScanResult scan =
            BarcodeReader::scanImage(image);
          const QString result = BarcodeReader::formatResults(scan.results);
          const QString error = scan.error;
          if (!guard) {
              return;
          }
          QMetaObject::invokeMethod(
            guard,
            [guard, result, error]() {
                if (guard) {
                    guard->handleBarcodeScanFinished(result, error);
                }
            },
            Qt::QueuedConnection);
      });
    m_barcodeThread = thread;
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_barcodeThread == thread) {
            m_barcodeThread = nullptr;
        }
        thread->deleteLater();
    });
    thread->start();
}

void OcrTaskWidget::startMarkerOcr()
{
    setStatus(tr("Preparing Marker OCR..."));

    QTemporaryFile imageFile(QDir::tempPath() +
                             QStringLiteral("/flameshot-marker-ocr-XXXXXX.png"));
    imageFile.setAutoRemove(false);
    if (!imageFile.open() || !m_capture.toImage()
                                .convertToFormat(QImage::Format_RGB32)
                                .save(&imageFile, "PNG")) {
        QFile::remove(imageFile.fileName());
        failTask(tr("Unable to create a temporary image for OCR."));
        return;
    }
    imageFile.close();
    m_imagePath = QFileInfo(imageFile.fileName()).absoluteFilePath();
    emit preparedImageReady(m_imagePath);

    AbstractLogger::info(AbstractLogger::Stderr)
      << tr("Marker OCR queued in background: image=%1, timeout=%2 ms.")
           .arg(m_imagePath, QString::number(MarkerOcr::timeoutMs()));

    setStatus(tr("Running Marker OCR..."));
    m_markerOcrRequestTimedOut = false;
    m_markerOcrRequestId = MarkerOcr::recognize(
      m_imagePath,
      [guard = QPointer<OcrTaskWidget>(this)](bool ok,
                                              const QString& text,
                                              const QString& latex,
                                              const QString& fallbackText,
                                              const QString& fallbackLatex,
                                              const QString& resultInfo,
                                              const QString& fallbackInfo,
                                              const QString& extraText,
                                              const QString& extraLatex,
                                              const QString& extraInfo,
                                              const QString& error) {
          if (guard) {
              guard->handleMarkerOcrServiceFinished(ok,
                                                    text,
                                                    latex,
                                                    fallbackText,
                                                    fallbackLatex,
                                                    resultInfo,
                                                    fallbackInfo,
                                                    extraText,
                                                    extraLatex,
                                                    extraInfo,
                                                    error);
          }
    });

    const int requestId = m_markerOcrRequestId;
    QTimer::singleShot(MarkerOcr::timeoutMs(), this, [this, requestId]() {
        if (m_markerOcrRequestId != requestId) {
            return;
        }
        m_markerOcrRequestTimedOut = true;
        m_lastError = tr("Marker OCR backend timed out.");
        AbstractLogger::warning(AbstractLogger::Stderr) << m_lastError;
        MarkerOcr::cancel(requestId);
    });
}

void OcrTaskWidget::handleMarkerOcrServiceFinished(bool ok,
                                                   const QString& text,
                                                   const QString& latex,
                                                   const QString& fallbackText,
                                                   const QString& fallbackLatex,
                                                   const QString& resultInfo,
                                                   const QString& fallbackInfo,
                                                   const QString& extraText,
                                                   const QString& extraLatex,
                                                   const QString& extraInfo,
                                                   const QString& error)
{
    if (m_markerOcrRequestId == 0) {
        return;
    }

    const bool timedOut = m_markerOcrRequestTimedOut;
    m_markerOcrRequestId = 0;
    m_markerOcrRequestTimedOut = false;

    if (m_cancelled) {
        emit cancelled();
        close();
        return;
    }

    if (!ok || timedOut) {
        m_lastError = timedOut ? tr("Marker OCR backend timed out.")
                               : tr("Marker OCR backend failed: %1").arg(error);
        AbstractLogger::warning(AbstractLogger::Stderr) << m_lastError;
        failTask(m_lastError);
        return;
    }

    completeStructuredOcr(text.trimmed(),
                          latex.trimmed(),
                          fallbackText.trimmed(),
                          fallbackLatex.trimmed(),
                          resultInfo.trimmed(),
                          fallbackInfo.trimmed(),
                          extraText.trimmed(),
                          extraLatex.trimmed(),
                          extraInfo.trimmed());
}

void OcrTaskWidget::cancelTask()
{
    m_cancelled = true;
    setStatus(tr("Cancelling task..."));
    AbstractLogger::info(AbstractLogger::Stderr)
      << tr("Background task cancelled.");
    if (m_barcodeThread && m_barcodeThread->isRunning()) {
        m_barcodeThread->requestInterruption();
        return;
    }
    if (m_markerOcrRequestId != 0) {
        MarkerOcr::cancel(m_markerOcrRequestId);
        return;
    }
    emit cancelled();
    close();
}

void OcrTaskWidget::handleBarcodeScanFinished(const QString& result,
                                              const QString& error)
{
    if (m_cancelled) {
        emit cancelled();
        close();
        return;
    }
    if (result.isEmpty()) {
        failTask(error.isEmpty() ? tr("No barcode or 2D code was recognized.")
                                 : error);
        return;
    }
    completeBarcodeScan(result);
}

void OcrTaskWidget::failTask(const QString& error)
{
    setStatus(error);
    AbstractLogger::error() << error;
    emit failed(error);
    close();
}

void OcrTaskWidget::completeStructuredOcr(const QString& text,
                                          const QString& latex,
                                          const QString& fallbackText,
                                          const QString& fallbackLatex,
                                          const QString& resultInfo,
                                          const QString& fallbackInfo,
                                          const QString& extraText,
                                          const QString& extraLatex,
                                          const QString& extraInfo)
{
    cleanupImage();
    if (text.isEmpty() && latex.isEmpty() && fallbackText.isEmpty() &&
        fallbackLatex.isEmpty() && extraText.isEmpty() &&
        extraLatex.isEmpty()) {
        AbstractLogger::warning() << tr("No text or formula was recognized.");
    } else {
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("OCR backend succeeded: textChars=%1, latexChars=%2, "
                "fallbackChars=%3, extraChars=%4, text=%5, latex=%6")
               .arg(QString::number(text.size()),
                    QString::number(latex.size()),
                    QString::number(fallbackText.size() + fallbackLatex.size()),
                    QString::number(extraText.size() + extraLatex.size()),
                    ocrTextPreview(text),
                    latexPreview(latex));
    }
    emit ocrCompleted(m_capture,
                      text,
                      latex,
                      fallbackText,
                      fallbackLatex,
                      resultInfo,
                      fallbackInfo,
                      extraText,
                      extraLatex,
                      extraInfo);
    close();
}

void OcrTaskWidget::completeBarcodeScan(const QString& result)
{
    AbstractLogger::info(AbstractLogger::Stderr)
      << tr("Barcode scan succeeded: chars=%1, text=%2")
           .arg(QString::number(result.size()), ocrTextPreview(result));
    emit ocrCompleted(m_capture,
                      result,
                      QString(),
                      QString(),
                      QString(),
                      QString(),
                      QString(),
                      QString(),
                      QString(),
                      QString());
    close();
}

void OcrTaskWidget::cleanupProcess()
{
    if (m_barcodeThread) {
        QThread* thread = m_barcodeThread;
        m_barcodeThread = nullptr;
        thread->requestInterruption();
        thread->wait();
        thread->disconnect(this);
        thread->deleteLater();
    }

    if (m_markerOcrRequestId != 0) {
        const int requestId = m_markerOcrRequestId;
        m_markerOcrRequestId = 0;
        MarkerOcr::cancel(requestId);
    }
}

void OcrTaskWidget::cleanupImage()
{
    if (m_imagePath.isEmpty()) {
        return;
    }

    if (keepOcrTempImage()) {
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("Keeping OCR temporary image: %1").arg(m_imagePath);
        m_imagePath.clear();
        return;
    }

    QFile::remove(m_imagePath);
    m_imagePath.clear();
}

void OcrTaskWidget::setStatus(const QString& status)
{
    emit statusChanged(status);
}
