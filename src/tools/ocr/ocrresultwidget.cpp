// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "ocrresultwidget.h"

#include "core/flameshotdaemon.h"
#include "utils/abstractlogger.h"
#include "utils/globalvalues.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSize>
#include <QSizePolicy>
#include <QSplitter>
#include <QStandardPaths>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVector>

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

QWidget* labeledPane(const QString& title, QWidget* content, QWidget* parent)
{
    auto* pane = new QWidget(parent);
    pane->setMinimumWidth(240);
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

class LatexSyntaxHighlighter : public QSyntaxHighlighter
{
public:
    explicit LatexSyntaxHighlighter(QTextDocument* document)
      : QSyntaxHighlighter(document)
    {
        QTextCharFormat commandFormat;
        commandFormat.setForeground(QColor(QStringLiteral("#0969da")));
        commandFormat.setFontWeight(QFont::DemiBold);
        m_rules.push_back(
          { QRegularExpression(QStringLiteral(R"(\\[A-Za-z]+\*?)")),
            commandFormat });
        m_rules.push_back(
          { QRegularExpression(QStringLiteral(R"(\\.)")), commandFormat });

        QTextCharFormat environmentFormat;
        environmentFormat.setForeground(QColor(QStringLiteral("#8250df")));
        environmentFormat.setFontWeight(QFont::DemiBold);
        m_rules.push_back(
          { QRegularExpression(QStringLiteral(R"(\\(begin|end)\s*\{[^}]+\})")),
            environmentFormat });

        QTextCharFormat operatorFormat;
        operatorFormat.setForeground(QColor(QStringLiteral("#cf222e")));
        m_rules.push_back(
          { QRegularExpression(QStringLiteral(R"([{}_^&])")), operatorFormat });
        m_rules.push_back(
          { QRegularExpression(QStringLiteral(R"(\$\$?|\[|\])")),
            operatorFormat });

        QTextCharFormat numberFormat;
        numberFormat.setForeground(QColor(QStringLiteral("#1a7f37")));
        m_rules.push_back(
          { QRegularExpression(QStringLiteral(R"(\b\d+(\.\d+)?\b)")),
            numberFormat });

        m_commentFormat.setForeground(QColor(QStringLiteral("#6e7781")));
        m_commentFormat.setFontItalic(true);
        m_commentExpression = QRegularExpression(QStringLiteral(R"(%.*$)"));
    }

protected:
    void highlightBlock(const QString& text) override
    {
        for (const auto& rule : m_rules) {
            auto matchIterator = rule.pattern.globalMatch(text);
            while (matchIterator.hasNext()) {
                const auto match = matchIterator.next();
                setFormat(
                  match.capturedStart(), match.capturedLength(), rule.format);
            }
        }

        const auto commentMatch = m_commentExpression.match(text);
        if (commentMatch.hasMatch()) {
            setFormat(commentMatch.capturedStart(),
                      text.size() - commentMatch.capturedStart(),
                      m_commentFormat);
        }
    }

private:
    struct HighlightRule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<HighlightRule> m_rules;
    QRegularExpression m_commentExpression;
    QTextCharFormat m_commentFormat;
};

class MarkdownSyntaxHighlighter : public QSyntaxHighlighter
{
public:
    explicit MarkdownSyntaxHighlighter(QTextDocument* document)
      : QSyntaxHighlighter(document)
    {
        QTextCharFormat headingFormat;
        headingFormat.setForeground(QColor(QStringLiteral("#0969da")));
        headingFormat.setFontWeight(QFont::DemiBold);
        m_rules.push_back(
          { QRegularExpression(QStringLiteral(R"(^\s*#{1,6}\s+.*$)")),
            headingFormat });

        QTextCharFormat mathFormat;
        mathFormat.setForeground(QColor(QStringLiteral("#8250df")));
        mathFormat.setFontWeight(QFont::DemiBold);
        m_rules.push_back(
          { QRegularExpression(QStringLiteral(R"(\$\$?.*?\$\$?)")),
            mathFormat });
        m_rules.push_back(
          { QRegularExpression(QStringLiteral(R"(\\\(|\\\)|\\\[|\\\])")),
            mathFormat });
        m_rules.push_back(
          { QRegularExpression(QStringLiteral(R"(\\[A-Za-z]+\*?)")),
            mathFormat });

        QTextCharFormat codeFormat;
        codeFormat.setForeground(QColor(QStringLiteral("#1a7f37")));
        m_rules.push_back(
          { QRegularExpression(QStringLiteral(R"(`[^`]*`)")), codeFormat });

        QTextCharFormat markerFormat;
        markerFormat.setForeground(QColor(QStringLiteral("#cf222e")));
        m_rules.push_back(
          { QRegularExpression(QStringLiteral(R"(^\s*([-*+]|\d+\.)\s+)")),
            markerFormat });
        m_rules.push_back(
          { QRegularExpression(QStringLiteral(R"((\*\*|__)[^*_]+(\*\*|__))")),
            markerFormat });
    }

protected:
    void highlightBlock(const QString& text) override
    {
        for (const auto& rule : m_rules) {
            auto matchIterator = rule.pattern.globalMatch(text);
            while (matchIterator.hasNext()) {
                const auto match = matchIterator.next();
                setFormat(
                  match.capturedStart(), match.capturedLength(), rule.format);
            }
        }
    }

private:
    struct HighlightRule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<HighlightRule> m_rules;
};

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
}

