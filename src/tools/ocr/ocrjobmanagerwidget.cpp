// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "ocrjobmanagerwidget.h"

#include "core/flameshotdaemon.h"
#include "tools/ocr/ocrpreviewrenderer.h"
#include "tools/ocr/ocrresultwidget.h"
#include "utils/globalvalues.h"

#include <QAbstractItemView>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSizePolicy>
#include <QSplitter>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
#include <QWebEngineView>
#endif

namespace {
QWidget* labeledPane(const QString& title,
                     QWidget* content,
                     QWidget* parent,
                     QLabel** titleLabelOut = nullptr)
{
    auto* pane = new QWidget(parent);
    pane->setMinimumSize(220, 160);
    pane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* titleWidget = new QLabel(title, pane);
    titleWidget->setStyleSheet(QStringLiteral("font-weight: 600;"));
    if (titleLabelOut) {
        *titleLabelOut = titleWidget;
    }

    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(titleWidget);
    layout->addWidget(content, 1);
    return pane;
}

struct BarcodeDisplayResult
{
    QString text;
    QString info;
};

BarcodeDisplayResult barcodeDisplayResult(const QString& raw)
{
    BarcodeDisplayResult result;
    QStringList textLines;
    QStringList formats;
    const QStringList lines = raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const int tabIndex = line.indexOf(QLatin1Char('\t'));
        if (tabIndex < 0) {
            textLines << line;
            continue;
        }
        const QString format = line.left(tabIndex).trimmed();
        const QString text = line.mid(tabIndex + 1);
        if (!format.isEmpty() && !formats.contains(format)) {
            formats << format;
        }
        textLines << text;
    }
    result.text = textLines.join(QLatin1Char('\n'));
    result.info = formats.join(QStringLiteral(", "));
    return result;
}

bool suppressOcrResultPopup()
{
    const QString value =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_OCR_SUPPRESS_RESULT_POPUP"))
        .trimmed()
        .toLower();
    return value == QStringLiteral("1") || value == QStringLiteral("true") ||
           value == QStringLiteral("yes") || value == QStringLiteral("on");
}
}

