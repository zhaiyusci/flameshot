// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#pragma once

#include <QString>

#include <functional>

namespace MarkerOcr {
using Callback = std::function<void(bool,
                                    const QString&,
                                    const QString&,
                                    const QString&,
                                    const QString&,
                                    const QString&,
                                    const QString&,
                                    const QString&,
                                    const QString&,
                                    const QString&,
                                    const QString&)>;

int timeoutMs();
int recognize(const QString& imagePath, Callback callback);
int recognizeFormula(const QString& imagePath, Callback callback);
void cancel(int requestId);
void stop();
bool isRunning();
}
