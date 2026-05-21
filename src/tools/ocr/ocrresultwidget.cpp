// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "ocrresultwidget.h"

#include "core/flameshotdaemon.h"
#include "tools/ocr/ocrpreviewrenderer.h"
#include "tools/ocr/ocrsyntaxhighlighter.h"
#include "tools/ocr/ocrtaskwidget.h"
#include "utils/abstractlogger.h"
#include "utils/globalvalues.h"

#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSize>
#include <QSizePolicy>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
#include <QWebEngineView>
#endif

namespace {
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

QWidget* originalImagePane(const QPixmap& capture, QWidget* parent)
{
    auto* imageLabel = new QLabel(parent);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setPixmap(capture);

    auto* imageScroll = new QScrollArea(parent);
    imageScroll->setWidget(imageLabel);
    imageScroll->setWidgetResizable(true);
    imageScroll->setMinimumWidth(240);
    imageScroll->setSizePolicy(QSizePolicy::Expanding,
                               QSizePolicy::Expanding);
    return labeledPane(QObject::tr("Original"), imageScroll, parent);
}

#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
QWebEngineView* previewWidget(QWidget* parent)
#else
QLabel* previewWidget(QWidget* parent)
#endif
{
    auto* preview = new
#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
      QWebEngineView
#else
      QLabel
#endif
      (parent);
    preview->setMinimumSize(QSize(240, 240));
    preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
#if !defined(FLAMESHOT_HAVE_QT_WEBENGINE)
    preview->setAlignment(Qt::AlignCenter);
    preview->setWordWrap(true);
    preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
#endif
    return preview;
}

bool canRunMarkerFormulaRoute(const QPixmap& capture, const QString& sourceInfo)
{
    return !capture.isNull() &&
           sourceInfo.compare(QStringLiteral("Marker Markdown"),
                              Qt::CaseInsensitive) == 0;
}

MarkerOcr::Result plainTextResult(const QString& text)
{
    MarkerOcr::Result result;
    result.ok = !text.isEmpty();
    result.text = text;
    return result;
}
}

OcrResultWidget::OcrResultWidget(const QString& text, QWidget* parent)
  : OcrResultWidget(QPixmap(), plainTextResult(text), parent)
{}

OcrResultWidget::OcrResultWidget(const QPixmap& capture,
                                 const MarkerOcr::Result& result,
                                 QWidget* parent)
  : QWidget(parent)
  , m_editor(new QPlainTextEdit(this))
  , m_katexDist(OcrPreviewRenderer::findKatexDist())
  , m_capture(capture)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowIcon(QIcon(GlobalValues::iconPath()));
    setWindowTitle(tr("OCR Result"));

    m_editor->setPlainText(result.text);
    m_editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);

    auto* copyButton = new QPushButton(tr("Copy Result"), this);
    auto* closeButton = new QPushButton(tr("Close"), this);

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    if (canRunMarkerFormulaRoute(capture, result.resultInfo)) {
        m_formulaRouteButton = new QPushButton(tr("Try Formula Route"), this);
        buttonLayout->addWidget(m_formulaRouteButton);
        connect(m_formulaRouteButton,
                &QPushButton::clicked,
                this,
                &OcrResultWidget::startFormulaRouteRequest);
    }
    buttonLayout->addWidget(copyButton);

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(120);

    m_preview = previewWidget(this);

    connect(
      m_previewTimer, &QTimer::timeout, this, [this]() { updatePreview(); });

    const bool hasAlternatives =
      canRunMarkerFormulaRoute(capture, result.resultInfo) ||
      !result.fallbackText.isEmpty() || !result.fallbackLatex.isEmpty() ||
      !result.extraText.isEmpty() || !result.extraLatex.isEmpty();
    if (hasAlternatives) {
        setupTabbedResultView(
          capture, result, buttonLayout, copyButton, closeButton);
        return;
    }

    if (result.latex.isEmpty()) {
        setupMarkdownResultView(
          capture, result, buttonLayout, copyButton, closeButton);
        return;
    }

    setupLatexResultView(
      capture, result, buttonLayout, copyButton, closeButton);
}

