// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "tools/ocr/markerocrservice.h"

#include <QPixmap>
#include <QThread>
#include <QWidget>

#include <functional>

class OcrTaskWidget : public QWidget
{
    Q_OBJECT
public:
    enum class Kind
    {
        Text,
        Barcode
    };

    explicit OcrTaskWidget(Kind kind,
                           const QPixmap& capture,
                           QWidget* parent = nullptr);
    ~OcrTaskWidget() override;

    void start();
    void cancelTask();
    static bool isMarkerOcrServiceRunning();
    static void stopMarkerOcrService();
    using MarkerFormulaCallback =
      std::function<void(const MarkerOcr::Result&)>;
    static int requestMarkerFormulaOcr(const QPixmap& capture,
                                       MarkerFormulaCallback callback);
    static void cancelMarkerOcrRequest(int requestId);

signals:
    void statusChanged(const QString& status);
    void preparedImageReady(const QString& imagePath);
    void ocrCompleted(const QPixmap& capture,
                      const QString& text,
                      const QString& latex,
                      const QString& fallbackText,
                      const QString& fallbackLatex,
                      const QString& resultInfo,
                      const QString& fallbackInfo,
                      const QString& extraText,
                      const QString& extraLatex,
                      const QString& extraInfo);
    void failed(const QString& error);
    void cancelled();

private:
    void startMarkerOcr();
    void startBarcodeScan();
    void handleMarkerOcrServiceFinished(const MarkerOcr::Result& result);
    void handleBarcodeScanFinished(const QString& result, const QString& error);
    void failTask(const QString& error);
    void completeStructuredOcr(const MarkerOcr::Result& result);
    void completeBarcodeScan(const QString& result);
    void cleanupProcess();
    void cleanupImage();
    void setStatus(const QString& status);

    Kind m_kind;
    QPixmap m_capture;
    int m_markerOcrRequestId = 0;
    bool m_markerOcrRequestTimedOut = false;
    QString m_imagePath;
    QString m_lastError;
    QThread* m_barcodeThread = nullptr;
    bool m_cancelled = false;
};
