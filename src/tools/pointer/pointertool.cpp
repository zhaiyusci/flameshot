// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Flameshot Contributors

#include "pointertool.h"

PointerTool::PointerTool(QObject* parent)
  : AbstractActionTool(parent)
{}

bool PointerTool::closeOnButtonPressed() const
{
    return false;
}

QIcon PointerTool::icon(const QColor& background, bool inEditor) const
{
    Q_UNUSED(inEditor)
    return QIcon(iconPath(background) + "cursor-arrow.svg");
}

QString PointerTool::name() const
{
    return tr("Pointer");
}

CaptureTool::Type PointerTool::type() const
{
    return CaptureTool::TYPE_POINTER;
}

QString PointerTool::description() const
{
    return tr("Select and edit annotations");
}

bool PointerTool::isSelectable() const
{
    return true;
}

CaptureTool* PointerTool::copy(QObject* parent)
{
    return new PointerTool(parent);
}

void PointerTool::pressed(CaptureContext& context)
{
    Q_UNUSED(context)
}