OcrJobManagerWidget::OcrJobManagerWidget(QWidget* parent)
  : QWidget(parent)
  , m_table(new QTableWidget(this))
  , m_imagePreview(new QLabel(this))
  , m_resultPaneTitle(nullptr)
  , m_resultPreview(new QPlainTextEdit(this))
  , m_latexPreview(new
#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
                   QWebEngineView
#else
                   QLabel
#endif
                   (this))
  , m_copyButton(new QPushButton(tr("Copy Result"), this))
  , m_openButton(new QPushButton(tr("Open Result"), this))
  , m_killButton(new QPushButton(tr("Kill Task"), this))
  , m_killAllButton(new QPushButton(tr("Kill All"), this))
  , m_clearHistoryButton(new QPushButton(tr("Clear History"), this))
  , m_stopOcrWorkerButton(new QPushButton(tr("Stop OCR Worker"), this))
  , m_workerStateTimer(new QTimer(this))
  , m_katexDist(OcrPreviewRenderer::findKatexDist())
{
    setWindowIcon(QIcon(GlobalValues::iconPath()));
    setWindowTitle(tr("Background Tasks"));
    resize(920, 560);

    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({ tr("ID"), tr("Type"), tr("Status") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);

    m_imagePreview->setAlignment(Qt::AlignCenter);
    m_imagePreview->setMinimumHeight(160);
    m_imagePreview->setStyleSheet(QStringLiteral(
      "QLabel { background: #f6f8fa; border: 1px solid #d0d7de; }"));
    m_imagePreview->setSizePolicy(QSizePolicy::Expanding,
                                  QSizePolicy::Expanding);

    m_resultPreview->setReadOnly(true);
    m_resultPreview->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_resultPreview->setMinimumSize(220, 180);

#if !defined(FLAMESHOT_HAVE_QT_WEBENGINE)
    m_latexPreview->setAlignment(Qt::AlignCenter);
    m_latexPreview->setWordWrap(true);
    m_latexPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
#endif
    m_latexPreview->setMinimumHeight(180);
    m_latexPreview->setMinimumWidth(220);
    m_latexPreview->setSizePolicy(QSizePolicy::Expanding,
                                  QSizePolicy::Expanding);

    auto* sourcePreviewSplitter = new QSplitter(Qt::Horizontal, this);
    sourcePreviewSplitter->addWidget(
      labeledPane(tr("Result"), m_resultPreview, this, &m_resultPaneTitle));
    sourcePreviewSplitter->addWidget(
      labeledPane(tr("Preview"), m_latexPreview, this));
    sourcePreviewSplitter->setChildrenCollapsible(false);
    sourcePreviewSplitter->setStretchFactor(0, 1);
    sourcePreviewSplitter->setStretchFactor(1, 1);
    sourcePreviewSplitter->setSizes({ 1, 1 });

    auto* details = new QSplitter(Qt::Vertical, this);
    details->addWidget(labeledPane(tr("Original"), m_imagePreview, this));
    details->addWidget(sourcePreviewSplitter);
    details->setChildrenCollapsible(false);
    details->setStretchFactor(0, 1);
    details->setStretchFactor(1, 2);
    details->setSizes({ 1, 2 });

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_table);
    splitter->addWidget(details);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_copyButton);
    buttonLayout->addWidget(m_openButton);
    buttonLayout->addWidget(m_killButton);
    buttonLayout->addWidget(m_killAllButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_stopOcrWorkerButton);
    buttonLayout->addWidget(m_clearHistoryButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(splitter);
    layout->addLayout(buttonLayout);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        updateDetails();
        updateButtons();
    });
    connect(m_copyButton,
            &QPushButton::clicked,
            this,
            &OcrJobManagerWidget::copySelectedResult);
    connect(m_openButton,
            &QPushButton::clicked,
            this,
            &OcrJobManagerWidget::openSelectedResult);
    connect(m_killButton,
            &QPushButton::clicked,
            this,
            &OcrJobManagerWidget::cancelSelectedJob);
    connect(m_killAllButton,
            &QPushButton::clicked,
            this,
            &OcrJobManagerWidget::killAllRunningJobs);
    connect(m_clearHistoryButton,
            &QPushButton::clicked,
            this,
            &OcrJobManagerWidget::clearHistory);
    connect(m_stopOcrWorkerButton, &QPushButton::clicked, this, [this]() {
        OcrTaskWidget::stopMarkerOcrService();
        updateButtons();
        emit stopOcrWorkerRequested();
    });
    m_workerStateTimer->setInterval(1000);
    connect(m_workerStateTimer, &QTimer::timeout, this, [this]() {
        updateButtons();
    });
    m_workerStateTimer->start();

    updateDetails();
    updateButtons();
}

