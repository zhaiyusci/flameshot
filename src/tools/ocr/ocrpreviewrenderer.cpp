// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "ocrpreviewrenderer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>

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

struct PreviewStyle
{
    int formulaPadding = 24;
    QString formulaFontSize = QStringLiteral("1.35rem");
    QString bodyPadding = QStringLiteral("16px 18px");
    QString bodyFont = QStringLiteral(
      "15px/1.55 system-ui, -apple-system, BlinkMacSystemFont, \"Segoe UI\", "
      "sans-serif");
    QString paragraphMargin = QStringLiteral("0.85rem");
    QString headingMarginTop = QStringLiteral("1.1rem");
    QString headingMarginBottom = QStringLiteral("0.65rem");
    QString listMarginBottom = QStringLiteral("0.85rem");
    QString listMarginLeft = QStringLiteral("1.4rem");
    QString preMarginBottom = QStringLiteral("0.85rem");
    int prePadding = 12;
    QString mathMargin = QStringLiteral("1rem");
};

PreviewStyle previewStyle(OcrPreviewRenderer::Density density)
{
    if (density == OcrPreviewRenderer::Density::Compact) {
        PreviewStyle style;
        style.formulaPadding = 18;
        style.formulaFontSize = QStringLiteral("1.2rem");
        style.bodyPadding = QStringLiteral("14px 16px");
        style.bodyFont = QStringLiteral(
          "14px/1.5 system-ui, -apple-system, BlinkMacSystemFont, \"Segoe UI\", "
          "sans-serif");
        style.paragraphMargin = QStringLiteral("0.75rem");
        style.headingMarginTop = QStringLiteral("1rem");
        style.headingMarginBottom = QStringLiteral("0.55rem");
        style.listMarginBottom = QStringLiteral("0.75rem");
        style.listMarginLeft = QStringLiteral("1.3rem");
        style.preMarginBottom = QStringLiteral("0.75rem");
        style.prePadding = 10;
        style.mathMargin = QStringLiteral("0.85rem");
        return style;
    }
    return {};
}
}

namespace OcrPreviewRenderer {
QString findKatexDist()
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

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        appDir.filePath(QStringLiteral("../share/katex")),
        appDir.filePath(QStringLiteral("../share/katex/dist")),
        appDir.filePath(QStringLiteral("../share/javascript/katex")),
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

QString katexHtml(const QString& latex, Density density)
{
    const PreviewStyle style = previewStyle(density);
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
  padding: __FORMULA_PADDING__px;
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: auto;
}
#preview {
  max-width: 100%;
  width: 100%;
  font-size: __FORMULA_FONT_SIZE__;
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
    html.replace(QStringLiteral("__FORMULA_PADDING__"),
                 QString::number(style.formulaPadding));
    html.replace(QStringLiteral("__FORMULA_FONT_SIZE__"),
                 style.formulaFontSize);
    html.replace(QStringLiteral("__LATEX__"),
                 jsStringLiteral(katexRenderSource(latex)));
    return html;
}

QString markdownHtml(const QString& markdown, Density density)
{
    const PreviewStyle style = previewStyle(density);
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
  padding: __BODY_PADDING__;
  overflow: auto;
  font: __BODY_FONT__;
}
#preview {
  box-sizing: border-box;
  width: 100%;
  min-width: 0;
}
p {
  margin: 0 0 __PARAGRAPH_MARGIN__;
}
h1, h2, h3, h4, h5, h6 {
  line-height: 1.25;
  margin: __HEADING_MARGIN_TOP__ 0 __HEADING_MARGIN_BOTTOM__;
}
h1:first-child, h2:first-child, h3:first-child,
h4:first-child, h5:first-child, h6:first-child {
  margin-top: 0;
}
ul, ol {
  margin: 0 0 __LIST_MARGIN_BOTTOM__ __LIST_MARGIN_LEFT__;
  padding: 0;
}
pre {
  margin: 0 0 __PRE_MARGIN_BOTTOM__;
  padding: __PRE_PADDING__px;
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
  margin: __MATH_MARGIN__ 0;
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
    html.replace(QStringLiteral("__BODY_PADDING__"), style.bodyPadding);
    html.replace(QStringLiteral("__BODY_FONT__"), style.bodyFont);
    html.replace(QStringLiteral("__PARAGRAPH_MARGIN__"),
                 style.paragraphMargin);
    html.replace(QStringLiteral("__HEADING_MARGIN_TOP__"),
                 style.headingMarginTop);
    html.replace(QStringLiteral("__HEADING_MARGIN_BOTTOM__"),
                 style.headingMarginBottom);
    html.replace(QStringLiteral("__LIST_MARGIN_BOTTOM__"),
                 style.listMarginBottom);
    html.replace(QStringLiteral("__LIST_MARGIN_LEFT__"), style.listMarginLeft);
    html.replace(QStringLiteral("__PRE_MARGIN_BOTTOM__"),
                 style.preMarginBottom);
    html.replace(QStringLiteral("__PRE_PADDING__"),
                 QString::number(style.prePadding));
    html.replace(QStringLiteral("__MATH_MARGIN__"), style.mathMargin);
    html.replace(QStringLiteral("__MARKDOWN__"), jsStringLiteral(markdown));
    return html;
}

QString messageHtml(const QString& message, int padding)
{
    return QStringLiteral("<!doctype html><meta charset=\"utf-8\">"
                          "<body style=\"font:14px sans-serif;"
                          "padding:%1px;color:#555;\">%2</body>")
      .arg(QString::number(padding), message.toHtmlEscaped());
}
}
