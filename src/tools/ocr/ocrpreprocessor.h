// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QImage>

namespace OcrPreprocessor {
QImage preparedTextOcrImage(const QImage& image);
QImage preparedLatexOcrImage(const QImage& image);
}
