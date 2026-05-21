// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "abstracttwopointtool.h"

#include <QCursor>
#include <QScreen>
#include <cmath>

namespace {

const double ADJ_UNIT = std::atan(1.0);
const int DIRS_NUMBER = 4;

enum UNIT
{
    HORIZ_DIR = 0,
    DIAG1_DIR = 1,
    VERT_DIR = 2
};

const double ADJ_DIAG_UNIT = 2 * ADJ_UNIT;
const int DIAG_DIRS_NUMBER = 2;

enum DIAG_UNIT
{
    DIR1 = 0
};

QPoint transformedPoint(const QPoint& point,
                        const QPointF& scale,
                        const QPointF& offset)
{
    return QPointF(point.x() * scale.x() + offset.x(),
                   point.y() * scale.y() + offset.y())
      .toPoint();
}

QPoint remappedPoint(const QPoint& point,
                     const QRectF& sourceRect,
                     const QRectF& targetRect)
{
    const qreal sourceWidth = sourceRect.width() - 1.0;
    const qreal sourceHeight = sourceRect.height() - 1.0;
    const qreal targetWidth = targetRect.width() - 1.0;
    const qreal targetHeight = targetRect.height() - 1.0;
    const qreal xRatio = sourceWidth > 0.0
                           ? (point.x() - sourceRect.left()) / sourceWidth
                           : 0.0;
    const qreal yRatio = sourceHeight > 0.0
                           ? (point.y() - sourceRect.top()) / sourceHeight
                           : 0.0;
    return QPointF(targetRect.left() + xRatio * qMax<qreal>(0.0, targetWidth),
                   targetRect.top() + yRatio * qMax<qreal>(0.0, targetHeight))
      .toPoint();
}

int scaledThickness(int thickness, const QPointF& scale)
{
    const qreal thicknessScale =
      (std::abs(scale.x()) + std::abs(scale.y())) / 2.0;
    return qMax(1, qRound(thickness * thicknessScale));
}

}

AbstractTwoPointTool::AbstractTwoPointTool(QObject* parent)
  : CaptureTool(parent)
  , m_thickness(1)
  , m_padding(0)
{}

void AbstractTwoPointTool::copyParams(const AbstractTwoPointTool* from,
                                      AbstractTwoPointTool* to)
{
    CaptureTool::copyParams(from, to);
    to->m_points.first = from->m_points.first;
    to->m_points.second = from->m_points.second;
    to->m_color = from->m_color;
    to->m_thickness = from->m_thickness;
    to->m_padding = from->m_padding;
    to->m_supportsOrthogonalAdj = from->m_supportsOrthogonalAdj;
    to->m_supportsDiagonalAdj = from->m_supportsDiagonalAdj;
}

bool AbstractTwoPointTool::isValid() const
{
    return (m_points.first != m_points.second);
}

bool AbstractTwoPointTool::closeOnButtonPressed() const
{
    return false;
}

bool AbstractTwoPointTool::isSelectable() const
{
    return true;
}

bool AbstractTwoPointTool::showMousePreview() const
{
    return true;
}

QRect AbstractTwoPointTool::mousePreviewRect(
  const CaptureContext& context) const
{
    QRect rect(0, 0, context.toolSize + 2, context.toolSize + 2);
    rect.moveCenter(context.mousePos);
    return rect;
}

QRect AbstractTwoPointTool::boundingRect() const
{
    if (!isValid()) {
        return {};
    }
    int offset =
      m_thickness <= 1 ? 1 : static_cast<int>(round(m_thickness * 0.7 + 0.5));
    QRect rect =
      QRect(std::min(m_points.first.x(), m_points.second.x()) - offset,
            std::min(m_points.first.y(), m_points.second.y()) - offset,
            std::abs(m_points.first.x() - m_points.second.x()) + offset * 2,
            std::abs(m_points.first.y() - m_points.second.y()) + offset * 2);

    return rect.normalized();
}

void AbstractTwoPointTool::drawObjectSelection(QPainter& painter)
{
    drawObjectSelectionRect(painter, boundingRect());

    const int handleSize = 9;
    const int halfHandle = handleSize / 2;
    const auto oldPen = painter.pen();
    const auto oldBrush = painter.brush();
    painter.setPen(QPen(Qt::black, 1));
    painter.setBrush(Qt::white);
    for (const auto& point : { m_points.first, m_points.second }) {
        painter.drawRect(QRect(point.x() - halfHandle,
                               point.y() - halfHandle,
                               handleSize,
                               handleSize));
    }
    painter.setBrush(oldBrush);
    painter.setPen(oldPen);
}

