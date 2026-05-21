// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QString>

namespace OcrPreviewRenderer {
enum class Density
{
    Normal,
    Compact
};

QString findKatexDist();
QString katexHtml(const QString& latex, Density density = Density::Normal);
QString markdownHtml(const QString& markdown,
                     Density density = Density::Normal);
QString messageHtml(const QString& message, int padding = 24);
}