void OcrJobManagerWidget::addTask(OcrTaskWidget::Kind kind,
                                  const QPixmap& capture,
                                  const QString& requestId)
{
    Job job;
    job.id = m_nextJobId++;
    job.kind = kind;
    job.capture = capture;
    job.requestId = requestId;
    job.status = tr("Queued");
    job.task = new OcrTaskWidget(kind, capture, this);

    const int jobId = job.id;
    connect(job.task,
            &OcrTaskWidget::preparedImageReady,
            this,
            [this, jobId](const QString& imagePath) {
                const int index = jobIndexById(jobId);
                if (index < 0) {
                    return;
                }
                m_jobs[index].preparedImagePath = imagePath;
            });
    connect(job.task,
            &OcrTaskWidget::statusChanged,
            this,
            [this, jobId](const QString& status) {
                const int index = jobIndexById(jobId);
                if (index < 0) {
                    return;
                }
                m_jobs[index].status = status;
                updateJobRow(index);
                updateDetails();
                emit tasksChanged();
            });
    connect(
      job.task,
      &OcrTaskWidget::ocrCompleted,
      this,
      [this, jobId](const OcrTaskResult& result) {
          const int index = jobIndexById(jobId);
          if (index < 0) {
              return;
          }
          MarkerOcr::Result ocr = result.ocr;
          m_jobs[index].capture = result.capture;
          if (m_jobs[index].kind == OcrTaskWidget::Kind::Barcode) {
              const BarcodeDisplayResult barcode = barcodeDisplayResult(ocr.text);
              ocr.text = barcode.text;
              ocr.resultInfo = barcode.info;
          }
          m_jobs[index].ocr = ocr;
          m_jobs[index].status = tr("Finished");
          m_jobs[index].completed = true;
          m_jobs[index].task = nullptr;
          updateJobRow(index);
          updateDetails();
          updateButtons();
          if (!suppressOcrResultPopup()) {
              openJobResult(index);
          }
          emit taskFinished(m_jobs.at(index).requestId,
                            static_cast<int>(m_jobs.at(index).kind),
                            true,
                            jobResultText(m_jobs.at(index)),
                            QString(),
                            m_jobs.at(index).preparedImagePath);
          emit tasksChanged();
      });
    connect(job.task,
            &OcrTaskWidget::failed,
            this,
            [this, jobId](const QString& error) {
                const int index = jobIndexById(jobId);
                if (index < 0) {
                    return;
                }
                m_jobs[index].error = error;
                m_jobs[index].status = tr("Failed");
                m_jobs[index].failed = true;
                m_jobs[index].task = nullptr;
                updateJobRow(index);
                updateDetails();
                updateButtons();
                emit taskFinished(m_jobs.at(index).requestId,
                                  static_cast<int>(m_jobs.at(index).kind),
                                  false,
                                  QString(),
                                  m_jobs.at(index).error,
                                  m_jobs.at(index).preparedImagePath);
                emit tasksChanged();
            });
    connect(job.task, &OcrTaskWidget::cancelled, this, [this, jobId]() {
        const int index = jobIndexById(jobId);
        if (index < 0) {
            return;
        }
        m_jobs[index].status = tr("Cancelled");
        m_jobs[index].cancelled = true;
        m_jobs[index].task = nullptr;
        updateJobRow(index);
        updateDetails();
        updateButtons();
        emit taskFinished(m_jobs.at(index).requestId,
                          static_cast<int>(m_jobs.at(index).kind),
                          false,
                          QString(),
                          tr("Cancelled"),
                          m_jobs.at(index).preparedImagePath);
        emit tasksChanged();
    });
    connect(job.task, &QObject::destroyed, this, [this, jobId]() {
        const int index = jobIndexById(jobId);
        if (index >= 0) {
            m_jobs[index].task = nullptr;
            emit tasksChanged();
        }
    });

    m_jobs.append(job);
    addJobRow(m_jobs.last());
    m_table->selectRow(m_table->rowCount() - 1);
    job.task->start();
    emit tasksChanged();
}

int OcrJobManagerWidget::runningJobCount() const
{
    int count = 0;
    for (const Job& job : m_jobs) {
        if (job.task && !job.completed && !job.failed && !job.cancelled) {
            ++count;
        }
    }
    return count;
}

void OcrJobManagerWidget::addJobRow(const Job& job)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(QString::number(job.id)));
    m_table->setItem(row, 1, new QTableWidgetItem(jobTypeText(job)));
    m_table->setItem(row, 2, new QTableWidgetItem(jobStatusText(job)));
}

void OcrJobManagerWidget::updateJobRow(int index)
{
    if (index < 0 || index >= m_jobs.size()) {
        return;
    }
    const Job& job = m_jobs.at(index);
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, 0)->text().toInt() != job.id) {
            continue;
        }
        m_table->item(row, 1)->setText(jobTypeText(job));
        m_table->item(row, 2)->setText(jobStatusText(job));
        break;
    }
}

