// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "widgets/capture/capturetoolobjects.h"

#include <QPointer>
#include <QWidget>

class CaptureWidget;
class QLabel;
class QVBoxLayout;
class QGestureEvent;
class QPinchGesture;
class QGraphicsDropShadowEffect;

class PinWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PinWidget(const QPixmap& pixmap,
                       const QRect& geometry,
                       QWidget* parent = nullptr);
    explicit PinWidget(const QPixmap& pixmap,
                       const QRect& geometry,
                       const QPixmap& basePixmap,
                       const CaptureToolObjects& captureToolObjects,
                       QWidget* parent = nullptr);
    ~PinWidget() override;

protected:
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    bool gestureEvent(QGestureEvent* event);
    bool scrollEvent(QWheelEvent* e);
    void pinchTriggered(QPinchGesture*);
    void closePin();
    void setPinnedPixmap(const QPixmap& pixmap);
    void setPinnedState(const QPixmap& displayedPixmap,
                        const QPixmap& basePixmap,
                        const CaptureToolObjects& captureToolObjects);
    QPixmap renderPinnedPixmap() const;
    bool editableStateMatchesDisplayedPixmap(const QPixmap& pixmap) const;
    void clearStoredCaptureToolObjects();

    void rotateLeft();
    void rotateRight();
    void openTools();

    void increaseOpacity();
    void decreaseOpacity();

    QPixmap m_pixmap;
    QPixmap m_basePixmap;
    CaptureToolObjects m_captureToolObjects;
    QVBoxLayout* m_layout;
    QLabel* m_label;
    QPointer<CaptureWidget> m_editor;
    QGraphicsDropShadowEffect* m_shadowEffect;
    QColor m_baseColor, m_hoverColor;

    bool m_expanding{ false };
    qreal m_scaleFactor{ 1 };
    qreal m_opacity{ 1 };
    unsigned int m_rotateFactor{ 0 };
    qreal m_currentStepScaleFactor{ 1 };
    bool m_sizeChanged{ false };
    bool m_forceManualMove{ false };
    bool m_manualMoveActive{ false };
    QPoint m_manualMoveOffset;

private slots:
    void showContextMenu(const QPoint& pos);
    void copyToClipboard();
    void saveToFile();
};
