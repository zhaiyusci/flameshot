// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QImage>
#include <QList>
#include <QString>

namespace BarcodeReader {

struct Result
{
    QString format;
    QString text;
    int orientation = 0;
    bool inverted = false;
    bool mirrored = false;
};

struct ScanResult
{
    QList<Result> results;
    QString error;
};

ScanResult scanImage(const QImage& image);
QString formatResults(const QList<Result>& results);

}
