// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QPixmap>
#include <QWidget>

class QPlainTextEdit;
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
                    QWidget* parent = nullptr);

private:
    QString combinedResult() const;
    void schedulePreviewUpdate();
    void updatePreview();
    void setPreviewMessage(const QString& message);
    QString katexHtml(const QString& latex) const;
    QString markdownHtml(const QString& markdown) const;
    QString findKatexDist() const;

    QPlainTextEdit* m_editor;
    QPlainTextEdit* m_latexEditor = nullptr;
    QTimer* m_previewTimer = nullptr;
    QString m_katexDist;
#if defined(FLAMESHOT_HAVE_QT_WEBENGINE)
    QWebEngineView* m_preview = nullptr;
#else
    QLabel* m_preview = nullptr;
#endif
};
