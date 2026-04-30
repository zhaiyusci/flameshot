// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QProcess>
#include <QPixmap>
#include <QStringList>
#include <QVector>
#include <QWidget>

class OcrTaskWidget : public QWidget
{
    Q_OBJECT
public:
    enum class Kind
    {
        Text,
        Latex
    };

    explicit OcrTaskWidget(Kind kind,
                           const QPixmap& capture,
                           QWidget* parent = nullptr);
    ~OcrTaskWidget() override;

    void start();
    void cancelTask();
    static bool isPaddleOcrServiceRunning();
    static void stopPaddleOcrService();

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
                      const QString& latex);
    void textCompleted(const QString& text);
    void latexCompleted(const QPixmap& capture, const QString& latex);
    void failed(const QString& error);
    void cancelled();

private:
    void startPaddleOcr();
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
                                        const QString& error);
    void handleTextOcrProbeFinished(const QString& output,
                                    bool ok,
                                    const QString& error);
    void drainProcessStandardError();
    void failTask(const QString& error);
    void completeTextOcr(const QString& text);
    void completeLatexOcr(const QString& latex);
    void completePaddleOcr(const QString& text, const QString& latex);
    void cleanupProcess();
    void cleanupImage();
    void setStatus(const QString& status);

    Kind m_kind;
    QPixmap m_capture;
    QProcess* m_process = nullptr;
    int m_paddleOcrRequestId = 0;
    bool m_paddleOcrRequestTimedOut = false;
    int m_textellerRequestId = 0;
    bool m_textellerRequestTimedOut = false;
    QString m_imagePath;
    QString m_formulaImagePath;
    QString m_lastError;
    QString m_processErrorOutput;
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