OcrResultWidget::~OcrResultWidget()
{
    m_destroying = true;
    if (m_formulaRouteRequestId != 0) {
        OcrTaskWidget::cancelMarkerOcrRequest(m_formulaRouteRequestId);
        m_formulaRouteRequestId = 0;
    }
}

void OcrResultWidget::setupTabbedResultView(const QPixmap& capture,
                                            const MarkerOcr::Result& result,
                                            QHBoxLayout* buttonLayout,
                                            QPushButton* copyButton,
                                            QPushButton* closeButton)
{
    m_editor->deleteLater();
    m_editor = nullptr;
    m_previewTimer->deleteLater();
    m_previewTimer = nullptr;
    m_preview->deleteLater();
    m_preview = nullptr;

    resize(capture.isNull() ? QSize(1040, 600) : QSize(1220, 640));
    m_resultTabs = new QTabWidget(this);

    addMarkdownResultTab(result.resultInfo.isEmpty() ? tr("Primary")
                                                     : result.resultInfo,
                         result.text,
                         result.latex);
    if (!result.fallbackText.isEmpty() || !result.fallbackLatex.isEmpty()) {
        addMarkdownResultTab(result.fallbackInfo.isEmpty() ? tr("Fallback")
                                                           : result.fallbackInfo,
                             result.fallbackText,
                             result.fallbackLatex);
    }
    if (!result.extraText.isEmpty() || !result.extraLatex.isEmpty()) {
        addMarkdownResultTab(result.extraInfo.isEmpty() ? tr("Text OCR")
                                                        : result.extraInfo,
                             result.extraText,
                             result.extraLatex);
    }

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    if (!capture.isNull()) {
        splitter->addWidget(originalImagePane(capture, this));
    }
    splitter->addWidget(m_resultTabs);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    buttonLayout->addWidget(closeButton);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(splitter);
    layout->addLayout(buttonLayout);

    connectCopyResultButton(copyButton);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    if (!m_tabEditors.isEmpty()) {
        m_tabEditors.first()->setFocus();
        m_tabEditors.first()->selectAll();
    }
}