void OcrJobManagerWidget::updateDetails()
{
    const int index = selectedJobIndex();
    if (index < 0) {
        m_imagePreview->setText(tr("No task selected"));
        m_imagePreview->setPixmap({});
        if (m_resultPaneTitle) {
            m_resultPaneTitle->setText(tr("Result"));
        }
        m_resultPreview->clear();
        clearLatexPreview();
        return;
    }

    const Job& job = m_jobs.at(index);
    if (m_resultPaneTitle) {
        m_resultPaneTitle->setText(resultPaneTitle(job));
    }
    if (!job.capture.isNull()) {
        m_imagePreview->setPixmap(job.capture.scaled(m_imagePreview->size(),
                                                     Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation));
    } else {
        m_imagePreview->setText({});
        m_imagePreview->setPixmap({});
    }

    const QString result = jobResultText(job);
    if (!result.isEmpty()) {
        m_resultPreview->setPlainText(result);
    } else if (!job.error.isEmpty()) {
        m_resultPreview->setPlainText(job.error);
    } else {
        m_resultPreview->setPlainText(job.status);
    }

    updateLatexPreview(job);
}

void OcrJobManagerWidget::updateButtons()
{
    const int index = selectedJobIndex();
    const bool hasJob = index >= 0;
    const bool hasResult =
      hasJob && (!jobResultText(m_jobs.at(index)).isEmpty() ||
                 !jobFallbackText(m_jobs.at(index)).isEmpty());
    const bool canCancel = hasJob && m_jobs.at(index).task;
    const bool hasRunningJobs = runningJobCount() > 0;
    bool hasHistory = false;
    for (const Job& job : m_jobs) {
        if (!job.task) {
            hasHistory = true;
            break;
        }
    }
    m_copyButton->setEnabled(hasResult);
    m_openButton->setEnabled(hasResult);
    m_killButton->setEnabled(canCancel);
    m_killAllButton->setEnabled(hasRunningJobs);
    m_clearHistoryButton->setEnabled(hasHistory);
    m_stopOcrWorkerButton->setEnabled(
      OcrTaskWidget::isMarkerOcrServiceRunning());
}

void OcrJobManagerWidget::updateLatexPreview(const Job& job)
{
    const QString result = jobResultText(job);
    if (job.ocr.latex.isEmpty() && result.isEmpty()) {
        clearLatexPreview();
        return;
    }

    m_latexPreview->show();
#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
    if (m_katexDist.isEmpty()) {
        setLatexPreviewMessage(
          tr("Markdown preview with KaTeX requires local KaTeX assets."));
        return;
    }
    m_latexPreview->setHtml(
      job.ocr.latex.isEmpty()
        ? OcrPreviewRenderer::markdownHtml(
            result, OcrPreviewRenderer::Density::Compact)
        : OcrPreviewRenderer::katexHtml(
            job.ocr.latex, OcrPreviewRenderer::Density::Compact),
      QUrl::fromLocalFile(m_katexDist + QDir::separator()));
#else
    setLatexPreviewMessage(
      tr("Markdown preview with KaTeX requires QtWebEngine."));
#endif
}

void OcrJobManagerWidget::clearLatexPreview()
{
    setLatexPreviewMessage(QString());
    m_latexPreview->hide();
}

void OcrJobManagerWidget::setLatexPreviewMessage(const QString& message)
{
#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
    m_latexPreview->setHtml(OcrPreviewRenderer::messageHtml(message, 16));
#else
    m_latexPreview->setText(message);
#endif
}

QString OcrJobManagerWidget::jobResultText(const Job& job) const
{
    const MarkerOcr::Result& ocr = job.ocr;
    if (!ocr.text.isEmpty() && !ocr.latex.isEmpty()) {
        return QStringLiteral("%1\n\nLaTeX:\n%2").arg(ocr.text, ocr.latex);
    }
    if (!ocr.text.isEmpty()) {
        return ocr.text;
    }
    if (!ocr.latex.isEmpty()) {
        return ocr.latex;
    }
    return {};
}

