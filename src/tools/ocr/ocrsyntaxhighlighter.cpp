// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "ocrsyntaxhighlighter.h"

OcrLatexSyntaxHighlighter::OcrLatexSyntaxHighlighter(QTextDocument* document)
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

void OcrLatexSyntaxHighlighter::highlightBlock(const QString& text)
{
    for (const auto& rule : m_rules) {
        auto matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            const auto match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    const auto commentMatch = m_commentExpression.match(text);
    if (commentMatch.hasMatch()) {
        setFormat(commentMatch.capturedStart(),
                  text.size() - commentMatch.capturedStart(),
                  m_commentFormat);
    }
}

OcrMarkdownSyntaxHighlighter::OcrMarkdownSyntaxHighlighter(
  QTextDocument* document)
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
      { QRegularExpression(QStringLiteral(R"(\$\$?.*?\$\$?)")), mathFormat });
    m_rules.push_back(
      { QRegularExpression(QStringLiteral(R"(\\\(|\\\)|\\\[|\\\])")),
        mathFormat });
    m_rules.push_back(
      { QRegularExpression(QStringLiteral(R"(\\[A-Za-z]+\*?)")), mathFormat });

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

void OcrMarkdownSyntaxHighlighter::highlightBlock(const QString& text)
{
    for (const auto& rule : m_rules) {
        auto matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            const auto match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}
