// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2021 Yurii Puchkov & Contributors

#pragma once

#include "tools/capturetool.h"

#include <QList>
#include <QPointer>

class CaptureToolObjects : public QObject
{
public:
    explicit CaptureToolObjects(QObject* parent = nullptr);
    QList<QPointer<CaptureTool>> captureToolObjects() const;
    void append(const QPointer<CaptureTool>& captureTool);
    void insert(int index, const QPointer<CaptureTool>& captureTool);
    void removeAt(int index);
    void clear();
    int size() const;
    int find(const QPoint& pos, QSize captureSize);
    QPointer<CaptureTool> at(int index);
    void assignFrom(const CaptureToolObjects& other,
                    QObject* toolParent = nullptr);
    void assignTranslatedFrom(const CaptureToolObjects& other,
                              const QPoint& offset,
                              QObject* toolParent = nullptr);
    void assignTransformedFrom(const CaptureToolObjects& other,
                               const QPointF& scale,
                               const QPointF& offset,
                               QObject* toolParent = nullptr);
    void assignMappedFrom(const CaptureToolObjects& other,
                          const QRectF& sourceRect,
                          const QRectF& targetRect,
                          QObject* toolParent = nullptr);
    CaptureToolObjects& operator=(const CaptureToolObjects& other);

private:
    int findWithRadius(QPainter& painter,
                       QPixmap& pixmap,
                       const QPoint& pos,
                       int radius = 0);

    // class members
    QList<QPointer<CaptureTool>> m_captureToolObjects;
    QVector<QImage> m_imageCache;
};