void AbstractTwoPointTool::drawEnd(const QPoint& p)
{
    Q_UNUSED(p)
}

void AbstractTwoPointTool::drawMove(const QPoint& p)
{
    m_points.second = p;
}

void AbstractTwoPointTool::setFirstPoint(const QPoint& point)
{
    m_points.first = point;
}

void AbstractTwoPointTool::setSecondPoint(const QPoint& point)
{
    m_points.second = point;
}

void AbstractTwoPointTool::drawMoveWithAdjustment(const QPoint& p)
{
    m_points.second = m_points.first + adjustedVector(p - m_points.first);
}

void AbstractTwoPointTool::onColorChanged(const QColor& c)
{
    m_color = c;
}

void AbstractTwoPointTool::onSizeChanged(int size)
{
    m_thickness = size;
}

void AbstractTwoPointTool::paintMousePreview(QPainter& painter,
                                             const CaptureContext& context)
{
    painter.setPen(QPen(context.color, context.toolSize));
    painter.drawLine(context.mousePos, context.mousePos);
}

void AbstractTwoPointTool::drawStart(const CaptureContext& context)
{
    onColorChanged(context.color);
    m_points.first = context.mousePos;
    m_points.second = context.mousePos;
    onSizeChanged(context.toolSize);
}

QPoint AbstractTwoPointTool::adjustedVector(QPoint v) const
{
    if (m_supportsOrthogonalAdj && m_supportsDiagonalAdj) {
        int dir = (static_cast<int>(round(atan2(-v.y(), v.x()) / ADJ_UNIT)) +
                   DIRS_NUMBER) %
                  DIRS_NUMBER;
        if (dir == UNIT::HORIZ_DIR) {
            v.setY(0);
        } else if (dir == UNIT::VERT_DIR) {
            v.setX(0);
        } else if (dir == UNIT::DIAG1_DIR) {
            int newX = (v.x() - v.y()) / 2;
            int newY = -newX;
            v.setX(newX);
            v.setY(newY);
        } else {
            int newX = (v.x() + v.y()) / 2;
            int newY = newX;
            v.setX(newX);
            v.setY(newY);
        }
    } else if (m_supportsDiagonalAdj) {
        int dir =
          (static_cast<int>(round((atan2(-v.y(), v.x()) - ADJ_DIAG_UNIT / 2) /
                                  ADJ_DIAG_UNIT)) +
           DIAG_DIRS_NUMBER) %
          DIAG_DIRS_NUMBER;
        if (dir == DIAG_UNIT::DIR1) {
            int newX = (v.x() - v.y()) / 2;
            int newY = -newX;
            v.setX(newX);
            v.setY(newY);
        } else {
            int newX = (v.x() + v.y()) / 2;
            int newY = newX;
            v.setX(newX);
            v.setY(newY);
        }
    }
    return v;
}

void AbstractTwoPointTool::move(const QPoint& pos)
{
    QPoint offset = m_points.second - m_points.first;
    m_points.first = pos;
    m_points.second = m_points.first + offset;
}

const QPoint* AbstractTwoPointTool::pos()
{
    return &m_points.first;
}

void AbstractTwoPointTool::transform(const QPointF& scale,
                                     const QPointF& offset)
{
    m_points.first = transformedPoint(m_points.first, scale, offset);
    m_points.second = transformedPoint(m_points.second, scale, offset);
    m_thickness = scaledThickness(m_thickness, scale);
}

void AbstractTwoPointTool::remap(const QRectF& sourceRect,
                                 const QRectF& targetRect)
{
    m_points.first = remappedPoint(m_points.first, sourceRect, targetRect);
    m_points.second = remappedPoint(m_points.second, sourceRect, targetRect);

    const qreal scaleX = sourceRect.width() > 0.0
                           ? targetRect.width() / sourceRect.width()
                           : 1.0;
    const qreal scaleY = sourceRect.height() > 0.0
                           ? targetRect.height() / sourceRect.height()
                           : 1.0;
    m_thickness = scaledThickness(m_thickness, QPointF(scaleX, scaleY));
}
