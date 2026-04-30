// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "ocrjobmanagerwidget.h"

#include "core/flameshotdaemon.h"
#include "tools/ocr/ocrresultwidget.h"
#include "utils/globalvalues.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSizePolicy>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
#include <QWebEngineView>
#endif

namespace {
QString jsStringLiteral(const QString& value)
{
    QJsonArray array;
    array.append(value);
    const QString json =
      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
    return json.mid(1, json.size() - 2);
}

QString katexRenderSource(QString latex)
{
    latex = latex.trimmed();
    if (latex.startsWith(QStringLiteral("$$")) &&
        latex.endsWith(QStringLiteral("$$")) && latex.size() > 4) {
        return latex.mid(2, latex.size() - 4).trimmed();
    }
    if (latex.startsWith(QStringLiteral("\\[")) &&
        latex.endsWith(QStringLiteral("\\]")) && latex.size() > 4) {
        return latex.mid(2, latex.size() - 4).trimmed();
    }
    if (latex.startsWith(QStringLiteral("\\(")) &&
        latex.endsWith(QStringLiteral("\\)")) && latex.size() > 4) {
        return latex.mid(2, latex.size() - 4).trimmed();
    }
    const QString displayMathBegin = QStringLiteral("\\begin{displaymath}");
    const QString displayMathEnd = QStringLiteral("\\end{displaymath}");
    if (latex.startsWith(displayMathBegin) && latex.endsWith(displayMathEnd) &&
        latex.size() > displayMathBegin.size() + displayMathEnd.size()) {
        return latex
          .mid(displayMathBegin.size(),
               latex.size() - displayMathBegin.size() - displayMathEnd.size())
          .trimmed();
    }
    if (latex.startsWith(QLatin1Char('$')) &&
        latex.endsWith(QLatin1Char('$')) && latex.size() > 2) {
        return latex.mid(1, latex.size() - 2).trimmed();
    }
    return latex;
}

bool isKatexDist(const QString& path)
{
    return QFileInfo::exists(
             QDir(path).filePath(QStringLiteral("katex.min.js"))) &&
           QFileInfo::exists(
             QDir(path).filePath(QStringLiteral("katex.min.css")));
}

QString resolveKatexDist(QString path)
{
    path = path.trimmed();
    if (path.isEmpty()) {
        return {};
    }
    if (isKatexDist(path)) {
        return path;
    }
    const QString distPath = QDir(path).filePath(QStringLiteral("dist"));
    return isKatexDist(distPath) ? distPath : QString();
}

QString nodeResolvedKatexDist()
{
    const QString node = QStandardPaths::findExecutable(QStringLiteral("node"));
    if (node.isEmpty()) {
        return {};
    }

    QProcess process;
    process.start(node,
                  { QStringLiteral("-e"),
                    QStringLiteral("const path=require('path');"
                                   "console.log(path.join(path.dirname("
                                   "require.resolve('katex/package.json')),"
                                   "'dist'));") });
    if (!process.waitForStarted() || !process.waitForFinished(3000) ||
        process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

QString npmGlobalKatexDist()
{
    const QString npm = QStandardPaths::findExecutable(QStringLiteral("npm"));
    if (npm.isEmpty()) {
        return {};
    }

    QProcess process;
    process.start(npm, { QStringLiteral("root"), QStringLiteral("-g") });
    if (!process.waitForStarted() || !process.waitForFinished(3000) ||
        process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        return {};
    }

    return QDir(QString::fromUtf8(process.readAllStandardOutput()).trimmed())
      .filePath(QStringLiteral("katex/dist"));
}

QWidget* labeledPane(const QString& title, QWidget* content, QWidget* parent)
{
    auto* pane = new QWidget(parent);
    pane->setMinimumSize(220, 160);
    pane->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* titleLabel = new QLabel(title, pane);
    titleLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));

    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(titleLabel);
    layout->addWidget(content, 1);
    return pane;
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
  , m_katexDist(findKatexDist())
{
    setWindowIcon(QIcon(GlobalValues::iconPath()));
    setWindowTitle(tr("OCR Jobs"));
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
    sourcePreviewSplitter->addWidget(labeledPane(tr("Markdown"), m_resultPreview, this));
    sourcePreviewSplitter->addWidget(labeledPane(tr("Preview"), m_latexPreview, this));
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

    connect(m_table,
            &QTableWidget::itemSelectionChanged,
            this,
            [this]() {
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
    connect(m_stopOcrWorkerButton,
            &QPushButton::clicked,
            this,
            [this]() {
                OcrTaskWidget::stopPaddleOcrService();
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
    connect(job.task, &OcrTaskWidget::statusChanged, this, [this, jobId](const QString& status) {
        const int index = jobIndexById(jobId);
        if (index < 0) {
            return;
        }
        m_jobs[index].status = status;
        updateJobRow(index);
        updateDetails();
        emit tasksChanged();
    });
    connect(job.task,
            &OcrTaskWidget::ocrCompleted,
            this,
            [this, jobId](const QPixmap& capture,
                          const QString& text,
                          const QString& latex) {
                const int index = jobIndexById(jobId);
                if (index < 0) {
                    return;
                }
                m_jobs[index].capture = capture;
                m_jobs[index].text = text;
                m_jobs[index].latex = latex;
                m_jobs[index].result = jobResultText(m_jobs.at(index));
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
    connect(job.task, &OcrTaskWidget::failed, this, [this, jobId](const QString& error) {
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
        m_imagePreview->setText(tr("No OCR job selected"));
        m_imagePreview->setPixmap({});
        m_resultPreview->clear();
        clearLatexPreview();
        return;
    }

    const Job& job = m_jobs.at(index);
    if (!job.capture.isNull()) {
        m_imagePreview->setPixmap(job.capture.scaled(
          m_imagePreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
    const bool hasResult = hasJob && !jobResultText(m_jobs.at(index)).isEmpty();
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
    m_stopOcrWorkerButton->setEnabled(OcrTaskWidget::isPaddleOcrServiceRunning());
}

void OcrJobManagerWidget::updateLatexPreview(const Job& job)
{
    const QString result = jobResultText(job);
    if (job.latex.isEmpty() && result.isEmpty()) {
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
    m_latexPreview->setHtml(job.latex.isEmpty() ? markdownHtml(result)
                                                : katexHtml(job.latex),
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
    m_latexPreview->setHtml(QStringLiteral("<!doctype html><meta charset=\"utf-8\">"
                                           "<body style=\"font:14px sans-serif;"
                                           "padding:16px;color:#555;\">%1</body>")
                              .arg(message.toHtmlEscaped()));
#else
    m_latexPreview->setText(message);
#endif
}

QString OcrJobManagerWidget::katexHtml(const QString& latex) const
{
    QString html = QStringLiteral(R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<link rel="stylesheet" href="katex.min.css">
<style>
html, body {
  margin: 0;
  min-height: 100%;
  background: #ffffff;
  color: #111111;
}
body {
  box-sizing: border-box;
  padding: 18px;
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: auto;
}
#preview {
  max-width: 100%;
  width: 100%;
  font-size: 1.2rem;
}
.katex-display {
  margin: 0;
}
#error {
  color: #b00020;
  font: 14px sans-serif;
  white-space: pre-wrap;
}
</style>
<script src="katex.min.js"></script>
</head>
<body>
<div id="preview"></div>
<script>
const latex = __LATEX__;
try {
  katex.render(latex, document.getElementById("preview"), {
    displayMode: true,
    throwOnError: false,
    strict: "ignore"
  });
} catch (error) {
  const preview = document.getElementById("preview");
  const errorNode = document.createElement("div");
  errorNode.id = "error";
  errorNode.textContent = error.message;
  preview.replaceChildren(errorNode);
}
</script>
</body>
</html>
)HTML");
    html.replace(QStringLiteral("__LATEX__"),
                 jsStringLiteral(katexRenderSource(latex)));
    return html;
}

QString OcrJobManagerWidget::markdownHtml(const QString& markdown) const
{
    QString html = QStringLiteral(R"HTML(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<link rel="stylesheet" href="katex.min.css">
<style>
html, body {
  margin: 0;
  min-height: 100%;
  width: 100%;
  background: #ffffff;
  color: #111111;
}
body {
  box-sizing: border-box;
  padding: 14px 16px;
  overflow: auto;
  font: 14px/1.5 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI",
    sans-serif;
}
#preview {
  box-sizing: border-box;
  width: 100%;
  min-width: 0;
}
p {
  margin: 0 0 0.75rem;
}
h1, h2, h3, h4, h5, h6 {
  line-height: 1.25;
  margin: 1rem 0 0.55rem;
}
h1:first-child, h2:first-child, h3:first-child,
h4:first-child, h5:first-child, h6:first-child {
  margin-top: 0;
}
ul, ol {
  margin: 0 0 0.75rem 1.3rem;
  padding: 0;
}
pre {
  margin: 0 0 0.75rem;
  padding: 10px;
  overflow: auto;
  background: #f6f8fa;
  border: 1px solid #d0d7de;
  border-radius: 6px;
}
code {
  font-family: ui-monospace, SFMono-Regular, Consolas, "Liberation Mono",
    monospace;
  font-size: 0.92em;
}
.math-display {
  margin: 0.85rem 0;
  overflow-x: auto;
  overflow-y: hidden;
  width: 100%;
  text-align: center;
}
.math-inline {
  display: inline-block;
}
.katex-display {
  margin: 0;
}
.render-error {
  color: #b00020;
  white-space: pre-wrap;
}
</style>
<script src="katex.min.js"></script>
</head>
<body>
<main id="preview"></main>
<script>
const markdown = __MARKDOWN__;

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, function(ch) {
    return {
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      '"': "&quot;",
      "'": "&#39;"
    }[ch];
  });
}

function mathNode(latex, display) {
  const tag = display ? "div" : "span";
  const cls = display ? "math math-display" : "math math-inline";
  return "<" + tag + " class=\"" + cls + "\" data-display=\"" +
    (display ? "1" : "0") + "\" data-latex=\"" +
    escapeHtml(latex.trim()) + "\"></" + tag + ">";
}

function renderInline(text) {
  let html = "";
  let i = 0;
  while (i < text.length) {
    if (text[i] === "`") {
      const end = text.indexOf("`", i + 1);
      if (end > i) {
        html += "<code>" + escapeHtml(text.slice(i + 1, end)) + "</code>";
        i = end + 1;
        continue;
      }
    }
    if (text.startsWith("\\(", i)) {
      const end = text.indexOf("\\)", i + 2);
      if (end > i) {
        html += mathNode(text.slice(i + 2, end), false);
        i = end + 2;
        continue;
      }
    }
    if (text[i] === "$" && text[i + 1] !== "$") {
      const end = text.indexOf("$", i + 1);
      if (end > i + 1 && text[end + 1] !== "$") {
        html += mathNode(text.slice(i + 1, end), false);
        i = end + 1;
        continue;
      }
    }
    html += escapeHtml(text[i]);
    i += 1;
  }
  return html;
}

function renderBlocks(source) {
  const lines = source.replace(/\r\n?/g, "\n").split("\n");
  let html = "";
  let paragraph = [];
  let listType = "";
  let listItems = [];
  let code = null;

  function flushParagraph() {
    if (paragraph.length === 0) {
      return;
    }
    html += "<p>" + renderInline(paragraph.join(" ")) + "</p>";
    paragraph = [];
  }

  function flushList() {
    if (!listType) {
      return;
    }
    html += "<" + listType + ">";
    for (const item of listItems) {
      html += "<li>" + renderInline(item) + "</li>";
    }
    html += "</" + listType + ">";
    listType = "";
    listItems = [];
  }

  function flushCode() {
    if (code === null) {
      return;
    }
    html += "<pre><code>" + escapeHtml(code.join("\n")) + "</code></pre>";
    code = null;
  }

  for (const rawLine of lines) {
    const line = rawLine.replace(/\s+$/, "");
    if (code !== null) {
      if (/^\s*```/.test(line)) {
        flushCode();
      } else {
        code.push(rawLine);
      }
      continue;
    }
    if (/^\s*```/.test(line)) {
      flushParagraph();
      flushList();
      code = [];
      continue;
    }
    if (/^\s*$/.test(line)) {
      flushParagraph();
      flushList();
      continue;
    }

    let match = line.match(/^\s*(#{1,6})\s+(.+)$/);
    if (match) {
      flushParagraph();
      flushList();
      const level = match[1].length;
      html += "<h" + level + ">" + renderInline(match[2].trim()) +
        "</h" + level + ">";
      continue;
    }

    match = line.match(/^\s*[-*+]\s+(.+)$/);
    if (match) {
      flushParagraph();
      if (listType && listType !== "ul") {
        flushList();
      }
      listType = "ul";
      listItems.push(match[1].trim());
      continue;
    }

    match = line.match(/^\s*\d+\.\s+(.+)$/);
    if (match) {
      flushParagraph();
      if (listType && listType !== "ol") {
        flushList();
      }
      listType = "ol";
      listItems.push(match[1].trim());
      continue;
    }

    flushList();
    paragraph.push(line.trim());
  }

  flushParagraph();
  flushList();
  flushCode();
  return html;
}

function renderMarkdown(source) {
  const parts = [];
  const displayMath = /(\$\$[\s\S]*?\$\$|\\\[[\s\S]*?\\\])/g;
  let index = 0;
  let match = null;
  while ((match = displayMath.exec(source)) !== null) {
    if (match.index > index) {
      parts.push(renderBlocks(source.slice(index, match.index)));
    }
    const token = match[0];
    const latex = token.startsWith("$$")
      ? token.slice(2, -2)
      : token.slice(2, -2);
    parts.push(mathNode(latex, true));
    index = match.index + token.length;
  }
  if (index < source.length) {
    parts.push(renderBlocks(source.slice(index)));
  }
  return parts.join("");
}

const preview = document.getElementById("preview");
preview.innerHTML = renderMarkdown(markdown);
for (const node of preview.querySelectorAll(".math")) {
  try {
    katex.render(node.getAttribute("data-latex") || "", node, {
      displayMode: node.getAttribute("data-display") === "1",
      throwOnError: false,
      strict: "ignore"
    });
  } catch (error) {
    node.className = "render-error";
    node.textContent = error.message;
  }
}
</script>
</body>
</html>
)HTML");
    html.replace(QStringLiteral("__MARKDOWN__"), jsStringLiteral(markdown));
    return html;
}

QString OcrJobManagerWidget::findKatexDist() const
{
    const QString configuredPath =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_KATEX_DIST"))
        .trimmed();
    QString resolvedPath = resolveKatexDist(configuredPath);
    if (!resolvedPath.isEmpty()) {
        return resolvedPath;
    }

    resolvedPath = resolveKatexDist(nodeResolvedKatexDist());
    if (!resolvedPath.isEmpty()) {
        return resolvedPath;
    }

    resolvedPath = resolveKatexDist(npmGlobalKatexDist());
    if (!resolvedPath.isEmpty()) {
        return resolvedPath;
    }

    const QStringList candidates = {
        QStringLiteral("/usr/share/katex"),
        QStringLiteral("/usr/share/javascript/katex"),
        QStringLiteral("/usr/lib/node_modules/katex/dist"),
        QStringLiteral("/usr/local/lib/node_modules/katex/dist"),
        QDir::home().filePath(
          QStringLiteral(".local/lib/node_modules/katex/dist"))
    };
    for (const QString& candidate : candidates) {
        resolvedPath = resolveKatexDist(candidate);
        if (!resolvedPath.isEmpty()) {
            return resolvedPath;
        }
    }

    return {};
}

QString OcrJobManagerWidget::jobResultText(const Job& job) const
{
    if (!job.text.isEmpty() && !job.latex.isEmpty()) {
        return QStringLiteral("%1\n\nLaTeX:\n%2").arg(job.text, job.latex);
    }
    if (!job.text.isEmpty()) {
        return job.text;
    }
    if (!job.latex.isEmpty()) {
        return job.latex;
    }
    return job.result;
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
    Q_UNUSED(job)
    return tr("OCR");
}

QString OcrJobManagerWidget::jobStatusText(const Job& job) const
{
    return job.status;
}

void OcrJobManagerWidget::openJobResult(int index)
{
    if (index < 0 || index >= m_jobs.size() ||
        jobResultText(m_jobs.at(index)).isEmpty()) {
        return;
    }

    const Job& job = m_jobs.at(index);
    auto* result = new OcrResultWidget(job.capture, job.text, job.latex);
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
