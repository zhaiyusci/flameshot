// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

class OcrLatexSyntaxHighlighter : public QSyntaxHighlighter
{
public:
    explicit OcrLatexSyntaxHighlighter(QTextDocument* document);

protected:
    void highlightBlock(const QString& text) override;

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

class OcrMarkdownSyntaxHighlighter : public QSyntaxHighlighter
{
public:
    explicit OcrMarkdownSyntaxHighlighter(QTextDocument* document);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct HighlightRule
    {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    QVector<HighlightRule> m_rules;
};
