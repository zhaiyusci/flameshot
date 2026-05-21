// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include "tools/ocr/markerocrservice.h"

#include <QList>
#include <QPixmap>
#include <QWidget>

class QHBoxLayout;
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
                    const MarkerOcr::Result& result,
                    QWidget* parent = nullptr);
    ~OcrResultWidget() override;

private:
    QString combinedResult() const;
    void setupTabbedResultView(const QPixmap& capture,
                               const MarkerOcr::Result& result,
                               QHBoxLayout* buttonLayout,
                               QPushButton* copyButton,
                               QPushButton* closeButton);
    void setupMarkdownResultView(const QPixmap& capture,
                                 const MarkerOcr::Result& result,
                                 QHBoxLayout* buttonLayout,
                                 QPushButton* copyButton,
                                 QPushButton* closeButton);
    void setupLatexResultView(const QPixmap& capture,
                              const MarkerOcr::Result& result,
                              QHBoxLayout* buttonLayout,
                              QPushButton* copyButton,
                              QPushButton* closeButton);
    void addMarkdownResultTab(const QString& title,
                              const QString& body,
                              const QString& pageLatex,
                              bool selectTab = false);
    void startFormulaRouteRequest();
    void finishFormulaRouteRequest(const MarkerOcr::Result& result);
    void connectCopyResultButton(QPushButton* copyButton);
    void schedulePreviewUpdate();
    void updatePreview();
    void setPreviewMessage(const QString& message);

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
