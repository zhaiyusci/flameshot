// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QString>

#include <functional>

namespace MarkerOcr {
struct Result
{
    bool ok = false;
    QString text;
    QString latex;
    QString fallbackText;
    QString fallbackLatex;
    QString resultInfo;
    QString fallbackInfo;
    QString extraText;
    QString extraLatex;
    QString extraInfo;
    QString error;
};

using Callback = std::function<void(const Result&)>;

int timeoutMs();
int recognize(const QString& imagePath, Callback callback);
int recognizeFormula(const QString& imagePath, Callback callback);
void cancel(int requestId);
void stop();
bool isRunning();
}
