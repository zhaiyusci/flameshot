// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "ocrtool.h"

#include "core/flameshotdaemon.h"
#include "tools/ocr/ocrtaskwidget.h"
#include "tools/ocr/ocrresultwidget.h"
#include "utils/abstractlogger.h"

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