void OcrResultWidget::setupMarkdownResultView(const QPixmap& capture,
                                              const MarkerOcr::Result& result,
                                              QHBoxLayout* buttonLayout,
                                              QPushButton* copyButton,
                                              QPushButton* closeButton)
{
    resize(capture.isNull() ? QSize(920, 540) : QSize(1180, 620));

    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new OcrMarkdownSyntaxHighlighter(m_editor->document());

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    if (!capture.isNull()) {
        splitter->addWidget(originalImagePane(capture, this));
    }
    const QString markdownTitle =
      result.resultInfo.isEmpty()
        ? tr("Markdown")
        : tr("Markdown (%1)").arg(result.resultInfo);
    splitter->addWidget(labeledPane(markdownTitle, m_editor, this));
    splitter->addWidget(labeledPane(tr("Preview"), m_preview, this));
    splitter->setChildrenCollapsible(false);
    for (int i = 0; i < splitter->count(); ++i) {
        splitter->setStretchFactor(i, 1);
    }
    splitter->setSizes(capture.isNull() ? QList<int>{ 440, 440 }
                                        : QList<int>{ 360, 360, 360 });
    QTimer::singleShot(
      0, splitter, [splitter, hasCapture = !capture.isNull()]() {
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
    connectCopyResultButton(copyButton);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    m_editor->setFocus();
    m_editor->selectAll();
    updatePreview();
}

void OcrResultWidget::setupLatexResultView(const QPixmap& capture,
                                           const MarkerOcr::Result& result,
                                           QHBoxLayout* buttonLayout,
                                           QPushButton* copyButton,
                                           QPushButton* closeButton)
{
    resize(1180, 620);

    m_latexEditor = new QPlainTextEdit(this);
    m_latexEditor->setPlainText(result.latex);
    m_latexEditor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_latexEditor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new OcrLatexSyntaxHighlighter(m_latexEditor->document());

    QWidget* sourcePane = nullptr;
    if (result.text.isEmpty()) {
        sourcePane = labeledPane(tr("LaTeX"), m_latexEditor, this);
    } else {
        auto* sourceSplitter = new QSplitter(Qt::Vertical, this);
        sourceSplitter->addWidget(labeledPane(tr("Text"), m_editor, this));
        sourceSplitter->addWidget(
          labeledPane(tr("LaTeX"), m_latexEditor, this));
        sourceSplitter->setChildrenCollapsible(false);
        sourceSplitter->setStretchFactor(0, 1);
        sourceSplitter->setStretchFactor(1, 1);
        sourcePane = labeledPane(tr("Recognized"), sourceSplitter, this);
    }

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(originalImagePane(capture, this));
    splitter->addWidget(sourcePane);
    splitter->addWidget(labeledPane(tr("Preview"), m_preview, this));
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 1);
    splitter->setSizes({ 360, 360, 360 });
    QTimer::singleShot(
      0, splitter, [splitter]() { splitter->setSizes({ 1, 1, 1 }); });

    auto* copyLatexButton = new QPushButton(tr("Copy LaTeX"), this);
    buttonLayout->addWidget(copyLatexButton);
    buttonLayout->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(splitter);
    layout->addLayout(buttonLayout);

    connect(m_latexEditor, &QPlainTextEdit::textChanged, this, [this]() {
        schedulePreviewUpdate();
    });
    connectCopyResultButton(copyButton);
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

void OcrResultWidget::addMarkdownResultTab(const QString& title,
                                           const QString& body,
                                           const QString& pageLatex,
                                           bool selectTab)
{
    if (!m_resultTabs) {
        return;
    }

    auto pageText = [](const QString& text, const QString& latex) {
        if (!text.isEmpty() && !latex.isEmpty()) {
            return QStringLiteral("%1\n\nLaTeX:\n%2").arg(text, latex);
        }
        if (!text.isEmpty()) {
            return text;
        }
        return latex;
    };

    auto* page = new QWidget(this);
    auto* editor = new QPlainTextEdit(page);
    editor->setPlainText(pageText(body, pageLatex));
    editor->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new OcrMarkdownSyntaxHighlighter(editor->document());

    auto* preview = previewWidget(page);

    auto* timer = new QTimer(page);
    timer->setSingleShot(true);
    timer->setInterval(120);
    auto updatePagePreview = [this, editor, preview]() {
        const QString markdown = editor->toPlainText().trimmed();
#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
        if (markdown.isEmpty()) {
            preview->setHtml(QStringLiteral(
              "<!doctype html><meta charset=\"utf-8\">"
              "<body style=\"font:14px sans-serif;padding:16px;"
              "color:#555;\">%1</body>")
                               .arg(tr("No OCR result to preview.")
                                      .toHtmlEscaped()));
            return;
        }
        if (m_katexDist.isEmpty()) {
            preview->setHtml(QStringLiteral(
              "<!doctype html><meta charset=\"utf-8\">"
              "<body style=\"font:14px sans-serif;padding:16px;"
              "color:#555;\">%1</body>")
                               .arg(tr("Markdown preview with KaTeX requires "
                                       "local KaTeX assets.")
                                      .toHtmlEscaped()));
            return;
        }
        preview->setHtml(
          OcrPreviewRenderer::markdownHtml(markdown),
          QUrl::fromLocalFile(m_katexDist + QDir::separator()));
#else
        preview->setText(markdown.isEmpty() ? tr("No OCR result to preview.")
                                            : markdown);
#endif
    };
    connect(timer, &QTimer::timeout, page, updatePagePreview);
    connect(editor, &QPlainTextEdit::textChanged, page, [timer]() {
        timer->start();
    });

    auto* splitter = new QSplitter(Qt::Horizontal, page);
    splitter->addWidget(labeledPane(tr("Source"), editor, page));
    splitter->addWidget(labeledPane(tr("Preview"), preview, page));
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({ 1, 1 });

    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->addWidget(splitter);

    m_tabEditors << editor;
    m_tabLatexEditors << nullptr;
    m_resultTabs->addTab(page, title.isEmpty() ? tr("OCR") : title);
    updatePagePreview();
    if (selectTab) {
        m_resultTabs->setCurrentWidget(page);
        editor->setFocus();
        editor->selectAll();
    }
}

void OcrResultWidget::startFormulaRouteRequest()
{
    if (m_formulaRouteRequestId != 0 || !m_formulaRouteButton) {
        return;
    }

    m_formulaRouteButton->setEnabled(false);
    m_formulaRouteButton->setText(tr("Running Formula Route..."));
    QPointer<OcrResultWidget> guard(this);
    m_formulaRouteRequestId = OcrTaskWidget::requestMarkerFormulaOcr(
      m_capture,
      [guard](const MarkerOcr::Result& result) {
          if (guard) {
              guard->finishFormulaRouteRequest(result);
          }
      });
}

void OcrResultWidget::finishFormulaRouteRequest(
  const MarkerOcr::Result& result)
{
    if (m_destroying) {
        return;
    }

    m_formulaRouteRequestId = 0;
    if (m_formulaRouteButton) {
        m_formulaRouteButton->setText(tr("Try Formula Route"));
        m_formulaRouteButton->setEnabled(true);
    }

    const QString title =
      result.resultInfo.isEmpty() ? tr("Marker Formula") : result.resultInfo;
    if (result.ok &&
        (!result.text.trimmed().isEmpty() ||
         !result.latex.trimmed().isEmpty())) {
        addMarkdownResultTab(
          title, result.text.trimmed(), result.latex.trimmed(), true);
        return;
    }

    const QString message = result.error.isEmpty()
                              ? tr("Marker formula route produced no result.")
                              : result.error;
    addMarkdownResultTab(tr("Formula Route Failed"), message, QString(), true);
}

void OcrResultWidget::connectCopyResultButton(QPushButton* copyButton)
{
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        FlameshotDaemon::copyToClipboard(combinedResult());
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("OCR result copied to clipboard.");
    });
}

QString OcrResultWidget::combinedResult() const
{
    if (m_resultTabs) {
        const int index = m_resultTabs->currentIndex();
        const QString text = m_tabEditors.value(index)
                               ? m_tabEditors.value(index)->toPlainText().trimmed()
                               : QString();
        const QString latex =
          m_tabLatexEditors.value(index)
            ? m_tabLatexEditors.value(index)->toPlainText().trimmed()
            : QString();
        if (!text.isEmpty() && !latex.isEmpty()) {
            return QStringLiteral("%1\n\nLaTeX:\n%2").arg(text, latex);
        }
        if (!text.isEmpty()) {
            return text;
        }
        return latex;
    }

    const QString text =
      m_editor ? m_editor->toPlainText().trimmed() : QString();
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
        m_preview->setHtml(
          OcrPreviewRenderer::markdownHtml(markdown),
          QUrl::fromLocalFile(m_katexDist + QDir::separator()));
#else
        setPreviewMessage(
          tr("Markdown preview with KaTeX requires QtWebEngine. "
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
    m_preview->setHtml(OcrPreviewRenderer::katexHtml(latex),
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
    m_preview->setHtml(OcrPreviewRenderer::messageHtml(message, 24));
#else
    m_preview->setText(message);
#endif
}
