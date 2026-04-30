// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "ocrtool.h"

#include "core/flameshotdaemon.h"
#include "tools/ocr/ocrtaskwidget.h"
#include "tools/ocr/ocrresultwidget.h"
#include "utils/abstractlogger.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSize>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryFile>

namespace {
QImage paddedOcrImage(const QPixmap& pixmap)
{
    constexpr int padding = 16;

    const QImage source = pixmap.toImage();
    QImage image(source.size() + QSize(padding * 2, padding * 2),
                 QImage::Format_RGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.drawImage(padding, padding, source);
    return image;
}

QStringList availableOcrLanguages(const QString& tesseract)
{
    QProcess process;
    process.start(tesseract, { QStringLiteral("--list-langs") });
    if (!process.waitForStarted() || !process.waitForFinished(5000) ||
        process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        return {};
    }

    QStringList languages;
    const auto lines = QString::fromUtf8(process.readAllStandardOutput())
                         .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (!line.startsWith(QStringLiteral("List of available languages"))) {
            languages << line;
        }
    }
    return languages;
}

QString ocrLanguage(const QString& tesseract)
{
    const QString configuredLanguage =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_OCR_LANGUAGE"))
        .trimmed();
    if (!configuredLanguage.isEmpty()) {
        return configuredLanguage;
    }

    const QStringList languages = availableOcrLanguages(tesseract);
    if (languages.contains(QStringLiteral("chi_sim"))) {
        return languages.contains(QStringLiteral("eng"))
                 ? QStringLiteral("chi_sim+eng")
                 : QStringLiteral("chi_sim");
    }

    return {};
}

QString ocrPageSegMode()
{
    const QString configuredPageSegMode =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_OCR_PSM"))
        .trimmed();
    return configuredPageSegMode.isEmpty() ? QStringLiteral("6")
                                           : configuredPageSegMode;
}

QString tesseractErrorMessage(QProcess& process)
{
    QString errorOutput =
      QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (errorOutput.isEmpty()) {
        return QObject::tr("OCR failed.");
    }
    errorOutput.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return QObject::tr("OCR failed: %1").arg(errorOutput.left(240));
}
}

OcrTool::OcrTool(QObject* parent)
  : AbstractActionTool(parent)
{}

bool OcrTool::closeOnButtonPressed() const
{
    return true;
}

QIcon OcrTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "ocr.svg");
}

QString OcrTool::name() const
{
    return tr("OCR");
}

CaptureTool::Type OcrTool::type() const
{
    return CaptureTool::TYPE_OCR;
}

QString OcrTool::description() const
{
    return tr("Recognize text and formulas in selection");
}

QWidget* OcrTool::widget()
{
    return new OcrResultWidget(m_ocrText);
}

CaptureTool* OcrTool::copy(QObject* parent)
{
    return new OcrTool(parent);
}

void OcrTool::pressed(CaptureContext& context)
{
    const QPixmap selection = context.selectedScreenshotArea();
    if (selection.isNull()) {
        AbstractLogger::error() << tr("Unable to prepare image for OCR.");
        return;
    }

    FlameshotDaemon::startOcrTask(
      selection, static_cast<int>(OcrTaskWidget::Kind::Text));
    emit requestAction(REQ_CLOSE_GUI_WITHOUT_CAPTURE);
}
