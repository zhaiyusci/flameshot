// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "tools/ocr/ocrtaskwidget.h"

#include <QPointer>
#include <QPixmap>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTimer;

#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
class QWebEngineView;
#endif

class OcrJobManagerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OcrJobManagerWidget(QWidget* parent = nullptr);

    void addTask(OcrTaskWidget::Kind kind,
                 const QPixmap& capture,
                 const QString& requestId = QString());
    int runningJobCount() const;

signals:
    void tasksChanged();
    void stopOcrWorkerRequested();
    void taskFinished(const QString& requestId,
                      int kind,
                      bool ok,
                      const QString& result,
                      const QString& error,
                      const QString& preparedImagePath);

private:
    struct Job
    {
        int id = 0;
        OcrTaskWidget::Kind kind = OcrTaskWidget::Kind::Text;
        QPixmap capture;
        QString status;
        QString result;
        QString text;
        QString latex;
        QString error;
        QString requestId;
        QString preparedImagePath;
        QPointer<OcrTaskWidget> task;
        bool completed = false;
        bool failed = false;
        bool cancelled = false;
    };

    void addJobRow(const Job& job);
    void updateJobRow(int index);
    void updateDetails();
    void updateButtons();
    void updateLatexPreview(const Job& job);
    void clearLatexPreview();
    void setLatexPreviewMessage(const QString& message);
    QString katexHtml(const QString& latex) const;
    QString markdownHtml(const QString& markdown) const;
    QString findKatexDist() const;
    QString jobResultText(const Job& job) const;
    int selectedJobIndex() const;
    int jobIndexById(int id) const;
    QString jobTypeText(const Job& job) const;
    QString jobStatusText(const Job& job) const;
    void openJobResult(int index);
    void copySelectedResult();
    void openSelectedResult();
    void cancelSelectedJob();
    void killAllRunningJobs();
    void clearHistory();

    QTableWidget* m_table;
    QLabel* m_imagePreview;
    QPlainTextEdit* m_resultPreview;
#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
    QWebEngineView* m_latexPreview;
#else
    QLabel* m_latexPreview;
#endif
    QPushButton* m_copyButton;
    QPushButton* m_openButton;
    QPushButton* m_killButton;
    QPushButton* m_killAllButton;
    QPushButton* m_clearHistoryButton;
    QPushButton* m_stopOcrWorkerButton;
    QTimer* m_workerStateTimer;
    QString m_katexDist;
    QList<Job> m_jobs;
    int m_nextJobId = 1;
};