QString OcrJobManagerWidget::jobFallbackText(const Job& job) const
{
    const MarkerOcr::Result& ocr = job.ocr;
    if (!ocr.fallbackText.isEmpty() && !ocr.fallbackLatex.isEmpty()) {
        return QStringLiteral("%1\n\nLaTeX:\n%2")
          .arg(ocr.fallbackText, ocr.fallbackLatex);
    }
    if (!ocr.fallbackText.isEmpty()) {
        return ocr.fallbackText;
    }
    return ocr.fallbackLatex;
}

QString OcrJobManagerWidget::resultPaneTitle(const Job& job) const
{
    const QString title =
      job.kind == OcrTaskWidget::Kind::Text ? tr("Markdown") : tr("Result");
    if (job.ocr.resultInfo.isEmpty()) {
        return title;
    }
    return tr("%1 (%2)").arg(title, job.ocr.resultInfo);
}

int OcrJobManagerWidget::selectedJobIndex() const
{
    const QList<QTableWidgetItem*> selected = m_table->selectedItems();
    if (selected.isEmpty()) {
        return -1;
    }
    const int row = selected.first()->row();
    const int jobId = m_table->item(row, 0)->text().toInt();
    return jobIndexById(jobId);
}

int OcrJobManagerWidget::jobIndexById(int id) const
{
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

QString OcrJobManagerWidget::jobTypeText(const Job& job) const
{
    switch (job.kind) {
        case OcrTaskWidget::Kind::Barcode:
            return tr("Barcode");
        case OcrTaskWidget::Kind::Text:
            return tr("OCR");
    }
    return tr("Task");
}

QString OcrJobManagerWidget::jobStatusText(const Job& job) const
{
    return job.status;
}

void OcrJobManagerWidget::openJobResult(int index)
{
    if (index < 0 || index >= m_jobs.size() ||
        (jobResultText(m_jobs.at(index)).isEmpty() &&
         jobFallbackText(m_jobs.at(index)).isEmpty() &&
         m_jobs.at(index).ocr.extraText.isEmpty() &&
         m_jobs.at(index).ocr.extraLatex.isEmpty())) {
        return;
    }

    const Job& job = m_jobs.at(index);
    auto* result = new OcrResultWidget(job.capture, job.ocr);
    if (job.kind == OcrTaskWidget::Kind::Barcode) {
        result->setWindowTitle(tr("Barcode Result"));
    }
    result->show();
    result->activateWindow();
    result->raise();
}

void OcrJobManagerWidget::copySelectedResult()
{
    const int index = selectedJobIndex();
    if (index >= 0 && !jobResultText(m_jobs.at(index)).isEmpty()) {
        FlameshotDaemon::copyToClipboard(jobResultText(m_jobs.at(index)));
    }
}

void OcrJobManagerWidget::openSelectedResult()
{
    const int index = selectedJobIndex();
    openJobResult(index);
}

void OcrJobManagerWidget::cancelSelectedJob()
{
    const int index = selectedJobIndex();
    if (index >= 0 && m_jobs.at(index).task) {
        m_jobs[index].task->cancelTask();
    }
}

void OcrJobManagerWidget::killAllRunningJobs()
{
    for (Job& job : m_jobs) {
        if (job.task) {
            job.task->cancelTask();
        }
    }
}

void OcrJobManagerWidget::clearHistory()
{
    for (int i = m_jobs.size() - 1; i >= 0; --i) {
        if (m_jobs.at(i).task) {
            continue;
        }
        m_jobs.removeAt(i);
    }

    m_table->setRowCount(0);
    for (const Job& job : m_jobs) {
        addJobRow(job);
    }
    updateDetails();
    updateButtons();
    emit tasksChanged();
}
