// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "barcodetool.h"

#include "core/flameshotdaemon.h"
#include "tools/ocr/ocrtaskwidget.h"
#include "utils/abstractlogger.h"

BarcodeTool::BarcodeTool(QObject* parent)
  : AbstractActionTool(parent)
{}

bool BarcodeTool::closeOnButtonPressed() const
{
    return true;
}

QIcon BarcodeTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "barcode.svg");
}

QString BarcodeTool::name() const
{
    return tr("Barcode");
}

CaptureTool::Type BarcodeTool::type() const
{
    return CaptureTool::TYPE_BARCODE;
}

QString BarcodeTool::description() const
{
    return tr("Recognize barcodes and 2D codes in selection");
}

CaptureTool* BarcodeTool::copy(QObject* parent)
{
    return new BarcodeTool(parent);
}

void BarcodeTool::pressed(CaptureContext& context)
{
    const QPixmap selection = context.selectedScreenshotArea();
    if (selection.isNull()) {
        AbstractLogger::error()
          << tr("Unable to prepare image for barcode scan.");
        return;
    }

    FlameshotDaemon::startOcrTask(
      selection, static_cast<int>(OcrTaskWidget::Kind::Barcode));
    emit requestAction(REQ_CLOSE_GUI_WITHOUT_CAPTURE);
}
