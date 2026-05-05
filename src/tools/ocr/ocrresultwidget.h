// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QList>
#include <QPixmap>
#include <QWidget>

class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTimer;

#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
class QWebEngineView;
#else
class QLabel;
#endif

class OcrResultWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OcrResultWidget(const QString& text, QWidget* parent = nullptr);
    OcrResultWidget(const QPixmap& capture,
                    const QString& text,
                    const QString& latex,
                    const QString& sourceInfo = QString(),
                    const QString& fallbackText = QString(),
                    const QString& fallbackLatex = QString(),
                    const QString& fallbackInfo = QString(),
                    const QString& extraText = QString(),
                    const QString& extraLatex = QString(),
                    const QString& extraInfo = QString(),
                    QWidget* parent = nullptr);
    ~OcrResultWidget() override;

private:
    QString combinedResult() const;
    void addMarkdownResultTab(const QString& title,
                              const QString& body,
                              const QString& pageLatex,
                              bool selectTab = false);
    void startFormulaRouteRequest();
    void finishFormulaRouteRequest(bool ok,
                                   const QString& text,
                                   const QString& latex,
                                   const QString& info,
                                   const QString& error);
    void schedulePreviewUpdate();
    void updatePreview();
    void setPreviewMessage(const QString& message);
    QString katexHtml(const QString& latex) const;
    QString markdownHtml(const QString& markdown) const;
    QString findKatexDist() const;

    QPlainTextEdit* m_editor;
    QPlainTextEdit* m_latexEditor = nullptr;
    QTabWidget* m_resultTabs = nullptr;
    QPushButton* m_formulaRouteButton = nullptr;
    QList<QPlainTextEdit*> m_tabEditors;
    QList<QPlainTextEdit*> m_tabLatexEditors;
    QTimer* m_previewTimer = nullptr;
    QString m_katexDist;
    QPixmap m_capture;
    int m_formulaRouteRequestId = 0;
    bool m_destroying = false;
#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
    QWebEngineView* m_preview = nullptr;
#else
    QLabel* m_preview = nullptr;
#endif
};
