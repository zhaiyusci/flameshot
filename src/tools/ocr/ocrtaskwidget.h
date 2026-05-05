// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QPixmap>
#include <QProcess>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <QWidget>

#include <functional>

class OcrTaskWidget : public QWidget
{
    Q_OBJECT
public:
    enum class Kind
    {
        Text,
        Latex,
        Barcode
    };

    explicit OcrTaskWidget(Kind kind,
                           const QPixmap& capture,
                           QWidget* parent = nullptr);
    ~OcrTaskWidget() override;

    void start();
    void cancelTask();
    static bool isPaddleOcrServiceRunning();
    static bool isMarkerOcrServiceRunning();
    static void stopPaddleOcrService();
    static void stopMarkerOcrService();
    using MarkerFormulaCallback = std::function<void(bool,
                                                     const QString&,
                                                     const QString&,
                                                     const QString&,
                                                     const QString&)>;
    static int requestMarkerFormulaOcr(const QPixmap& capture,
                                       MarkerFormulaCallback callback);
    static void cancelMarkerOcrRequest(int requestId);

    struct BackendCommand
    {
        QString backendName;
        QString program;
        QStringList arguments;
    };

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
    void textCompleted(const QString& text);
    void latexCompleted(const QPixmap& capture, const QString& latex);
    void failed(const QString& error);
    void cancelled();

private:
    void startPaddleOcr();
    void startMarkerOcr();
    void startBarcodeScan();
    void startTextOcr();
    void startNextTextOcrCandidate();
    void startFinalTextOcr(const QString& language);
    void startLatexOcr();
    void startNextLatexBackend();
    void startProcess(const BackendCommand& command);
    void handleProcessFinished(QProcess* process,
                               int exitCode,
                               QProcess::ExitStatus exitStatus);
    void handleProcessFailedToStart(QProcess* process);
    void handleTextellerServiceFinished(const QString& backendName,
                                        bool ok,
                                        const QString& result);
    void handlePaddleOcrServiceFinished(bool ok,
                                        const QString& text,
                                        const QString& latex,
                                        const QString& fallbackText,
                                        const QString& fallbackLatex,
                                        const QString& resultInfo,
                                        const QString& fallbackInfo,
                                        const QString& extraText,
                                        const QString& extraLatex,
                                        const QString& extraInfo,
                                        const QString& error);
    void handleMarkerOcrServiceFinished(bool ok,
                                        const QString& text,
                                        const QString& latex,
                                        const QString& fallbackText,
                                        const QString& fallbackLatex,
                                        const QString& resultInfo,
                                        const QString& fallbackInfo,
                                        const QString& extraText,
                                        const QString& extraLatex,
                                        const QString& extraInfo,
                                        const QString& error);
    void handleBarcodeScanFinished(const QString& result, const QString& error);
    void handleTextOcrProbeFinished(const QString& output,
                                    bool ok,
                                    const QString& error);
    void drainProcessStandardError();
    void failTask(const QString& error);
    void completeTextOcr(const QString& text);
    void completeLatexOcr(const QString& latex);
    void completePaddleOcr(const QString& text,
                           const QString& latex,
                           const QString& fallbackText,
                           const QString& fallbackLatex,
                           const QString& resultInfo,
                           const QString& fallbackInfo,
                           const QString& extraText,
                           const QString& extraLatex,
                           const QString& extraInfo);
    void completeBarcodeScan(const QString& result);
    void cleanupProcess();
    void cleanupImage();
    void setStatus(const QString& status);

    Kind m_kind;
    QPixmap m_capture;
    QProcess* m_process = nullptr;
    int m_paddleOcrRequestId = 0;
    bool m_paddleOcrRequestTimedOut = false;
    int m_markerOcrRequestId = 0;
    bool m_markerOcrRequestTimedOut = false;
    int m_textellerRequestId = 0;
    bool m_textellerRequestTimedOut = false;
    QString m_imagePath;
    QString m_formulaImagePath;
    QString m_lastError;
    QString m_processErrorOutput;
    QThread* m_barcodeThread = nullptr;
    QVector<BackendCommand> m_latexCommands;
    struct TextOcrCandidateResult
    {
        QString language;
        QString text;
        QString error;
        qreal confidence = 0.0;
        int wordCount = 0;
        int chineseCount = 0;
        int latinCount = 0;
        bool ok = false;
    };
    QStringList m_textLanguageCandidates;
    QVector<TextOcrCandidateResult> m_textCandidateResults;
    int m_currentBackend = -1;
    int m_currentTextCandidate = -1;
    bool m_textAutoSelectingLanguage = false;
    bool m_cancelled = false;
};