OcrResultWidget::OcrResultWidget(const QString& text, QWidget* parent)
  : OcrResultWidget(QPixmap(), text, QString(), parent)
{}

OcrResultWidget::OcrResultWidget(const QPixmap& capture,
                                 const QString& text,
                                 const QString& latex,
                                 QWidget* parent)
  : QWidget(parent)
  , m_editor(new QPlainTextEdit(this))
  , m_katexDist(findKatexDist())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowIcon(QIcon(GlobalValues::iconPath()));
    setWindowTitle(tr("OCR Result"));

    m_editor->setPlainText(text);
    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    auto* copyButton = new QPushButton(tr("Copy Result"), this);
    auto* closeButton = new QPushButton(tr("Close"), this);

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    buttonLayout->addWidget(copyButton);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(120);

    m_preview = new
#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
      QWebEngineView
#else
      QLabel
#endif
      (this);
    m_preview->setMinimumSize(QSize(240, 240));
    m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
#if !defined(FLAMESHOT_HAVE_QT_WEBENGINE)
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setWordWrap(true);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
#endif

    connect(m_previewTimer, &QTimer::timeout, this, [this]() {
        updatePreview();
    });

    if (latex.isEmpty()) {
        resize(capture.isNull() ? QSize(920, 540) : QSize(1180, 620));

        m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        new MarkdownSyntaxHighlighter(m_editor->document());

        auto* splitter = new QSplitter(Qt::Horizontal, this);
        if (!capture.isNull()) {
            auto* imageLabel = new QLabel(this);
            imageLabel->setAlignment(Qt::AlignCenter);
            imageLabel->setPixmap(capture);

            auto* imageScroll = new QScrollArea(this);
            imageScroll->setWidget(imageLabel);
            imageScroll->setWidgetResizable(true);
            imageScroll->setMinimumWidth(240);
            imageScroll->setSizePolicy(QSizePolicy::Expanding,
                                       QSizePolicy::Expanding);
            splitter->addWidget(labeledPane(tr("Original"), imageScroll, this));
        }
        splitter->addWidget(labeledPane(tr("Markdown"), m_editor, this));
        splitter->addWidget(labeledPane(tr("Preview"), m_preview, this));
        splitter->setChildrenCollapsible(false);
        for (int i = 0; i < splitter->count(); ++i) {
            splitter->setStretchFactor(i, 1);
        }
        splitter->setSizes(capture.isNull() ? QList<int>{ 440, 440 }
                                            : QList<int>{ 360, 360, 360 });
        QTimer::singleShot(0, splitter, [splitter, hasCapture = !capture.isNull()]() {
            splitter->setSizes(hasCapture ? QList<int>{ 1, 1, 1 }
                                          : QList<int>{ 1, 1 });
        });

        auto* layout = new QVBoxLayout(this);
        layout->addWidget(splitter);
        buttonLayout->addWidget(closeButton);
        layout->addLayout(buttonLayout);

        connect(m_editor, &QPlainTextEdit::textChanged, this, [this]() {
            schedulePreviewUpdate();
        });
        connect(copyButton, &QPushButton::clicked, this, [this]() {
            FlameshotDaemon::copyToClipboard(combinedResult());
            AbstractLogger::info(AbstractLogger::Stderr)
              << tr("OCR result copied to clipboard.");
        });
        connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

        m_editor->setFocus();
        m_editor->selectAll();
        updatePreview();
        return;
    }

    resize(1180, 620);

    auto* imageLabel = new QLabel(this);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setPixmap(capture);

    auto* imageScroll = new QScrollArea(this);
    imageScroll->setWidget(imageLabel);
    imageScroll->setWidgetResizable(true);
    imageScroll->setMinimumWidth(240);
    imageScroll->setSizePolicy(QSizePolicy::Expanding,
                               QSizePolicy::Expanding);

    m_latexEditor = new QPlainTextEdit(this);
    m_latexEditor->setPlainText(latex);
    m_latexEditor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_latexEditor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new LatexSyntaxHighlighter(m_latexEditor->document());

    QWidget* sourcePane = nullptr;
    if (text.isEmpty()) {
        sourcePane = labeledPane(tr("LaTeX"), m_latexEditor, this);
    } else {
        auto* sourceSplitter = new QSplitter(Qt::Vertical, this);
        sourceSplitter->addWidget(labeledPane(tr("Text"), m_editor, this));
        sourceSplitter->addWidget(labeledPane(tr("LaTeX"), m_latexEditor, this));
        sourceSplitter->setChildrenCollapsible(false);
        sourceSplitter->setStretchFactor(0, 1);
        sourceSplitter->setStretchFactor(1, 1);
        sourcePane = labeledPane(tr("Recognized"), sourceSplitter, this);
    }

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(labeledPane(tr("Original"), imageScroll, this));
    splitter->addWidget(sourcePane);
    splitter->addWidget(labeledPane(tr("Preview"), m_preview, this));
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 1);
    splitter->setSizes({ 360, 360, 360 });
    QTimer::singleShot(0, splitter, [splitter]() {
        splitter->setSizes({ 1, 1, 1 });
    });

    auto* copyLatexButton = new QPushButton(tr("Copy LaTeX"), this);
    buttonLayout->addWidget(copyLatexButton);
    buttonLayout->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(splitter);
    layout->addLayout(buttonLayout);

    connect(m_latexEditor, &QPlainTextEdit::textChanged, this, [this]() {
        schedulePreviewUpdate();
    });
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        FlameshotDaemon::copyToClipboard(combinedResult());
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("OCR result copied to clipboard.");
    });
    connect(copyLatexButton, &QPushButton::clicked, this, [this]() {
        FlameshotDaemon::copyToClipboard(m_latexEditor->toPlainText());
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("LaTeX copied to clipboard.");
    });
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    m_latexEditor->setFocus();
    m_latexEditor->selectAll();
    updatePreview();
}

QString OcrResultWidget::combinedResult() const
{
    const QString text = m_editor ? m_editor->toPlainText().trimmed() : QString();
    const QString latex =
      m_latexEditor ? m_latexEditor->toPlainText().trimmed() : QString();
    if (!text.isEmpty() && !latex.isEmpty()) {
        return QStringLiteral("%1\n\nLaTeX:\n%2").arg(text, latex);
    }
    if (!text.isEmpty()) {
        return text;
    }
    return latex;
}

void OcrResultWidget::schedulePreviewUpdate()
{
    if (m_previewTimer) {
        m_previewTimer->start();
    }
}

void OcrResultWidget::updatePreview()
{
    if (!m_preview) {
        return;
    }

    if (!m_latexEditor) {
        const QString markdown =
          m_editor ? m_editor->toPlainText().trimmed() : QString();
        if (markdown.isEmpty()) {
            setPreviewMessage(tr("No OCR result to preview."));
            return;
        }
#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
        if (m_katexDist.isEmpty()) {
            setPreviewMessage(tr("Markdown preview with KaTeX requires local "
                                 "KaTeX assets. Install the katex npm package "
                                 "or set FLAMESHOT_KATEX_DIST to the KaTeX "
                                 "package or dist directory."));
            return;
        }
        m_preview->setHtml(markdownHtml(markdown),
                           QUrl::fromLocalFile(m_katexDist + QDir::separator()));
#else
        setPreviewMessage(tr("Markdown preview with KaTeX requires QtWebEngine. "
                             "Install the qt6-webenginewidgets-devel package "
                             "and rebuild Flameshot."));
#endif
        return;
    }

    const QString latex = m_latexEditor->toPlainText().trimmed();
    if (latex.isEmpty()) {
        setPreviewMessage(tr("No LaTeX to preview."));
        return;
    }

#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
    if (m_katexDist.isEmpty()) {
        setPreviewMessage(tr("KaTeX preview requires local KaTeX assets. "
                             "Install the katex npm package or set "
                             "FLAMESHOT_KATEX_DIST to the KaTeX package or "
                             "dist directory."));
        return;
    }
    m_preview->setHtml(katexHtml(latex),
                       QUrl::fromLocalFile(m_katexDist + QDir::separator()));
#else
    Q_UNUSED(latex)
    setPreviewMessage(tr("KaTeX preview requires QtWebEngine. Install the "
                         "qt6-webenginewidgets-devel package and rebuild "
                         "Flameshot."));
#endif
}

void OcrResultWidget::setPreviewMessage(const QString& message)
{
#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
    m_preview->setHtml(QStringLiteral("<!doctype html><meta charset=\"utf-8\">"
                                      "<body style=\"font:14px sans-serif;"
                                      "padding:24px;color:#555;\">%1</body>")
                         .arg(message.toHtmlEscaped()));
#else
    m_preview->setText(message);
#endif
}

QString OcrResultWidget::katexHtml(const QString& latex) const
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
  padding: 24px;
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: auto;
}
#preview {
  max-width: 100%;
  width: 100%;
  font-size: 1.35rem;
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

QString OcrResultWidget::markdownHtml(const QString& markdown) const
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
  padding: 16px 18px;
  overflow: auto;
  font: 15px/1.55 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI",
    sans-serif;
}
#preview {
  box-sizing: border-box;
  width: 100%;
  min-width: 0;
}
p {
  margin: 0 0 0.85rem;
}
h1, h2, h3, h4, h5, h6 {
  line-height: 1.25;
  margin: 1.1rem 0 0.65rem;
}
h1:first-child, h2:first-child, h3:first-child,
h4:first-child, h5:first-child, h6:first-child {
  margin-top: 0;
}
ul, ol {
  margin: 0 0 0.85rem 1.4rem;
  padding: 0;
}
pre {
  margin: 0 0 0.85rem;
  padding: 12px;
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
  margin: 1rem 0;
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

QString OcrResultWidget::findKatexDist() const
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
