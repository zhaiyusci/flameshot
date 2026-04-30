// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "ocrtaskwidget.h"

#include "tools/ocr/ocrpreprocessor.h"
#include "utils/abstractlogger.h"

#include <algorithm>
#include <array>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPointer>
#include <QProcessEnvironment>
#include <QSize>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QVector>

#include <functional>

namespace {
QString configuredLatexOcrCommandSpec()
{
    return QProcessEnvironment::systemEnvironment()
      .value(QStringLiteral("FLAMESHOT_LATEX_OCR_COMMAND"))
      .trimmed();
}

QString configuredLatexOcrBackend()
{
    QString backend =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_LATEX_OCR_BACKEND"),
               QStringLiteral("auto"))
        .trimmed()
        .toLower();
    backend.replace(QLatin1Char('-'), QLatin1Char('_'));
    return backend.isEmpty() ? QStringLiteral("auto") : backend;
}

int latexOcrTimeoutMs()
{
    bool ok = false;
    const int timeout =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_LATEX_OCR_TIMEOUT_MS"))
        .toInt(&ok);
    return ok && timeout > 0 ? timeout : 120000;
}

int textOcrTimeoutMs()
{
    bool ok = false;
    const int timeout =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_OCR_TIMEOUT_MS"))
        .toInt(&ok);
    return ok && timeout > 0 ? timeout : 30000;
}

int textellerIdleTimeoutMs()
{
    bool ok = false;
    const int timeout =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_TEXTELLER_IDLE_TIMEOUT_MS"))
        .toInt(&ok);
    return ok && timeout > 0 ? timeout : 30 * 60 * 1000;
}

bool textellerServiceEnabled()
{
    const QString enabled =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_TEXTELLER_SERVICE"),
               QStringLiteral("1"))
        .trimmed()
        .toLower();
    return enabled != QStringLiteral("0") && enabled != QStringLiteral("false") &&
           enabled != QStringLiteral("no") && enabled != QStringLiteral("off");
}

int paddleOcrTimeoutMs()
{
    bool ok = false;
    const int timeout =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_PADDLEOCR_TIMEOUT_MS"))
        .toInt(&ok);
    return ok && timeout > 0 ? timeout : 300000;
}

int paddleOcrIdleTimeoutMs()
{
    bool ok = false;
    const int timeout =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_PADDLEOCR_IDLE_TIMEOUT_MS"))
        .toInt(&ok);
    return ok && timeout > 0 ? timeout : 30 * 60 * 1000;
}

int latexOcrPadding()
{
    bool ok = false;
    const int padding = QProcessEnvironment::systemEnvironment()
                          .value(QStringLiteral("FLAMESHOT_LATEX_OCR_PADDING"))
                          .toInt(&ok);
    return ok && padding >= 0 ? padding : 24;
}

qreal latexOcrScale()
{
    bool ok = false;
    const qreal scale = QProcessEnvironment::systemEnvironment()
                          .value(QStringLiteral("FLAMESHOT_LATEX_OCR_SCALE"))
                          .toDouble(&ok);
    return ok && scale > 0 ? scale : 2.0;
}

QString latexOcrInvertMode()
{
    QString mode =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_LATEX_OCR_INVERT"),
               QStringLiteral("auto"))
        .trimmed()
        .toLower();
    return mode.isEmpty() ? QStringLiteral("auto") : mode;
}

QString latexOcrPreprocessMode()
{
    QString mode =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_LATEX_OCR_PREPROCESS"),
               QStringLiteral("normalize"))
        .trimmed()
        .toLower();
    mode.replace(QLatin1Char('-'), QLatin1Char('_'));
    return mode.isEmpty() ? QStringLiteral("normalize") : mode;
}

QString ocrPageSegMode()
{
    const QString configuredPageSegMode =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_OCR_PSM"))
        .trimmed();
    return configuredPageSegMode.isEmpty() ? QStringLiteral("6")
                                           : configuredPageSegMode;
}

qreal textOcrScale()
{
    bool ok = false;
    const qreal scale = QProcessEnvironment::systemEnvironment()
                          .value(QStringLiteral("FLAMESHOT_OCR_SCALE"))
                          .toDouble(&ok);
    return ok && scale > 0 ? scale : 2.0;
}

QString textOcrInvertMode()
{
    QString mode = QProcessEnvironment::systemEnvironment()
                     .value(QStringLiteral("FLAMESHOT_OCR_INVERT"),
                            QStringLiteral("auto"))
                     .trimmed()
                     .toLower();
    return mode.isEmpty() ? QStringLiteral("auto") : mode;
}

QString textOcrPreprocessMode()
{
    QString mode = QProcessEnvironment::systemEnvironment()
                     .value(QStringLiteral("FLAMESHOT_OCR_PREPROCESS"),
                            QStringLiteral("auto"))
                     .trimmed()
                     .toLower();
    mode.replace(QLatin1Char('-'), QLatin1Char('_'));
    return mode.isEmpty() ? QStringLiteral("auto") : mode;
}

bool keepOcrTempImage()
{
    const QString keep =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_OCR_KEEP_TEMP"))
        .trimmed()
        .toLower();
    return keep == QStringLiteral("1") || keep == QStringLiteral("true") ||
           keep == QStringLiteral("yes") || keep == QStringLiteral("on");
}

int luminanceAt(const QImage& image, int x, int y)
{
    const QRgb pixel = image.pixel(x, y);
    return qRound(0.299 * qRed(pixel) + 0.587 * qGreen(pixel) +
                  0.114 * qBlue(pixel));
}

int histogramPercentile(const int histogram[256], int total, qreal percentile);

int medianFromHistogram(const int histogram[256], int total)
{
    if (total <= 0) {
        return 255;
    }

    const int target = (total + 1) / 2;
    int accumulated = 0;
    for (int i = 0; i < 256; ++i) {
        accumulated += histogram[i];
        if (accumulated >= target) {
            return i;
        }
    }
    return 255;
}

int highestHistogramValue(const int histogram[256])
{
    for (int i = 255; i >= 0; --i) {
        if (histogram[i] > 0) {
            return i;
        }
    }
    return 0;
}

struct BorderStats
{
    int red = 255;
    int green = 255;
    int blue = 255;
    int luminance = 255;
    int spread90 = 0;
};

int borderMarginForImage(const QImage& image)
{
    const int shortestSide = std::min(image.width(), image.height());
    return std::max(1, std::min(24, std::max(4, shortestSide / 8)));
}

bool isBorderPixel(const QImage& image, int margin, int x, int y)
{
    return x < margin || y < margin || x >= image.width() - margin ||
           y >= image.height() - margin;
}

int colorDistanceFromBackground(QRgb pixel, const BorderStats& background)
{
    return std::max({ std::abs(qRed(pixel) - background.red),
                      std::abs(qGreen(pixel) - background.green),
                      std::abs(qBlue(pixel) - background.blue) });
}

BorderStats estimateBorderStats(const QImage& image)
{
    BorderStats stats;
    if (image.isNull()) {
        return stats;
    }

    int redHistogram[256] = {};
    int greenHistogram[256] = {};
    int blueHistogram[256] = {};
    int total = 0;
    const int margin = borderMarginForImage(image);

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (!isBorderPixel(image, margin, x, y)) {
                continue;
            }
            const QRgb pixel = image.pixel(x, y);
            ++redHistogram[qRed(pixel)];
            ++greenHistogram[qGreen(pixel)];
            ++blueHistogram[qBlue(pixel)];
            ++total;
        }
    }

    stats.red = medianFromHistogram(redHistogram, total);
    stats.green = medianFromHistogram(greenHistogram, total);
    stats.blue = medianFromHistogram(blueHistogram, total);
    stats.luminance =
      qRound(0.299 * stats.red + 0.587 * stats.green + 0.114 * stats.blue);

    int spreadHistogram[256] = {};
    int spreadTotal = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (!isBorderPixel(image, margin, x, y)) {
                continue;
            }
            ++spreadHistogram[colorDistanceFromBackground(image.pixel(x, y),
                                                          stats)];
            ++spreadTotal;
        }
    }
    stats.spread90 = histogramPercentile(spreadHistogram, spreadTotal, 0.90);

    return stats;
}

bool shouldInvertTextOcrImage(const QImage& source)
{
    const QString mode = textOcrInvertMode();
    if (mode == QStringLiteral("1") || mode == QStringLiteral("true") ||
        mode == QStringLiteral("yes") || mode == QStringLiteral("on")) {
        return true;
    }
    if (mode == QStringLiteral("0") || mode == QStringLiteral("false") ||
        mode == QStringLiteral("no") || mode == QStringLiteral("off")) {
        return false;
    }

    const int stepX = std::max(1, source.width() / 96);
    const int stepY = std::max(1, source.height() / 96);
    qint64 sum = 0;
    int darkSamples = 0;
    int lightSamples = 0;
    int samples = 0;

    for (int y = 0; y < source.height(); y += stepY) {
        for (int x = 0; x < source.width(); x += stepX) {
            const int luminance = luminanceAt(source, x, y);
            sum += luminance;
            ++samples;
            if (luminance < 96) {
                ++darkSamples;
            } else if (luminance > 160) {
                ++lightSamples;
            }
        }
    }

    if (samples == 0) {
        return false;
    }

    const qreal mean = static_cast<qreal>(sum) / samples;
    return mean < 128.0 && darkSamples > lightSamples;
}

bool shouldInvertLatexOcrImage(const QImage& source)
{
    const QString mode = latexOcrInvertMode();
    if (mode == QStringLiteral("1") || mode == QStringLiteral("true") ||
        mode == QStringLiteral("yes") || mode == QStringLiteral("on")) {
        return true;
    }
    if (mode == QStringLiteral("0") || mode == QStringLiteral("false") ||
        mode == QStringLiteral("no") || mode == QStringLiteral("off")) {
        return false;
    }

    const int stepX = std::max(1, source.width() / 96);
    const int stepY = std::max(1, source.height() / 96);
    qint64 sum = 0;
    int darkSamples = 0;
    int lightSamples = 0;
    int samples = 0;

    for (int y = 0; y < source.height(); y += stepY) {
        for (int x = 0; x < source.width(); x += stepX) {
            const int luminance = luminanceAt(source, x, y);
            sum += luminance;
            ++samples;
            if (luminance < 96) {
                ++darkSamples;
            } else if (luminance > 160) {
                ++lightSamples;
            }
        }
    }

    if (samples == 0) {
        return false;
    }

    const qreal mean = static_cast<qreal>(sum) / samples;
    return mean < 128.0 && darkSamples > lightSamples;
}

int histogramPercentile(const int histogram[256], int total, qreal percentile)
{
    if (total <= 0) {
        return 0;
    }

    const int target =
      std::max(1, std::min(total, qRound(total * percentile)));
    int accumulated = 0;
    for (int i = 0; i < 256; ++i) {
        accumulated += histogram[i];
        if (accumulated >= target) {
            return i;
        }
    }
    return 255;
}

QImage contrastStretchedImage(const QImage& image)
{
    int histogram[256] = {};
    int total = 0;
    for (int y = 0; y < image.height(); ++y) {
        const uchar* line = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            ++histogram[line[x]];
            ++total;
        }
    }

    const int low = histogramPercentile(histogram, total, 0.01);
    const int high = histogramPercentile(histogram, total, 0.99);
    if (high <= low + 8) {
        return image;
    }

    QImage stretched(image.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < image.height(); ++y) {
        const uchar* sourceLine = image.constScanLine(y);
        uchar* targetLine = stretched.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const int value =
              (static_cast<int>(sourceLine[x]) - low) * 255 / (high - low);
            targetLine[x] = static_cast<uchar>(
              std::max(0, std::min(255, value)));
        }
    }
    return stretched;
}

int otsuThreshold(const QImage& image)
{
    int histogram[256] = {};
    int total = 0;
    qint64 sum = 0;
    for (int y = 0; y < image.height(); ++y) {
        const uchar* line = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const int value = line[x];
            ++histogram[value];
            ++total;
            sum += value;
        }
    }

    qint64 backgroundSum = 0;
    int backgroundWeight = 0;
    double bestVariance = -1.0;
    int threshold = 127;

    for (int value = 0; value < 256; ++value) {
        backgroundWeight += histogram[value];
        if (backgroundWeight == 0) {
            continue;
        }
        const int foregroundWeight = total - backgroundWeight;
        if (foregroundWeight == 0) {
            break;
        }

        backgroundSum += static_cast<qint64>(value) * histogram[value];
        const double backgroundMean =
          static_cast<double>(backgroundSum) / backgroundWeight;
        const double foregroundMean =
          static_cast<double>(sum - backgroundSum) / foregroundWeight;
        const double difference = backgroundMean - foregroundMean;
        const double variance = static_cast<double>(backgroundWeight) *
                                foregroundWeight * difference * difference;
        if (variance > bestVariance) {
            bestVariance = variance;
            threshold = value;
        }
    }

    return threshold;
}

QImage thresholdedImage(const QImage& image, int threshold)
{
    QImage output(image.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < image.height(); ++y) {
        const uchar* sourceLine = image.constScanLine(y);
        uchar* targetLine = output.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            targetLine[x] = sourceLine[x] <= threshold ? 0 : 255;
        }
    }
    return output;
}

QVector<uchar> sobelEdgeStrength(const QImage& image, int* edgeThreshold)
{
    QVector<uchar> strengths(image.width() * image.height(), 0);
    int histogram[256] = {};
    int nonZeroCount = 0;

    auto gray = [&image](int x, int y) -> int {
        return image.constScanLine(y)[x];
    };

    for (int y = 1; y < image.height() - 1; ++y) {
        for (int x = 1; x < image.width() - 1; ++x) {
            const int gx = -gray(x - 1, y - 1) + gray(x + 1, y - 1) -
                           2 * gray(x - 1, y) + 2 * gray(x + 1, y) -
                           gray(x - 1, y + 1) + gray(x + 1, y + 1);
            const int gy = -gray(x - 1, y - 1) - 2 * gray(x, y - 1) -
                           gray(x + 1, y - 1) + gray(x - 1, y + 1) +
                           2 * gray(x, y + 1) + gray(x + 1, y + 1);
            const int magnitude =
              std::min(255, (std::abs(gx) + std::abs(gy)) / 4);
            strengths[y * image.width() + x] = static_cast<uchar>(magnitude);
            if (magnitude > 0) {
                ++histogram[magnitude];
                ++nonZeroCount;
            }
        }
    }

    int threshold = 255;
    if (nonZeroCount > 0) {
        threshold = std::max(24, histogramPercentile(histogram,
                                                     nonZeroCount,
                                                     0.75));
    }
    if (edgeThreshold) {
        *edgeThreshold = threshold;
    }
    return strengths;
}

QImage edgeReinforcedImage(const QImage& binary,
                           const QImage& gray,
                           bool edgeOnly,
                           int* edgeThreshold)
{
    QImage output(binary.size(), QImage::Format_Grayscale8);
    output.fill(255);
    if (!edgeOnly) {
        output = binary.copy();
    }

    const QVector<uchar> strengths = sobelEdgeStrength(gray, edgeThreshold);
    const int threshold = edgeThreshold ? *edgeThreshold : 255;
    const int width = gray.width();
    const int height = gray.height();
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            if (strengths[y * width + x] < threshold) {
                continue;
            }
            for (int dy = -1; dy <= 1; ++dy) {
                uchar* line = output.scanLine(y + dy);
                for (int dx = -1; dx <= 1; ++dx) {
                    line[x + dx] = 0;
                }
            }
        }
    }

    return output;
}

QImage borderedTextOcrImage(const QImage& content, int padding)
{
    QImage image(content.size() + QSize(padding * 2, padding * 2),
                 QImage::Format_Grayscale8);
    image.fill(255);

    QPainter painter(&image);
    painter.drawImage(padding, padding, content);
    return image;
}

struct ForegroundNormalizationInfo
{
    BorderStats background;
    int otsu = -1;
    int low = -1;
    int high = -1;
    QRect crop;
};

QRect foregroundBounds(const QImage& foregroundStrength, int threshold)
{
    QRect bounds;
    for (int y = 0; y < foregroundStrength.height(); ++y) {
        const uchar* line = foregroundStrength.constScanLine(y);
        for (int x = 0; x < foregroundStrength.width(); ++x) {
            if (line[x] <= threshold) {
                continue;
            }
            const QRect pixelRect(x, y, 1, 1);
            bounds = bounds.isNull() ? pixelRect : bounds.united(pixelRect);
        }
    }
    return bounds;
}

QImage foregroundNormalizedImage(const QImage& source,
                                 int padding,
                                 bool crop,
                                 ForegroundNormalizationInfo* info = nullptr)
{
    const QImage rgb = source.convertToFormat(QImage::Format_RGB32);
    const BorderStats background = estimateBorderStats(rgb);

    QImage distanceImage(rgb.size(), QImage::Format_Grayscale8);
    int histogram[256] = {};
    int total = 0;
    for (int y = 0; y < rgb.height(); ++y) {
        uchar* targetLine = distanceImage.scanLine(y);
        for (int x = 0; x < rgb.width(); ++x) {
            const int distance =
              colorDistanceFromBackground(rgb.pixel(x, y), background);
            targetLine[x] = static_cast<uchar>(distance);
            ++histogram[distance];
            ++total;
        }
    }

    const int otsu = otsuThreshold(distanceImage);
    int high = histogramPercentile(histogram, total, 0.995);
    if (high < 24) {
        high = highestHistogramValue(histogram);
    }
    int low = std::max(6, std::min(otsu, high - 1));
    if (background.spread90 > 18) {
        low = std::max(low, std::min(high - 1, background.spread90 + 8));
    }
    if (low >= high - 2) {
        low = std::max(3, high / 4);
    }

    QImage foregroundStrength(rgb.size(), QImage::Format_Grayscale8);
    QImage blackOnWhite(rgb.size(), QImage::Format_Grayscale8);
    blackOnWhite.fill(255);
    for (int y = 0; y < distanceImage.height(); ++y) {
        const uchar* sourceLine = distanceImage.constScanLine(y);
        uchar* strengthLine = foregroundStrength.scanLine(y);
        uchar* targetLine = blackOnWhite.scanLine(y);
        for (int x = 0; x < distanceImage.width(); ++x) {
            int strength = 0;
            if (high > low) {
                strength =
                  (static_cast<int>(sourceLine[x]) - low) * 255 / (high - low);
            } else if (sourceLine[x] > low) {
                strength = 255;
            }
            strength = std::max(0, std::min(255, strength));
            strengthLine[x] = static_cast<uchar>(strength);
            targetLine[x] = static_cast<uchar>(255 - strength);
        }
    }

    QRect cropRect = crop ? foregroundBounds(foregroundStrength, 8)
                          : blackOnWhite.rect();
    if (cropRect.isNull()) {
        cropRect = blackOnWhite.rect();
    }

    QImage content = blackOnWhite.copy(cropRect);
    if (padding > 0) {
        content = borderedTextOcrImage(content, padding);
    }

    if (info) {
        info->background = background;
        info->otsu = otsu;
        info->low = low;
        info->high = high;
        info->crop = cropRect;
    }
    return content;
}

bool shouldUseTextBackgroundNormalization(const BorderStats& background)
{
    return background.luminance < 190 || background.spread90 > 18;
}

QImage preparedTextOcrImageFromImage(const QImage& input)
{
    constexpr int padding = 16;
    constexpr int maxPreparedSide = 2400;

    const QImage source = input.convertToFormat(QImage::Format_RGB32);
    if (source.isNull()) {
        return {};
    }

    qreal scale = textOcrScale();
    if (source.width() * scale > maxPreparedSide ||
        source.height() * scale > maxPreparedSide) {
        scale = std::min(static_cast<qreal>(maxPreparedSide) / source.width(),
                         static_cast<qreal>(maxPreparedSide) / source.height());
    }

    QImage working = source;
    const QSize scaledSize = working.size() * scale;
    if (scaledSize != working.size()) {
        working =
          working.scaled(scaledSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    const QString mode = textOcrPreprocessMode();
    const bool forceBackgroundNormalization =
      mode == QStringLiteral("background") ||
      mode == QStringLiteral("foreground") ||
      mode == QStringLiteral("normalize_background");
    const bool officialMode = mode == QStringLiteral("auto") ||
                              mode == QStringLiteral("official") ||
                              mode == QStringLiteral("tesseract");
    const BorderStats background = estimateBorderStats(working);
    const bool useBackgroundNormalization =
      forceBackgroundNormalization ||
      (officialMode && shouldUseTextBackgroundNormalization(background));
    const bool invert = !useBackgroundNormalization && shouldInvertTextOcrImage(working);

    int threshold = -1;
    int edgeThreshold = -1;
    ForegroundNormalizationInfo foregroundInfo;
    QImage image;
    if (useBackgroundNormalization) {
        image = foregroundNormalizedImage(working, padding, true, &foregroundInfo);
    } else {
        QImage normalized = working.convertToFormat(QImage::Format_Grayscale8);
        if (invert) {
            normalized.invertPixels(QImage::InvertRgb);
        }

        QImage prepared = normalized;
        if (mode == QStringLiteral("none") ||
            mode == QStringLiteral("grayscale") ||
            mode == QStringLiteral("gray") || officialMode) {
            prepared = normalized;
        } else {
            const QImage contrast = contrastStretchedImage(normalized);
            threshold = otsuThreshold(contrast);
            QImage binary = thresholdedImage(contrast, threshold);
            if (mode == QStringLiteral("threshold") ||
                mode == QStringLiteral("binary")) {
                prepared = binary;
            } else {
                prepared = edgeReinforcedImage(binary,
                                               contrast,
                                               mode == QStringLiteral("outline") ||
                                                 mode == QStringLiteral("edge"),
                                               &edgeThreshold);
            }
        }

        image = borderedTextOcrImage(prepared, padding);
    }

    AbstractLogger::info(AbstractLogger::Stderr)
      << QObject::tr("Text OCR preprocessing: mode=%1, backgroundNormalized=%2, "
                     "backgroundRgb=%3/%4/%5, backgroundLuma=%6, "
                     "backgroundSpread90=%7, invert=%8, scale=%9, threshold=%10, "
                     "edgeThreshold=%11, foregroundLow=%12, foregroundHigh=%13, "
                     "size=%14x%15.")
           .arg(mode,
                useBackgroundNormalization ? QStringLiteral("true")
                                           : QStringLiteral("false"),
                QString::number(background.red),
                QString::number(background.green),
                QString::number(background.blue),
                QString::number(background.luminance),
                QString::number(background.spread90),
                invert ? QStringLiteral("true") : QStringLiteral("false"),
                QString::number(scale, 'f', 2),
                threshold >= 0 ? QString::number(threshold)
                               : QStringLiteral("n/a"),
                edgeThreshold >= 0 ? QString::number(edgeThreshold)
                                   : QStringLiteral("n/a"),
                foregroundInfo.low >= 0 ? QString::number(foregroundInfo.low)
                                        : QStringLiteral("n/a"),
                foregroundInfo.high >= 0 ? QString::number(foregroundInfo.high)
                                         : QStringLiteral("n/a"),
                QString::number(image.width()),
                QString::number(image.height()));
    return image;
}

QImage preparedTextOcrImage(const QPixmap& pixmap)
{
    return preparedTextOcrImageFromImage(pixmap.toImage());
}

QImage preparedLatexOcrImageFromImage(const QImage& input)
{
    constexpr int maxPreparedSide = 2400;

    const QImage source = input.convertToFormat(QImage::Format_RGB32);
    if (source.isNull()) {
        return {};
    }

    const int padding = latexOcrPadding();
    qreal scale = latexOcrScale();
    if (source.width() * scale > maxPreparedSide ||
        source.height() * scale > maxPreparedSide) {
        scale = std::min(static_cast<qreal>(maxPreparedSide) / source.width(),
                         static_cast<qreal>(maxPreparedSide) / source.height());
    }

    QImage working = source;
    const QSize scaledSize = working.size() * scale;
    if (scaledSize != working.size()) {
        working =
          working.scaled(scaledSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    const QString mode = latexOcrPreprocessMode();
    const bool foregroundMode =
      mode == QStringLiteral("normalize") || mode == QStringLiteral("auto") ||
      mode == QStringLiteral("foreground") ||
      mode == QStringLiteral("background") ||
      mode == QStringLiteral("white_background") ||
      mode == QStringLiteral("texteller");
    ForegroundNormalizationInfo foregroundInfo;
    QImage normalized;
    const bool invert = !foregroundMode && shouldInvertLatexOcrImage(working);
    if (mode == QStringLiteral("raw") || mode == QStringLiteral("none")) {
        normalized = working;
        if (invert) {
            normalized.invertPixels(QImage::InvertRgb);
        }
    } else if (foregroundMode) {
        normalized =
          foregroundNormalizedImage(working, padding, true, &foregroundInfo)
            .convertToFormat(QImage::Format_RGB32);
    } else {
        QImage gray = working.convertToFormat(QImage::Format_Grayscale8);
        if (invert) {
            gray.invertPixels(QImage::InvertRgb);
        }
        if (mode == QStringLiteral("contrast")) {
            gray = contrastStretchedImage(gray);
        } else if (mode == QStringLiteral("threshold") ||
                   mode == QStringLiteral("binary")) {
            const QImage contrast = contrastStretchedImage(gray);
            gray = thresholdedImage(contrast, otsuThreshold(contrast));
        }
        normalized = gray.convertToFormat(QImage::Format_RGB32);
    }

    QImage image;
    if (foregroundMode) {
        image = normalized;
    } else {
        image = QImage(normalized.size() + QSize(padding * 2, padding * 2),
                       QImage::Format_RGB32);
        image.fill(Qt::white);

        QPainter painter(&image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(padding, padding, normalized);
    }

    AbstractLogger::info(AbstractLogger::Stderr)
      << QObject::tr("LaTeX OCR preprocessing: mode=%1, foregroundNormalized=%2, "
                     "backgroundRgb=%3/%4/%5, backgroundLuma=%6, "
                     "backgroundSpread90=%7, invert=%8, scale=%9, "
                     "foregroundLow=%10, foregroundHigh=%11, size=%12x%13.")
           .arg(mode,
                foregroundMode ? QStringLiteral("true") : QStringLiteral("false"),
                QString::number(foregroundInfo.background.red),
                QString::number(foregroundInfo.background.green),
                QString::number(foregroundInfo.background.blue),
                QString::number(foregroundInfo.background.luminance),
                QString::number(foregroundInfo.background.spread90),
                invert ? QStringLiteral("true") : QStringLiteral("false"),
                QString::number(scale, 'f', 2),
                foregroundInfo.low >= 0 ? QString::number(foregroundInfo.low)
                                        : QStringLiteral("n/a"),
                foregroundInfo.high >= 0 ? QString::number(foregroundInfo.high)
                                         : QStringLiteral("n/a"),
                QString::number(image.width()),
                QString::number(image.height()));
    return image;
}

QImage preparedLatexOcrImage(const QPixmap& pixmap)
{
    return preparedLatexOcrImageFromImage(pixmap.toImage());
}

QString resolvedExecutable(const QString& executable)
{
    const QString resolved = QStandardPaths::findExecutable(executable);
    return resolved.isEmpty() ? executable : resolved;
}

QString firstExistingExecutable(const QString& executable,
                                const QStringList& candidates)
{
    const QString fromPath = QStandardPaths::findExecutable(executable);
    if (!fromPath.isEmpty()) {
        return fromPath;
    }

    for (const QString& candidate : candidates) {
        QFileInfo info(candidate);
        if (info.exists() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
    }

    return {};
}

QString pythonFromScriptShebang(const QString& scriptPath)
{
    if (scriptPath.isEmpty()) {
        return {};
    }

    QFile file(scriptPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QByteArray firstLine = file.readLine(512).trimmed();
    if (!firstLine.startsWith("#!")) {
        return {};
    }

    const QStringList parts =
      QProcess::splitCommand(QString::fromUtf8(firstLine.mid(2)));
    if (parts.isEmpty()) {
        return {};
    }

    if (QFileInfo(parts.first()).fileName() == QStringLiteral("env") &&
        parts.size() > 1) {
        return QStandardPaths::findExecutable(parts.at(1));
    }

    return resolvedExecutable(parts.first());
}

QString executablePathIfUsable(const QString& path)
{
    QFileInfo info(path);
    if (info.exists() && info.isExecutable()) {
        return info.absoluteFilePath();
    }
    return {};
}

QString paddleOcrPython()
{
    const QString configured =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_PADDLEOCR_PYTHON"))
        .trimmed();
    if (!configured.isEmpty()) {
        const QString executable = executablePathIfUsable(configured);
        if (!executable.isEmpty()) {
            return executable;
        }
        return configured;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QDir currentDir(QDir::currentPath());
    const QStringList candidates = {
        appDir.absoluteFilePath(
          QStringLiteral("../../../.venv-paddleocr/bin/python")),
        currentDir.absoluteFilePath(
          QStringLiteral("../.venv-paddleocr/bin/python")),
        currentDir.absoluteFilePath(QStringLiteral(".venv-paddleocr/bin/python")),
        QDir::home().filePath(
          QStringLiteral(".local/share/flameshot-ocr-backends/"
                         "paddleocr/bin/python")),
    };
    for (const QString& candidate : candidates) {
        const QString executable = executablePathIfUsable(candidate);
        if (!executable.isEmpty()) {
            return executable;
        }
    }

    const QString python3 = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (!python3.isEmpty()) {
        return python3;
    }
    return QStandardPaths::findExecutable(QStringLiteral("python"));
}

QString paddleOcrCacheHome()
{
    const QString configured =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_PADDLEOCR_CACHE"))
        .trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QDir currentDir(QDir::currentPath());
    const QStringList candidates = {
        appDir.absoluteFilePath(QStringLiteral("../../../.cache/paddlex")),
        currentDir.absoluteFilePath(QStringLiteral("../.cache/paddlex")),
        currentDir.absoluteFilePath(QStringLiteral(".cache/paddlex")),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isDir()) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return QDir::home().filePath(QStringLiteral(".cache/flameshot/paddlex"));
}

QString textellerExecutable()
{
    return firstExistingExecutable(
      QStringLiteral("texteller"),
      { QDir::homePath() +
        QStringLiteral(
          "/.local/share/flameshot-ocr-backends/texteller-py311/bin/"
          "texteller") });
}

QString pix2textExecutable()
{
    return firstExistingExecutable(
      QStringLiteral("p2t"),
      { QDir::homePath() +
        QStringLiteral(
          "/.local/share/flameshot-ocr-backends/pix2text-py311/bin/p2t") });
}

QString pix2texExecutable()
{
    return firstExistingExecutable(
      QStringLiteral("pix2tex"),
      { QDir::homePath() + QStringLiteral("/.local/bin/pix2tex") });
}

QString textellerPython()
{
    return pythonFromScriptShebang(textellerExecutable());
}

QString pix2textPython()
{
    return pythonFromScriptShebang(pix2textExecutable());
}

QString pix2texPython()
{
    const QString shebangPython = pythonFromScriptShebang(pix2texExecutable());
    if (!shebangPython.isEmpty()) {
        return shebangPython;
    }

    const QString python3 =
      QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (!python3.isEmpty()) {
        return python3;
    }

    return QStandardPaths::findExecutable(QStringLiteral("python"));
}

QString pix2texPythonScript()
{
    return QStringLiteral(R"PY(
import sys
from argparse import Namespace

try:
    import pandas.io.clipboard as clipboard
    clipboard.copy = lambda text: None
except Exception:
    pass

from PIL import Image
from pix2tex.cli import LatexOCR

arguments = Namespace(
    config='settings/config.yaml',
    checkpoint='checkpoints/weights.pth',
    no_cuda=False,
    no_resize=False,
    temperature=.333,
)

image = Image.open(sys.argv[1])
model = LatexOCR(arguments)
print(model(image))
)PY");
}

QString textellerPythonScript()
{
    return QStringLiteral(R"PY(
import os
import sys

def log(message):
    print("flameshot-texteller: " + message, file=sys.stderr, flush=True)

log("import torch")
import torch
log("import texteller")
from texteller import img2latex, load_model, load_tokenizer

model_path = os.environ.get("FLAMESHOT_TEXTELLER_MODEL_PATH") or None
tokenizer_path = os.environ.get("FLAMESHOT_TEXTELLER_TOKENIZER_PATH") or None
use_onnx = os.environ.get("FLAMESHOT_TEXTELLER_USE_ONNX", "").lower() in (
    "1",
    "true",
    "yes",
    "on",
)
device_name = os.environ.get("FLAMESHOT_LATEX_OCR_DEVICE") or "cpu"
log("select device: " + device_name)
device = torch.device(device_name)

log("load_model begin")
model = load_model(model_path, use_onnx=use_onnx)
log("load_model done")
log("load_tokenizer begin")
tokenizer = load_tokenizer(tokenizer_path)
log("load_tokenizer done")
log("img2latex begin")
result = img2latex(
    model,
    tokenizer,
    [sys.argv[1]],
    device=device,
    out_format="latex",
)[0]
log("img2latex done")
print(result)
)PY");
}

QString textellerServicePythonScript()
{
    return QStringLiteral(R"PY(
import json
import os
import sys
import traceback

def log(message):
    print("flameshot-texteller-worker: " + message, file=sys.stderr, flush=True)

def send(payload):
    print(json.dumps(payload, ensure_ascii=False), flush=True)

log("import torch")
import torch
log("import texteller")
from texteller import img2latex, load_model, load_tokenizer

model_path = os.environ.get("FLAMESHOT_TEXTELLER_MODEL_PATH") or None
tokenizer_path = os.environ.get("FLAMESHOT_TEXTELLER_TOKENIZER_PATH") or None
use_onnx = os.environ.get("FLAMESHOT_TEXTELLER_USE_ONNX", "").lower() in (
    "1",
    "true",
    "yes",
    "on",
)
device_name = os.environ.get("FLAMESHOT_LATEX_OCR_DEVICE") or "cpu"
log("select device: " + device_name)
device = torch.device(device_name)

log("load_model begin")
model = load_model(model_path, use_onnx=use_onnx)
log("load_model done")
log("load_tokenizer begin")
tokenizer = load_tokenizer(tokenizer_path)
log("load_tokenizer done")
send({"type": "ready"})

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    try:
        request = json.loads(line)
        if request.get("cmd") == "quit":
            break
        request_id = request.get("id")
        image_path = request.get("image")
        if not image_path:
            send({"type": "result", "id": request_id, "ok": False, "error": "empty image path"})
            continue
        log("img2latex begin: " + image_path)
        result = img2latex(
            model,
            tokenizer,
            [image_path],
            device=device,
            out_format="latex",
        )[0]
        log("img2latex done")
        send({"type": "result", "id": request_id, "ok": True, "latex": result})
    except Exception as error:
        send({
            "type": "result",
            "id": request.get("id") if "request" in locals() else None,
            "ok": False,
            "error": str(error),
            "traceback": traceback.format_exc(),
        })
)PY");
}

QString pix2textPythonScript()
{
    return QStringLiteral(R"PY(
import os
import sys

def log(message):
    print("flameshot-pix2text: " + message, file=sys.stderr, flush=True)

log("import pix2text")
from pix2text import Pix2Text

device = os.environ.get("FLAMESHOT_LATEX_OCR_DEVICE") or "cpu"
log("load model begin")
p2t = Pix2Text(enable_table=False, device=device)
log("load model done")
log("recognize_formula begin")
result = p2t.recognize_formula(sys.argv[1], return_text=True)
log("recognize_formula done")
if isinstance(result, list):
    print("\n".join(str(item) for item in result))
else:
    print(result)
)PY");
}

QString paddleOcrServicePythonScript()
{
    return QStringLiteral(R"PY(
import json
import os
import re
import sys
import traceback

os.environ.setdefault("NO_ALBUMENTATIONS_UPDATE", "1")
os.environ.setdefault("PADDLE_PDX_MODEL_SOURCE", "bos")
os.environ.setdefault("PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK", "True")

def log(message):
    print("flameshot-paddleocr-worker: " + str(message), file=sys.stderr, flush=True)

def send(payload):
    print(json.dumps(payload, ensure_ascii=False), flush=True)

structure_pipeline = None
text_pipeline = None
formula_pipeline = None

def result_dict(result):
    data = getattr(result, "json", {})
    if callable(data):
        data = data()
    if isinstance(data, dict):
        return data.get("res", data)
    return {}

def result_markdown(result):
    markdown = getattr(result, "markdown", {})
    if callable(markdown):
        markdown = markdown()
    return markdown if isinstance(markdown, dict) else {}

def markdown_text(value):
    if isinstance(value, dict):
        value = (
            value.get("markdown_texts")
            or value.get("markdown_text")
            or value.get("text")
            or ""
        )
    if isinstance(value, list):
        return "\n".join(str(part) for part in value if str(part).strip())
    return str(value or "")

VISUAL_BLOCK_LABELS = {
    "chart",
    "image",
    "figure",
    "table",
    "seal",
}

FORMULA_BLOCK_LABELS = {
    "formula",
    "equation",
}

def clean_markdown(text):
    text = str(text or "").strip()
    if not text:
        return ""
    text = re.sub(
        r"<div[^>]*>\s*<img\b[^>]*>\s*</div>",
        "\n",
        text,
        flags=re.I | re.S,
    )
    text = re.sub(
        r"<p[^>]*>\s*<img\b[^>]*>\s*</p>",
        "\n",
        text,
        flags=re.I | re.S,
    )
    text = re.sub(r"<img\b[^>]*>", "\n", text, flags=re.I | re.S)
    text = re.sub(r"!\[[^\]]*\]\([^)]*\)", "\n", text)
    text = re.sub(r"[ \t]+\n", "\n", text)
    text = re.sub(r"\n{3,}", "\n\n", text)
    text = "\n".join(line.strip() for line in text.splitlines() if line.strip())
    return text

def has_image_placeholder(text):
    return bool(re.search(r"<img\b|!\[[^\]]*\]\([^)]*\)", str(text or ""), re.I))

def structure_markdown(pipeline, outputs):
    markdown_list = []
    visual_block_count = 0
    formula_block_count = 0
    image_placeholder_count = 0
    for result in outputs or []:
        markdown = result_markdown(result)
        if markdown:
            markdown_list.append(markdown)
        data = result_dict(result)
        for block in data.get("parsing_res_list") or []:
            label = str(block.get("block_label") or "").strip().lower()
            if label in FORMULA_BLOCK_LABELS:
                formula_block_count += 1
            if label in VISUAL_BLOCK_LABELS:
                visual_block_count += 1

    markdown = ""
    if markdown_list:
        try:
            combined = pipeline.concatenate_markdown_pages(markdown_list)
            markdown = markdown_text(combined)
        except Exception as error:
            log("concatenate markdown failed: " + str(error))
        if not markdown:
            markdown = "\n\n".join(markdown_text(item) for item in markdown_list)

    if has_image_placeholder(markdown):
        image_placeholder_count += 1
    markdown = clean_markdown(markdown)
    non_text_block_count = (
        visual_block_count + formula_block_count + image_placeholder_count
    )
    return markdown, non_text_block_count

def text_content(outputs):
    parts = []
    for result in outputs or []:
        data = result_dict(result)
        texts = data.get("rec_texts") or []
        if isinstance(texts, list):
            parts.extend(str(item) for item in texts if str(item).strip())
        elif str(texts).strip():
            parts.append(str(texts))
    return "\n".join(parts).strip()

def formula_content(outputs):
    parts = []
    for result in outputs or []:
        data = result_dict(result)
        direct = str(data.get("rec_formula") or "").strip()
        if direct:
            parts.append(direct)
        for item in data.get("formula_res_list") or []:
            latex = str(item.get("rec_formula") or "").strip()
            if latex:
                parts.append(latex)
    return "\n".join(parts).strip()

def plausible_formula(latex):
    latex = str(latex or "").strip()
    if not latex:
        return False
    if re.search(r"<\s*(html|body|div|img|span|style|script)\b", latex, re.I):
        return False
    if re.search(r"(?:\\\s*[A-Za-z]\s*){5,}", latex) and not re.search(
        r"[=<>≤≥≈∝]|\\(frac|sum|int|sqrt|partial|alpha|beta|gamma|theta|lambda|mu|nu|pi|infty)\b",
        latex,
    ):
        return False
    relation_count = len(re.findall(r"[=<>≤≥≈∝]", latex))
    strong_command_count = len(
        re.findall(
            r"\\(frac|dfrac|tfrac|sqrt|sum|prod|int|oint|partial|lim|"
            r"alpha|beta|gamma|delta|epsilon|theta|lambda|mu|nu|pi|rho|"
            r"sigma|tau|phi|varphi|psi|omega|Gamma|Delta|Theta|Lambda|"
            r"Sigma|Phi|Psi|Omega|mathbf|mathbb|mathcal|mathrm|hat|bar|"
            r"dot|ddot|vec|sin|cos|tan|log|ln|exp)\b",
            latex,
        )
    )
    math_token_count = len(
        re.findall(
            r"[_^{}+\-*/]|[∂∑∫√∞πμνλΛαβγθΩ]",
            latex,
        )
    )
    return relation_count > 0 or strong_command_count > 0 or math_token_count >= 3

def init_structure_pipeline():
    global structure_pipeline
    if structure_pipeline is None:
        from paddleocr import PPStructureV3
        log("load PPStructureV3 begin")
        structure_pipeline = PPStructureV3(
            layout_detection_model_name=os.environ.get(
                "FLAMESHOT_PADDLEOCR_LAYOUT_MODEL", "PP-DocLayout-M"
            ),
            text_detection_model_name=os.environ.get(
                "FLAMESHOT_PADDLEOCR_TEXT_DET_MODEL", "PP-OCRv5_mobile_det"
            ),
            text_recognition_model_name=os.environ.get(
                "FLAMESHOT_PADDLEOCR_TEXT_REC_MODEL", "PP-OCRv5_mobile_rec"
            ),
            formula_recognition_model_name=os.environ.get(
                "FLAMESHOT_PADDLEOCR_FORMULA_MODEL", "PP-FormulaNet_plus-S"
            ),
            use_doc_orientation_classify=False,
            use_doc_unwarping=False,
            use_textline_orientation=False,
            use_table_recognition=False,
            use_seal_recognition=False,
            use_chart_recognition=False,
            use_region_detection=False,
            use_formula_recognition=True,
            format_block_content=True,
        )
        log("load PPStructureV3 done")
    return structure_pipeline

def init_text_pipeline():
    global text_pipeline
    if text_pipeline is None:
        from paddleocr import PaddleOCR
        log("load PaddleOCR text pipeline begin")
        text_pipeline = PaddleOCR(
            text_detection_model_name=os.environ.get(
                "FLAMESHOT_PADDLEOCR_TEXT_DET_MODEL", "PP-OCRv5_mobile_det"
            ),
            text_recognition_model_name=os.environ.get(
                "FLAMESHOT_PADDLEOCR_TEXT_REC_MODEL", "PP-OCRv5_mobile_rec"
            ),
            use_doc_orientation_classify=False,
            use_doc_unwarping=False,
            use_textline_orientation=False,
        )
        log("load PaddleOCR text pipeline done")
    return text_pipeline

def init_formula_pipeline():
    global formula_pipeline
    if formula_pipeline is None:
        from paddleocr import FormulaRecognitionPipeline
        log("load formula pipeline begin")
        formula_pipeline = FormulaRecognitionPipeline(
            formula_recognition_model_name=os.environ.get(
                "FLAMESHOT_PADDLEOCR_FORMULA_MODEL", "PP-FormulaNet_plus-S"
            ),
            use_doc_orientation_classify=False,
            use_doc_unwarping=False,
            use_layout_detection=False,
        )
        log("load formula pipeline done")
    return formula_pipeline

def truthy_env(name, default):
    value = os.environ.get(name)
    if value is None:
        return default
    return value.strip().lower() in ("1", "true", "yes", "on")

def recognize(image_path, formula_image_path):
    use_structure = truthy_env("FLAMESHOT_PADDLEOCR_STRUCTURE", True)
    markdown = ""
    latex = ""
    non_text_block_count = 0
    if use_structure:
        log("structure predict begin: " + image_path)
        pipeline = init_structure_pipeline()
        structure_text, structure_non_text_count = structure_markdown(
            pipeline,
            pipeline.predict(input=image_path)
        )
        log("structure predict done")
        markdown = structure_text
        non_text_block_count += structure_non_text_count
        if not markdown and formula_image_path:
            log("structure predict normalized formula image begin: " + formula_image_path)
            normalized_markdown, normalized_non_text_count = structure_markdown(
                pipeline,
                pipeline.predict(input=formula_image_path)
            )
            log("structure predict normalized formula image done")
            non_text_block_count += normalized_non_text_count
            markdown = normalized_markdown

    force_prepared_formula = truthy_env(
        "FLAMESHOT_PADDLEOCR_FORCE_PREPARED_FORMULA", False
    )
    use_prepared_formula = formula_image_path and (
        force_prepared_formula or (not markdown and non_text_block_count > 0)
    )
    if use_prepared_formula:
        log("formula predict begin: " + formula_image_path)
        prepared_latex = formula_content(
            init_formula_pipeline().predict(input=formula_image_path)
        )
        log("formula predict done")
        if plausible_formula(prepared_latex):
            markdown = "$$\n" + prepared_latex.strip() + "\n$$"

    if not markdown:
        log("text predict begin: " + image_path)
        markdown = text_content(init_text_pipeline().predict(input=image_path))
        log("text predict done")

    return markdown.strip(), latex.strip()

send({"type": "ready"})

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    request = {}
    try:
        request = json.loads(line)
        if request.get("cmd") == "quit":
            break
        request_id = request.get("id")
        image_path = request.get("image") or ""
        formula_image_path = request.get("formula_image") or ""
        if not image_path:
            send({
                "type": "result",
                "id": request_id,
                "ok": False,
                "error": "empty image path",
            })
            continue
        text, latex = recognize(image_path, formula_image_path)
        send({
            "type": "result",
            "id": request_id,
            "ok": True,
            "text": text,
            "latex": latex,
        })
    except Exception as error:
        send({
            "type": "result",
            "id": request.get("id"),
            "ok": False,
            "error": str(error),
            "traceback": traceback.format_exc(),
        })
)PY");
}

QStringList availableOcrLanguages(const QString& tesseract)
{
    QProcess process;
    process.start(tesseract, { QStringLiteral("--list-langs") });
    if (!process.waitForStarted() || !process.waitForFinished(5000) ||
        process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        return {};
    }

    QStringList languages;
    const auto lines = QString::fromUtf8(process.readAllStandardOutput())
                         .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (!line.startsWith(QStringLiteral("List of available languages"))) {
            languages << line;
        }
    }
    return languages;
}

QString configuredOcrLanguage()
{
    return QProcessEnvironment::systemEnvironment()
      .value(QStringLiteral("FLAMESHOT_OCR_LANGUAGE"))
      .trimmed();
}

QString ocrLanguage(const QString& tesseract)
{
    const QString configuredLanguage = configuredOcrLanguage();
    if (!configuredLanguage.isEmpty()) {
        return configuredLanguage;
    }

    const QStringList languages = availableOcrLanguages(tesseract);
    if (languages.contains(QStringLiteral("chi_sim"))) {
        return languages.contains(QStringLiteral("eng"))
                 ? QStringLiteral("chi_sim+eng")
                 : QStringLiteral("chi_sim");
    }

    return {};
}

QStringList ocrLanguageCandidates(const QString& tesseract)
{
    const QString configuredLanguage = configuredOcrLanguage();
    if (!configuredLanguage.isEmpty()) {
        return { configuredLanguage };
    }

    const QStringList languages = availableOcrLanguages(tesseract);
    QStringList candidates;
    if (languages.contains(QStringLiteral("chi_sim"))) {
        candidates << QStringLiteral("chi_sim");
    }
    if (languages.contains(QStringLiteral("eng"))) {
        candidates << QStringLiteral("eng");
    }
    if (languages.contains(QStringLiteral("chi_sim")) &&
        languages.contains(QStringLiteral("eng"))) {
        candidates << QStringLiteral("chi_sim+eng");
    }
    return candidates;
}

bool isChineseCodepoint(QChar ch)
{
    const uint codepoint = ch.unicode();
    return (codepoint >= 0x4e00 && codepoint <= 0x9fff) ||
           (codepoint >= 0x3400 && codepoint <= 0x4dbf);
}

bool prepareConfiguredCommand(QString commandSpec,
                              const QString& imagePath,
                              OcrTaskWidget::BackendCommand* command)
{
    commandSpec.replace(QStringLiteral("{image}"), imagePath);
    QStringList parts = QProcess::splitCommand(commandSpec);
    if (parts.isEmpty()) {
        return false;
    }

    command->backendName = QStringLiteral("custom");
    command->program = parts.takeFirst();
    command->arguments = parts;
    if (!commandSpec.contains(imagePath)) {
        command->arguments.append(imagePath);
    }
    return true;
}

bool prepareTextellerCommand(const QString& imagePath,
                             OcrTaskWidget::BackendCommand* command)
{
    const QString python = textellerPython();
    if (python.isEmpty()) {
        return false;
    }

    command->backendName = QStringLiteral("texteller");
    command->program = python;
    command->arguments = { QStringLiteral("-u"),
                           QStringLiteral("-c"),
                           textellerPythonScript(),
                           imagePath };
    return true;
}

bool preparePix2textCommand(const QString& imagePath,
                            OcrTaskWidget::BackendCommand* command)
{
    const QString python = pix2textPython();
    if (python.isEmpty()) {
        return false;
    }

    command->backendName = QStringLiteral("pix2text");
    command->program = python;
    command->arguments = { QStringLiteral("-u"),
                           QStringLiteral("-c"),
                           pix2textPythonScript(),
                           imagePath };
    return true;
}

bool preparePix2texCommand(const QString& imagePath,
                           OcrTaskWidget::BackendCommand* command)
{
    const QString python = pix2texPython();
    if (python.isEmpty()) {
        return false;
    }

    command->backendName = QStringLiteral("pix2tex");
    command->program = python;
    command->arguments = { QStringLiteral("-u"),
                           QStringLiteral("-c"),
                           pix2texPythonScript(),
                           imagePath };
    return true;
}

bool appendLatexBackendCommand(const QString& backend,
                               const QString& imagePath,
                               QVector<OcrTaskWidget::BackendCommand>* commands)
{
    OcrTaskWidget::BackendCommand command;
    if (backend == QStringLiteral("texteller")) {
        if (!prepareTextellerCommand(imagePath, &command)) {
            return false;
        }
    } else if (backend == QStringLiteral("pix2text")) {
        if (!preparePix2textCommand(imagePath, &command)) {
            return false;
        }
    } else if (backend == QStringLiteral("pix2tex")) {
        if (!preparePix2texCommand(imagePath, &command)) {
            return false;
        }
    } else {
        return false;
    }

    commands->append(command);
    return true;
}

bool prepareLatexOcrCommands(
  const QString& imagePath,
  QVector<OcrTaskWidget::BackendCommand>* commands)
{
    const QString commandSpec = configuredLatexOcrCommandSpec();
    if (!commandSpec.isEmpty()) {
        OcrTaskWidget::BackendCommand command;
        if (!prepareConfiguredCommand(commandSpec, imagePath, &command)) {
            return false;
        }
        commands->append(command);
        return true;
    }

    const QString backend = configuredLatexOcrBackend();
    if (backend == QStringLiteral("auto")) {
        appendLatexBackendCommand(QStringLiteral("texteller"), imagePath, commands);
        appendLatexBackendCommand(QStringLiteral("pix2text"), imagePath, commands);
        appendLatexBackendCommand(QStringLiteral("pix2tex"), imagePath, commands);
        return !commands->isEmpty();
    }

    return appendLatexBackendCommand(backend, imagePath, commands);
}

QString processErrorMessage(const QString& prefix, const QString& errorOutput)
{
    QString cleanedError = errorOutput.trimmed();
    if (cleanedError.isEmpty()) {
        return prefix;
    }
    cleanedError.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return QStringLiteral("%1: %2").arg(prefix, cleanedError.left(240));
}

QProcessEnvironment ocrProcessEnvironment()
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString keepTransformersCache =
      environment
        .value(QStringLiteral("FLAMESHOT_LATEX_OCR_KEEP_TRANSFORMERS_CACHE"))
        .trimmed()
        .toLower();
    if (keepTransformersCache != QStringLiteral("1") &&
        keepTransformersCache != QStringLiteral("true") &&
        keepTransformersCache != QStringLiteral("yes") &&
        keepTransformersCache != QStringLiteral("on")) {
        environment.remove(QStringLiteral("TRANSFORMERS_CACHE"));
    }
    environment.insert(QStringLiteral("NO_ALBUMENTATIONS_UPDATE"),
                       QStringLiteral("1"));
    environment.insert(QStringLiteral("HF_HUB_DISABLE_TELEMETRY"),
                       QStringLiteral("1"));
    environment.insert(QStringLiteral("TOKENIZERS_PARALLELISM"),
                       QStringLiteral("false"));
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
    if (!environment.contains(QStringLiteral("TRANSFORMERS_VERBOSITY"))) {
        environment.insert(QStringLiteral("TRANSFORMERS_VERBOSITY"),
                           QStringLiteral("error"));
    }
    if (!environment.contains(QStringLiteral("HF_HUB_OFFLINE"))) {
        environment.insert(QStringLiteral("HF_HUB_OFFLINE"), QStringLiteral("1"));
    }
    if (!environment.contains(QStringLiteral("TRANSFORMERS_OFFLINE"))) {
        environment.insert(QStringLiteral("TRANSFORMERS_OFFLINE"),
                           QStringLiteral("1"));
    }
    if (!environment.contains(QStringLiteral("HF_HOME"))) {
        environment.insert(QStringLiteral("HF_HOME"),
                           QDir::homePath() +
                             QStringLiteral("/.cache/flameshot/huggingface"));
    }
    if (!environment.contains(QStringLiteral("MPLCONFIGDIR"))) {
        environment.insert(QStringLiteral("MPLCONFIGDIR"),
                           QDir::homePath() +
                             QStringLiteral("/.cache/flameshot/matplotlib"));
    }
    if (!environment.contains(QStringLiteral("YOLO_CONFIG_DIR"))) {
        environment.insert(QStringLiteral("YOLO_CONFIG_DIR"),
                           QDir::homePath() +
                             QStringLiteral("/.cache/flameshot/ultralytics"));
    }
    if (!environment.contains(QStringLiteral("PADDLE_PDX_CACHE_HOME"))) {
        environment.insert(QStringLiteral("PADDLE_PDX_CACHE_HOME"),
                           paddleOcrCacheHome());
    }
    if (!environment.contains(QStringLiteral("MODELSCOPE_CACHE"))) {
        environment.insert(QStringLiteral("MODELSCOPE_CACHE"),
                           QDir::homePath() +
                             QStringLiteral("/.cache/flameshot/modelscope"));
    }
    if (!environment.contains(QStringLiteral("PADDLE_PDX_MODEL_SOURCE"))) {
        environment.insert(QStringLiteral("PADDLE_PDX_MODEL_SOURCE"),
                           QStringLiteral("bos"));
    }
    if (!environment.contains(
          QStringLiteral("PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK"))) {
        environment.insert(QStringLiteral("PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK"),
                           QStringLiteral("True"));
    }
    QDir().mkpath(environment.value(QStringLiteral("PADDLE_PDX_CACHE_HOME")));
    QDir().mkpath(environment.value(QStringLiteral("HF_HOME")));
    QDir().mkpath(environment.value(QStringLiteral("MPLCONFIGDIR")));
    QDir().mkpath(environment.value(QStringLiteral("YOLO_CONFIG_DIR")));
    QDir().mkpath(environment.value(QStringLiteral("MODELSCOPE_CACHE")));
    return environment;
}

class TextellerService : public QObject
{
public:
    using Callback = std::function<void(bool, const QString&)>;

    explicit TextellerService(QObject* parent = nullptr)
      : QObject(parent)
      , m_idleTimer(new QTimer(this))
    {
        m_idleTimer->setSingleShot(true);
        connect(m_idleTimer, &QTimer::timeout, this, [this]() {
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("Texteller worker idle timeout reached; stopping.");
            stopProcess();
        });
    }

    int recognize(const QString& imagePath, Callback callback)
    {
        m_idleTimer->stop();
        Request request;
        request.id = m_nextRequestId++;
        request.imagePath = imagePath;
        request.callback = std::move(callback);
        m_queue.append(request);
        ensureProcess();
        startNextRequest();
        return request.id;
    }

    void cancel(int id)
    {
        for (int i = 0; i < m_queue.size(); ++i) {
            if (m_queue.at(i).id != id) {
                continue;
            }
            const Request request = m_queue.takeAt(i);
            if (request.callback) {
                request.callback(false, QObject::tr("texteller task cancelled"));
            }
            return;
        }

        if (m_current.id == id) {
            const Callback callback = m_current.callback;
            m_current = {};
            if (callback) {
                callback(false, QObject::tr("texteller task cancelled"));
            }
            stopProcess();
            if (!m_queue.isEmpty()) {
                ensureProcess();
            }
            startNextRequest();
        }
    }

    void stop()
    {
        for (const Request& request : m_queue) {
            if (request.callback) {
                request.callback(false,
                                 QObject::tr("texteller worker was stopped"));
            }
        }
        m_queue.clear();
        if (m_current.id != 0 && m_current.callback) {
            m_current.callback(false,
                               QObject::tr("texteller worker was stopped"));
        }
        m_current = {};
        stopProcess();
    }

    bool isRunning() const
    {
        return m_process && m_process->state() != QProcess::NotRunning;
    }

private:
    struct Request
    {
        int id = 0;
        QString imagePath;
        Callback callback;
    };

    void ensureProcess()
    {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            return;
        }

        const QString python = textellerPython();
        if (python.isEmpty()) {
            failPending(QObject::tr("texteller executable was not found"));
            return;
        }

        m_idleTimer->stop();
        m_ready = false;
        m_stdoutBuffer.clear();
        m_process = new QProcess(this);
        m_process->setProcessEnvironment(ocrProcessEnvironment());

        connect(m_process, &QProcess::started, this, [this]() {
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("Texteller worker started: pid=%1.")
                   .arg(QString::number(m_process->processId()));
        });
        connect(m_process,
                &QProcess::readyReadStandardOutput,
                this,
                [this]() { drainStdout(); });
        connect(m_process,
                &QProcess::readyReadStandardError,
                this,
                [this]() { drainStderr(); });
        connect(m_process,
                &QProcess::finished,
                this,
                [this](int exitCode, QProcess::ExitStatus exitStatus) {
                    handleFinished(exitCode, exitStatus);
                });

        AbstractLogger::info(AbstractLogger::Stderr)
          << QObject::tr("Texteller worker launching: program=%1.").arg(python);
        m_process->start(python,
                         { QStringLiteral("-u"),
                           QStringLiteral("-c"),
                           textellerServicePythonScript() });
    }

    void startNextRequest()
    {
        if (!m_ready || m_current.id != 0 || m_queue.isEmpty() || !m_process ||
            m_process->state() != QProcess::Running) {
            return;
        }

        m_current = m_queue.takeFirst();
        QJsonObject request;
        request.insert(QStringLiteral("cmd"), QStringLiteral("recognize"));
        request.insert(QStringLiteral("id"), m_current.id);
        request.insert(QStringLiteral("image"), m_current.imagePath);
        const QByteArray line =
          QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        m_process->write(line);
    }

    void drainStdout()
    {
        if (!m_process) {
            return;
        }

        m_stdoutBuffer += QString::fromUtf8(m_process->readAllStandardOutput());
        int newline = m_stdoutBuffer.indexOf(QLatin1Char('\n'));
        while (newline >= 0) {
            const QString line = m_stdoutBuffer.left(newline).trimmed();
            m_stdoutBuffer.remove(0, newline + 1);
            handleProtocolLine(line);
            newline = m_stdoutBuffer.indexOf(QLatin1Char('\n'));
        }
    }

    void drainStderr()
    {
        if (!m_process) {
            return;
        }

        const QString chunk =
          QString::fromLocal8Bit(m_process->readAllStandardError());
        const QStringList lines =
          chunk.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (QString line : lines) {
            line = line.trimmed();
            if (line.isEmpty()) {
                continue;
            }
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("Texteller worker stderr: %1").arg(line.left(500));
        }
    }

    void handleProtocolLine(const QString& line)
    {
        if (line.isEmpty()) {
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document =
          QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !document.isObject()) {
            AbstractLogger::warning(AbstractLogger::Stderr)
              << QObject::tr("Texteller worker returned non-JSON output: %1")
                   .arg(line.left(500));
            return;
        }

        const QJsonObject object = document.object();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("ready")) {
            m_ready = true;
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("Texteller worker ready.");
            startNextRequest();
            return;
        }

        if (type != QStringLiteral("result")) {
            return;
        }

        const int id = object.value(QStringLiteral("id")).toInt();
        if (m_current.id == 0 || id != m_current.id) {
            AbstractLogger::warning(AbstractLogger::Stderr)
              << QObject::tr("Texteller worker returned an unexpected result id: %1")
                   .arg(QString::number(id));
            return;
        }

        const Callback callback = m_current.callback;
        const bool ok = object.value(QStringLiteral("ok")).toBool();
        const QString result =
          ok ? object.value(QStringLiteral("latex")).toString()
             : object.value(QStringLiteral("error")).toString();
        m_current = {};
        if (callback) {
            callback(ok, result);
        }

        if (m_queue.isEmpty()) {
            const int idleTimeout = textellerIdleTimeoutMs();
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("Texteller worker idle timer started: %1 ms.")
                   .arg(QString::number(idleTimeout));
            m_idleTimer->start(idleTimeout);
        }
        startNextRequest();
    }

    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
    {
        Q_UNUSED(exitCode)
        Q_UNUSED(exitStatus)

        if (m_process) {
            m_process->deleteLater();
            m_process = nullptr;
        }
        m_ready = false;
        m_stdoutBuffer.clear();

        if (m_current.id != 0) {
            const Callback callback = m_current.callback;
            m_current = {};
            if (callback) {
                callback(false,
                         QObject::tr("texteller worker exited unexpectedly"));
            }
        }

        if (!m_queue.isEmpty()) {
            ensureProcess();
        }
    }

    void failPending(const QString& error)
    {
        for (const Request& request : m_queue) {
            if (request.callback) {
                request.callback(false, error);
            }
        }
        m_queue.clear();
        if (m_current.id != 0 && m_current.callback) {
            m_current.callback(false, error);
        }
        m_current = {};
    }

    void stopProcess()
    {
        m_idleTimer->stop();
        if (!m_process) {
            m_ready = false;
            return;
        }

        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->write("{\"cmd\":\"quit\"}\n");
            m_process->closeWriteChannel();
            if (!m_process->waitForFinished(1500)) {
                m_process->kill();
                m_process->waitForFinished(1000);
            }
        }
        m_process->deleteLater();
        m_process = nullptr;
        m_ready = false;
        m_stdoutBuffer.clear();
    }

    QProcess* m_process = nullptr;
    QTimer* m_idleTimer = nullptr;
    QList<Request> m_queue;
    Request m_current;
    QString m_stdoutBuffer;
    int m_nextRequestId = 1;
    bool m_ready = false;
};

TextellerService* textellerService()
{
    static TextellerService* service = new TextellerService(qApp);
    return service;
}

class PaddleOcrService : public QObject
{
public:
    using Callback =
      std::function<void(bool, const QString&, const QString&, const QString&)>;

    explicit PaddleOcrService(QObject* parent = nullptr)
      : QObject(parent)
      , m_idleTimer(new QTimer(this))
    {
        m_idleTimer->setSingleShot(true);
        connect(m_idleTimer, &QTimer::timeout, this, [this]() {
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("PaddleOCR worker idle timeout reached; stopping.");
            stopProcess();
        });
    }

    int recognize(const QString& imagePath,
                  const QString& formulaImagePath,
                  Callback callback)
    {
        m_idleTimer->stop();
        Request request;
        request.id = m_nextRequestId++;
        request.imagePath = imagePath;
        request.formulaImagePath = formulaImagePath;
        request.callback = std::move(callback);
        m_queue.append(request);
        ensureProcess();
        startNextRequest();
        return request.id;
    }

    void cancel(int id)
    {
        for (int i = 0; i < m_queue.size(); ++i) {
            if (m_queue.at(i).id != id) {
                continue;
            }
            const Request request = m_queue.takeAt(i);
            if (request.callback) {
                request.callback(
                  false, QString(), QString(),
                  QObject::tr("PaddleOCR task cancelled"));
            }
            return;
        }

        if (m_current.id == id) {
            const Callback callback = m_current.callback;
            m_current = {};
            if (callback) {
                callback(false,
                         QString(),
                         QString(),
                         QObject::tr("PaddleOCR task cancelled"));
            }
            stopProcess();
            if (!m_queue.isEmpty()) {
                ensureProcess();
            }
            startNextRequest();
        }
    }

    void stop()
    {
        for (const Request& request : m_queue) {
            if (request.callback) {
                request.callback(
                  false, QString(), QString(),
                  QObject::tr("PaddleOCR worker was stopped"));
            }
        }
        m_queue.clear();
        if (m_current.id != 0 && m_current.callback) {
            m_current.callback(false,
                               QString(),
                               QString(),
                               QObject::tr("PaddleOCR worker was stopped"));
        }
        m_current = {};
        stopProcess();
    }

    bool isRunning() const
    {
        return m_process && m_process->state() != QProcess::NotRunning;
    }

private:
    struct Request
    {
        int id = 0;
        QString imagePath;
        QString formulaImagePath;
        Callback callback;
    };

    void ensureProcess()
    {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            return;
        }

        const QString python = paddleOcrPython();
        if (python.isEmpty()) {
            failPending(QObject::tr(
              "PaddleOCR requires Python with paddleocr installed."));
            return;
        }

        m_idleTimer->stop();
        m_ready = false;
        m_stdoutBuffer.clear();
        m_process = new QProcess(this);
        m_process->setProcessEnvironment(ocrProcessEnvironment());

        connect(m_process, &QProcess::started, this, [this]() {
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("PaddleOCR worker started: pid=%1.")
                   .arg(QString::number(m_process->processId()));
        });
        connect(m_process,
                &QProcess::readyReadStandardOutput,
                this,
                [this]() { drainStdout(); });
        connect(m_process,
                &QProcess::readyReadStandardError,
                this,
                [this]() { drainStderr(); });
        connect(m_process,
                &QProcess::finished,
                this,
                [this](int exitCode, QProcess::ExitStatus exitStatus) {
                    handleFinished(exitCode, exitStatus);
                });

        AbstractLogger::info(AbstractLogger::Stderr)
          << QObject::tr("PaddleOCR worker launching: program=%1.").arg(python);
        m_process->start(python,
                         { QStringLiteral("-u"),
                           QStringLiteral("-c"),
                           paddleOcrServicePythonScript() });
    }

    void startNextRequest()
    {
        if (!m_ready || m_current.id != 0 || m_queue.isEmpty() || !m_process ||
            m_process->state() != QProcess::Running) {
            return;
        }

        m_current = m_queue.takeFirst();
        QJsonObject request;
        request.insert(QStringLiteral("cmd"), QStringLiteral("recognize"));
        request.insert(QStringLiteral("id"), m_current.id);
        request.insert(QStringLiteral("image"), m_current.imagePath);
        request.insert(QStringLiteral("formula_image"),
                       m_current.formulaImagePath);
        const QByteArray line =
          QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        m_process->write(line);
    }

    void drainStdout()
    {
        if (!m_process) {
            return;
        }

        m_stdoutBuffer += QString::fromUtf8(m_process->readAllStandardOutput());
        int newline = m_stdoutBuffer.indexOf(QLatin1Char('\n'));
        while (newline >= 0) {
            const QString line = m_stdoutBuffer.left(newline).trimmed();
            m_stdoutBuffer.remove(0, newline + 1);
            handleProtocolLine(line);
            newline = m_stdoutBuffer.indexOf(QLatin1Char('\n'));
        }
    }

    void drainStderr()
    {
        if (!m_process) {
            return;
        }

        const QString chunk =
          QString::fromLocal8Bit(m_process->readAllStandardError());
        const QStringList lines =
          chunk.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (QString line : lines) {
            line = line.trimmed();
            if (line.isEmpty()) {
                continue;
            }
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("PaddleOCR worker stderr: %1").arg(line.left(500));
        }
    }

    void handleProtocolLine(const QString& line)
    {
        if (line.isEmpty()) {
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document =
          QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !document.isObject()) {
            AbstractLogger::warning(AbstractLogger::Stderr)
              << QObject::tr("PaddleOCR worker returned non-JSON output: %1")
                   .arg(line.left(500));
            return;
        }

        const QJsonObject object = document.object();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("ready")) {
            m_ready = true;
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("PaddleOCR worker ready.");
            startNextRequest();
            return;
        }

        if (type != QStringLiteral("result")) {
            return;
        }

        const int id = object.value(QStringLiteral("id")).toInt();
        if (m_current.id == 0 || id != m_current.id) {
            AbstractLogger::warning(AbstractLogger::Stderr)
              << QObject::tr("PaddleOCR worker returned an unexpected result id: %1")
                   .arg(QString::number(id));
            return;
        }

        const Callback callback = m_current.callback;
        const bool ok = object.value(QStringLiteral("ok")).toBool();
        const QString text = object.value(QStringLiteral("text")).toString();
        const QString latex = object.value(QStringLiteral("latex")).toString();
        QString error = object.value(QStringLiteral("error")).toString();
        const QString traceback =
          object.value(QStringLiteral("traceback")).toString();
        if (!traceback.isEmpty()) {
            AbstractLogger::warning(AbstractLogger::Stderr)
              << QObject::tr("PaddleOCR worker traceback: %1")
                   .arg(traceback.left(1200));
        }
        if (!ok && error.isEmpty()) {
            error = QObject::tr("PaddleOCR failed.");
        }

        m_current = {};
        if (callback) {
            callback(ok, text, latex, error);
        }

        if (m_queue.isEmpty()) {
            const int idleTimeout = paddleOcrIdleTimeoutMs();
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("PaddleOCR worker idle timer started: %1 ms.")
                   .arg(QString::number(idleTimeout));
            m_idleTimer->start(idleTimeout);
        }
        startNextRequest();
    }

    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
    {
        Q_UNUSED(exitCode)
        Q_UNUSED(exitStatus)

        if (m_process) {
            m_process->deleteLater();
            m_process = nullptr;
        }
        m_ready = false;
        m_stdoutBuffer.clear();

        if (m_current.id != 0) {
            const Callback callback = m_current.callback;
            m_current = {};
            if (callback) {
                callback(false,
                         QString(),
                         QString(),
                         QObject::tr("PaddleOCR worker exited unexpectedly"));
            }
        }

        if (!m_queue.isEmpty()) {
            ensureProcess();
        }
    }

    void failPending(const QString& error)
    {
        for (const Request& request : m_queue) {
            if (request.callback) {
                request.callback(false, QString(), QString(), error);
            }
        }
        m_queue.clear();
        if (m_current.id != 0 && m_current.callback) {
            m_current.callback(false, QString(), QString(), error);
        }
        m_current = {};
    }

    void stopProcess()
    {
        m_idleTimer->stop();
        if (!m_process) {
            m_ready = false;
            return;
        }

        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->write("{\"cmd\":\"quit\"}\n");
            m_process->closeWriteChannel();
            if (!m_process->waitForFinished(1500)) {
                m_process->kill();
                m_process->waitForFinished(1000);
            }
        }
        m_process->deleteLater();
        m_process = nullptr;
        m_ready = false;
        m_stdoutBuffer.clear();
    }

    QProcess* m_process = nullptr;
    QTimer* m_idleTimer = nullptr;
    QList<Request> m_queue;
    Request m_current;
    QString m_stdoutBuffer;
    int m_nextRequestId = 1;
    bool m_ready = false;
};

PaddleOcrService* paddleOcrService()
{
    static PaddleOcrService* service = new PaddleOcrService(qApp);
    return service;
}

QString backendNames(const QVector<OcrTaskWidget::BackendCommand>& commands)
{
    QStringList names;
    for (const OcrTaskWidget::BackendCommand& command : commands) {
        names << command.backendName;
    }
    return names.join(QStringLiteral(" -> "));
}

QString latexPreview(const QString& latex)
{
    QString preview = latex.simplified();
    if (preview.size() > 120) {
        preview = preview.left(120) + QStringLiteral("...");
    }
    return preview;
}

QString ocrTextPreview(const QString& text)
{
    QString preview = text.simplified();
    if (preview.size() > 120) {
        preview = preview.left(120) + QStringLiteral("...");
    }
    return preview;
}
}

QImage OcrPreprocessor::preparedTextOcrImage(const QImage& image)
{
    return preparedTextOcrImageFromImage(image);
}

QImage OcrPreprocessor::preparedLatexOcrImage(const QImage& image)
{
    return preparedLatexOcrImageFromImage(image);
}

OcrTaskWidget::OcrTaskWidget(Kind kind,
                             const QPixmap& capture,
                             QWidget* parent)
  : QWidget(parent)
  , m_kind(kind)
  , m_capture(capture)
{
    setAttribute(Qt::WA_DeleteOnClose);
}

OcrTaskWidget::~OcrTaskWidget()
{
    cleanupProcess();
    cleanupImage();
}

void OcrTaskWidget::stopPaddleOcrService()
{
    paddleOcrService()->stop();
}

bool OcrTaskWidget::isPaddleOcrServiceRunning()
{
    return paddleOcrService()->isRunning();
}

void OcrTaskWidget::start()
{
    if (m_capture.isNull()) {
        failTask(tr("Unable to prepare image for OCR."));
        return;
    }

    startPaddleOcr();
}

void OcrTaskWidget::startPaddleOcr()
{
    setStatus(tr("Preparing PaddleOCR..."));

    QTemporaryFile imageFile(QDir::tempPath() +
                             QStringLiteral("/flameshot-paddleocr-XXXXXX.png"));
    imageFile.setAutoRemove(false);
    if (!imageFile.open() ||
        !m_capture.toImage().convertToFormat(QImage::Format_RGB32).save(&imageFile,
                                                                         "PNG")) {
        QFile::remove(imageFile.fileName());
        failTask(tr("Unable to create a temporary image for OCR."));
        return;
    }
    imageFile.close();
    m_imagePath = QFileInfo(imageFile.fileName()).absoluteFilePath();
    emit preparedImageReady(m_imagePath);

    QTemporaryFile formulaImageFile(
      QDir::tempPath() + QStringLiteral("/flameshot-paddleocr-formula-XXXXXX.png"));
    formulaImageFile.setAutoRemove(false);
    if (!formulaImageFile.open() ||
        !preparedLatexOcrImage(m_capture).save(&formulaImageFile, "PNG")) {
        QFile::remove(formulaImageFile.fileName());
        failTask(tr("Unable to create a temporary formula image for OCR."));
        return;
    }
    formulaImageFile.close();
    m_formulaImagePath =
      QFileInfo(formulaImageFile.fileName()).absoluteFilePath();

    AbstractLogger::info(AbstractLogger::Stderr)
      << tr("PaddleOCR queued in background: image=%1, formulaImage=%2, "
            "timeout=%3 ms.")
           .arg(m_imagePath,
                m_formulaImagePath,
                QString::number(paddleOcrTimeoutMs()));

    setStatus(tr("Running PaddleOCR..."));
    m_paddleOcrRequestTimedOut = false;
    m_paddleOcrRequestId =
      paddleOcrService()->recognize(m_imagePath,
                                    m_formulaImagePath,
                                    [guard = QPointer<OcrTaskWidget>(this)](
                                      bool ok,
                                      const QString& text,
                                      const QString& latex,
                                      const QString& error) {
                                        if (guard) {
                                            guard
                                              ->handlePaddleOcrServiceFinished(
                                                ok, text, latex, error);
                                        }
                                    });

    const int requestId = m_paddleOcrRequestId;
    QTimer::singleShot(paddleOcrTimeoutMs(), this, [this, requestId]() {
        if (m_paddleOcrRequestId != requestId) {
            return;
        }
        m_paddleOcrRequestTimedOut = true;
        m_lastError = tr("PaddleOCR backend timed out.");
        AbstractLogger::warning(AbstractLogger::Stderr) << m_lastError;
        paddleOcrService()->cancel(requestId);
    });
}

void OcrTaskWidget::startTextOcr()
{
    setStatus(tr("Preparing text OCR..."));

    const QString tesseract =
      QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (tesseract.isEmpty()) {
        failTask(tr("OCR requires the tesseract command to be installed."));
        return;
    }

    QTemporaryFile imageFile(QDir::tempPath() +
                             QStringLiteral("/flameshot-ocr-XXXXXX.png"));
    imageFile.setAutoRemove(false);
    if (!imageFile.open() ||
        !preparedTextOcrImage(m_capture).save(&imageFile, "PNG")) {
        QFile::remove(imageFile.fileName());
        failTask(tr("Unable to create a temporary image for OCR."));
        return;
    }
    imageFile.close();
    m_imagePath = QFileInfo(imageFile.fileName()).absoluteFilePath();
    emit preparedImageReady(m_imagePath);

    m_textLanguageCandidates = ocrLanguageCandidates(tesseract);
    if (m_textLanguageCandidates.size() > 1) {
        m_currentTextCandidate = -1;
        m_textCandidateResults.clear();
        m_textAutoSelectingLanguage = true;
        setStatus(tr("Detecting OCR language..."));
        startNextTextOcrCandidate();
        return;
    }

    startFinalTextOcr(m_textLanguageCandidates.value(0));
}

void OcrTaskWidget::startNextTextOcrCandidate()
{
    ++m_currentTextCandidate;
    if (m_currentTextCandidate >= m_textLanguageCandidates.size()) {
        qreal bestScore = -1000000.0;
        QString bestLanguage;
        const TextOcrCandidateResult* chineseResult = nullptr;
        const TextOcrCandidateResult* mixedResult = nullptr;
        for (const TextOcrCandidateResult& result : m_textCandidateResults) {
            if (!result.ok || result.wordCount <= 0) {
                continue;
            }
            if (result.language == QStringLiteral("chi_sim") &&
                result.chineseCount >= 6 &&
                result.chineseCount >= result.latinCount * 2) {
                chineseResult = &result;
            } else if (result.language.contains(QLatin1Char('+')) &&
                       result.chineseCount >= 6 && result.latinCount >= 2) {
                mixedResult = &result;
            }
        }
        if (chineseResult) {
            if (mixedResult &&
                mixedResult->chineseCount * 10 >=
                  chineseResult->chineseCount * 7 &&
                mixedResult->confidence >= chineseResult->confidence - 5.0) {
                bestLanguage = mixedResult->language;
            } else {
                bestLanguage = chineseResult->language;
            }
        }
        if (bestLanguage.isEmpty()) {
            for (const TextOcrCandidateResult& result : m_textCandidateResults) {
                if (!result.ok || result.wordCount <= 0) {
                    continue;
                }
                qreal score = result.confidence;
                const bool hasChinese = result.chineseCount >= 2;
                const bool hasLatin = result.latinCount >= 2;
                if (result.language == QStringLiteral("eng")) {
                    score += result.latinCount >= result.chineseCount ? 3.0
                                                                      : -10.0;
                } else if (result.language == QStringLiteral("chi_sim")) {
                    score += result.chineseCount >= result.latinCount ? 3.0
                                                                      : -5.0;
                } else if (result.language.contains(QLatin1Char('+'))) {
                    score += hasChinese && hasLatin ? 4.0 : -1.0;
                }
                if (score > bestScore) {
                    bestScore = score;
                    bestLanguage = result.language;
                }
            }
        }

        m_textAutoSelectingLanguage = false;
        if (bestLanguage.isEmpty()) {
            bestLanguage = ocrLanguage(QStandardPaths::findExecutable(
              QStringLiteral("tesseract")));
        }
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("Text OCR language selected: %1.").arg(
               bestLanguage.isEmpty() ? QStringLiteral("default") : bestLanguage);
        startFinalTextOcr(bestLanguage);
        return;
    }

    const QString tesseract =
      QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (tesseract.isEmpty()) {
        failTask(tr("OCR requires the tesseract command to be installed."));
        return;
    }

    const QString language = m_textLanguageCandidates.at(m_currentTextCandidate);
    BackendCommand command;
    command.backendName = QStringLiteral("tesseract-probe:%1").arg(language);
    command.program = tesseract;
    command.arguments << m_imagePath << QStringLiteral("stdout")
                      << QStringLiteral("-l") << language
                      << QStringLiteral("--psm") << ocrPageSegMode()
                      << QStringLiteral("tsv");

    setStatus(tr("Detecting OCR language: %1...").arg(language));
    startProcess(command);
}

void OcrTaskWidget::startFinalTextOcr(const QString& language)
{
    const QString tesseract =
      QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (tesseract.isEmpty()) {
        failTask(tr("OCR requires the tesseract command to be installed."));
        return;
    }

    BackendCommand command;
    command.backendName =
      language.isEmpty() ? QStringLiteral("tesseract")
                         : QStringLiteral("tesseract:%1").arg(language);
    command.program = tesseract;
    command.arguments << m_imagePath << QStringLiteral("stdout");
    if (!language.isEmpty()) {
        command.arguments << QStringLiteral("-l") << language;
    }
    command.arguments << QStringLiteral("--psm") << ocrPageSegMode();

    setStatus(tr("Running text OCR..."));
    startProcess(command);
}

void OcrTaskWidget::startLatexOcr()
{
    setStatus(tr("Preparing LaTeX OCR..."));

    QTemporaryFile imageFile(QDir::tempPath() +
                             QStringLiteral("/flameshot-latex-ocr-XXXXXX.png"));
    imageFile.setAutoRemove(false);
    if (!imageFile.open() ||
        !preparedLatexOcrImage(m_capture).save(&imageFile, "PNG")) {
        QFile::remove(imageFile.fileName());
        failTask(tr("Unable to create a temporary image for LaTeX OCR."));
        return;
    }
    imageFile.close();
    m_imagePath = QFileInfo(imageFile.fileName()).absoluteFilePath();
    emit preparedImageReady(m_imagePath);

    if (!prepareLatexOcrCommands(m_imagePath, &m_latexCommands)) {
        failTask(tr("LaTeX OCR requires texteller, pix2text, pix2tex, or a "
                    "custom FLAMESHOT_LATEX_OCR_COMMAND."));
        return;
    }

    AbstractLogger::info(AbstractLogger::Stderr)
      << tr("LaTeX OCR queued in background: image=%1, backends=%2, timeout=%3 ms.")
           .arg(m_imagePath,
                backendNames(m_latexCommands),
                QString::number(latexOcrTimeoutMs()));

    startNextLatexBackend();
}

void OcrTaskWidget::startNextLatexBackend()
{
    ++m_currentBackend;
    if (m_currentBackend >= m_latexCommands.size()) {
        failTask(m_lastError.isEmpty() ? tr("No LaTeX was recognized.")
                                       : m_lastError);
        return;
    }

    const BackendCommand& command = m_latexCommands.at(m_currentBackend);
    setStatus(tr("Running LaTeX OCR backend: %1...").arg(command.backendName));
    startProcess(command);
}

void OcrTaskWidget::startProcess(const BackendCommand& command)
{
    cleanupProcess();
    m_processErrorOutput.clear();
    m_textellerRequestTimedOut = false;
    if (command.backendName == QStringLiteral("texteller") &&
        textellerServiceEnabled()) {
        const QString backendName = command.backendName;
        setStatus(tr("Running LaTeX OCR backend: %1...").arg(backendName));
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("OCR backend queued on Texteller worker: image=%1.")
               .arg(m_imagePath);
        m_textellerRequestId =
          textellerService()->recognize(m_imagePath,
                                        [guard = QPointer<OcrTaskWidget>(this),
                                         backendName](bool ok,
                                                      const QString& result) {
                                            if (guard) {
                                                guard
                                                  ->handleTextellerServiceFinished(
                                                    backendName, ok, result);
                                            }
                                        });

        const int requestId = m_textellerRequestId;
        QTimer::singleShot(latexOcrTimeoutMs(), this, [this, requestId, backendName]() {
            if (m_textellerRequestId != requestId) {
                return;
            }
            m_textellerRequestTimedOut = true;
            m_lastError = tr("%1 backend timed out.").arg(backendName);
            AbstractLogger::warning(AbstractLogger::Stderr)
              << tr("OCR backend timed out: backend=%1.").arg(backendName);
            textellerService()->cancel(requestId);
        });
        return;
    }

    m_process = new QProcess(this);
    m_process->setProperty("backendName", command.backendName);
    m_process->setProcessEnvironment(ocrProcessEnvironment());

    connect(m_process, &QProcess::started, this, [this]() {
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("OCR backend started: backend=%1, pid=%2.")
               .arg(m_process->property("backendName").toString(),
                    QString::number(m_process->processId()));
    });
    connect(m_process,
            &QProcess::errorOccurred,
            this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart && m_process) {
                    handleProcessFailedToStart(m_process);
                }
            });
    connect(m_process,
            &QProcess::finished,
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (m_process) {
                    handleProcessFinished(m_process, exitCode, exitStatus);
                }
            });
    connect(m_process,
            &QProcess::readyReadStandardError,
            this,
            &OcrTaskWidget::drainProcessStandardError);

    const int timeout =
      m_kind == Kind::Latex ? latexOcrTimeoutMs() : textOcrTimeoutMs();
    QTimer::singleShot(timeout, m_process, [this]() {
        if (!m_process || m_process->state() == QProcess::NotRunning) {
            return;
        }
        const QString backendName = m_process->property("backendName").toString();
        m_process->setProperty("flameshotTimedOut", true);
        m_lastError = tr("%1 backend timed out.").arg(backendName);
        AbstractLogger::warning(AbstractLogger::Stderr)
          << tr("OCR backend timed out: backend=%1.").arg(backendName);
        m_process->kill();
    });

    AbstractLogger::info(AbstractLogger::Stderr)
      << tr("OCR backend launching: backend=%1, program=%2.")
           .arg(command.backendName, command.program);
    m_process->start(command.program, command.arguments);
}

void OcrTaskWidget::handleTextellerServiceFinished(const QString& backendName,
                                                   bool ok,
                                                   const QString& result)
{
    if (m_textellerRequestId == 0) {
        return;
    }

    const bool timedOut = m_textellerRequestTimedOut;
    m_textellerRequestId = 0;
    m_textellerRequestTimedOut = false;

    if (m_cancelled) {
        emit cancelled();
        close();
        return;
    }

    if (!ok || timedOut) {
        m_lastError =
          timedOut ? tr("%1 backend timed out.").arg(backendName)
                   : tr("%1 backend failed: %2").arg(backendName, result);
        AbstractLogger::warning(AbstractLogger::Stderr)
          << tr("OCR backend failed: backend=%1, error=%2.")
               .arg(backendName, m_lastError);
        if (m_kind == Kind::Latex) {
            startNextLatexBackend();
        } else {
            failTask(m_lastError);
        }
        return;
    }

    const QString output = result.trimmed();
    if (m_kind == Kind::Text) {
        completeTextOcr(output);
    } else if (output.isEmpty()) {
        m_lastError = tr("%1 backend returned no LaTeX.").arg(backendName);
        AbstractLogger::warning(AbstractLogger::Stderr) << m_lastError;
        startNextLatexBackend();
    } else {
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("LaTeX OCR backend succeeded: backend=%1, chars=%2, latex=%3")
               .arg(backendName,
                    QString::number(output.size()),
                    latexPreview(output));
        completeLatexOcr(output);
    }
}

void OcrTaskWidget::handlePaddleOcrServiceFinished(bool ok,
                                                   const QString& text,
                                                   const QString& latex,
                                                   const QString& error)
{
    if (m_paddleOcrRequestId == 0) {
        return;
    }

    const bool timedOut = m_paddleOcrRequestTimedOut;
    m_paddleOcrRequestId = 0;
    m_paddleOcrRequestTimedOut = false;

    if (m_cancelled) {
        emit cancelled();
        close();
        return;
    }

    if (!ok || timedOut) {
        m_lastError =
          timedOut ? tr("PaddleOCR backend timed out.")
                   : tr("PaddleOCR backend failed: %1").arg(error);
        AbstractLogger::warning(AbstractLogger::Stderr) << m_lastError;
        failTask(m_lastError);
        return;
    }

    completePaddleOcr(text.trimmed(), latex.trimmed());
}

void OcrTaskWidget::handleTextOcrProbeFinished(const QString& output,
                                               bool ok,
                                               const QString& error)
{
    TextOcrCandidateResult result;
    result.ok = ok;
    result.error = error;
    if (m_currentTextCandidate >= 0 &&
        m_currentTextCandidate < m_textLanguageCandidates.size()) {
        result.language = m_textLanguageCandidates.at(m_currentTextCandidate);
    }

    if (ok) {
        const QStringList lines =
          output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (!lines.isEmpty()) {
            const QStringList headers =
              lines.first().split(QLatin1Char('\t'), Qt::KeepEmptyParts);
            const int confIndex = headers.indexOf(QStringLiteral("conf"));
            const int textIndex = headers.indexOf(QStringLiteral("text"));
            qreal confidenceSum = 0.0;
            QStringList words;
            for (int i = 1; i < lines.size(); ++i) {
                const QStringList columns =
                  lines.at(i).split(QLatin1Char('\t'), Qt::KeepEmptyParts);
                if (confIndex < 0 || textIndex < 0 ||
                    confIndex >= columns.size() || textIndex >= columns.size()) {
                    continue;
                }
                bool confidenceOk = false;
                const qreal confidence = columns.at(confIndex).toDouble(&confidenceOk);
                if (!confidenceOk || confidence < 0.0) {
                    continue;
                }
                const QString text = columns.at(textIndex).trimmed();
                if (text.isEmpty()) {
                    continue;
                }
                confidenceSum += confidence;
                ++result.wordCount;
                words << text;
                for (QChar ch : text) {
                    if (isChineseCodepoint(ch)) {
                        ++result.chineseCount;
                    } else if (ch.isLetter() && ch.unicode() <= 0x024f) {
                        ++result.latinCount;
                    }
                }
            }
            if (result.wordCount > 0) {
                result.confidence = confidenceSum / result.wordCount;
                result.text = words.join(QLatin1Char(' '));
            }
        }
    }

    AbstractLogger::info(AbstractLogger::Stderr)
      << tr("Text OCR language probe: language=%1, ok=%2, confidence=%3, "
            "words=%4, chinese=%5, latin=%6, text=%7")
           .arg(result.language,
                result.ok ? QStringLiteral("true") : QStringLiteral("false"),
                QString::number(result.confidence, 'f', 1),
                QString::number(result.wordCount),
                QString::number(result.chineseCount),
                QString::number(result.latinCount),
                ocrTextPreview(result.text));
    m_textCandidateResults.append(result);
    startNextTextOcrCandidate();
}

void OcrTaskWidget::handleProcessFinished(QProcess* process,
                                          int exitCode,
                                          QProcess::ExitStatus exitStatus)
{
    if (process != m_process) {
        process->deleteLater();
        return;
    }

    const QString backendName = process->property("backendName").toString();
    const bool timedOut = process->property("flameshotTimedOut").toBool();
    drainProcessStandardError();
    if (timedOut) {
        const QString error = m_lastError;
        cleanupProcess();
        if (m_kind == Kind::Text && m_textAutoSelectingLanguage &&
            !m_cancelled) {
            handleTextOcrProbeFinished(QString(), false, error);
            return;
        }
        if (m_kind == Kind::Latex && !m_cancelled) {
            startNextLatexBackend();
        }
        return;
    }

    if (m_cancelled) {
        cleanupProcess();
        emit cancelled();
        close();
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        m_lastError =
          processErrorMessage(tr("%1 backend failed").arg(backendName),
                              m_processErrorOutput);
        AbstractLogger::warning(AbstractLogger::Stderr)
          << tr("OCR backend failed: backend=%1, exitCode=%2, error=%3.")
               .arg(backendName, QString::number(exitCode), m_lastError);
        const QString error = m_lastError;
        cleanupProcess();
        if (m_kind == Kind::Text && m_textAutoSelectingLanguage) {
            handleTextOcrProbeFinished(QString(), false, error);
            return;
        }
        if (m_kind == Kind::Latex) {
            startNextLatexBackend();
        } else {
            failTask(m_lastError);
        }
        return;
    }

    QString output = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
    cleanupProcess();

    if (m_kind == Kind::Text && m_textAutoSelectingLanguage) {
        handleTextOcrProbeFinished(output, true, QString());
        return;
    }

    if (m_kind == Kind::Text) {
        output.remove(QChar::FormFeed);
        completeTextOcr(output.trimmed());
    } else if (output.isEmpty()) {
        m_lastError = tr("%1 backend returned no LaTeX.").arg(backendName);
        AbstractLogger::warning(AbstractLogger::Stderr) << m_lastError;
        startNextLatexBackend();
    } else {
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("LaTeX OCR backend succeeded: backend=%1, chars=%2, latex=%3")
               .arg(backendName,
                    QString::number(output.size()),
                    latexPreview(output));
        completeLatexOcr(output);
    }
}

void OcrTaskWidget::drainProcessStandardError()
{
    if (!m_process) {
        return;
    }

    const QString chunk =
      QString::fromLocal8Bit(m_process->readAllStandardError());
    if (chunk.isEmpty()) {
        return;
    }

    m_processErrorOutput += chunk;
    const QString backendName = m_process->property("backendName").toString();
    const QStringList lines =
      chunk.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("OCR backend stderr: backend=%1, %2")
               .arg(backendName, line.left(500));
    }
}

void OcrTaskWidget::handleProcessFailedToStart(QProcess* process)
{
    if (process != m_process) {
        process->deleteLater();
        return;
    }

    const QString backendName = process->property("backendName").toString();
    m_lastError =
      tr("%1 backend did not start: %2").arg(backendName, process->errorString());
    AbstractLogger::warning(AbstractLogger::Stderr) << m_lastError;
    const QString error = m_lastError;
    cleanupProcess();
    if (m_kind == Kind::Text && m_textAutoSelectingLanguage) {
        handleTextOcrProbeFinished(QString(), false, error);
        return;
    }
    if (m_kind == Kind::Latex) {
        startNextLatexBackend();
    } else {
        failTask(m_lastError);
    }
}

void OcrTaskWidget::cancelTask()
{
    m_cancelled = true;
    setStatus(tr("Cancelling OCR task..."));
    AbstractLogger::info(AbstractLogger::Stderr) << tr("OCR task cancelled.");
    if (m_paddleOcrRequestId != 0) {
        paddleOcrService()->cancel(m_paddleOcrRequestId);
        return;
    }
    if (m_textellerRequestId != 0) {
        textellerService()->cancel(m_textellerRequestId);
        return;
    }
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        return;
    }
    emit cancelled();
    close();
}

void OcrTaskWidget::failTask(const QString& error)
{
    setStatus(error);
    AbstractLogger::error() << error;
    emit failed(error);
    close();
}

void OcrTaskWidget::completeTextOcr(const QString& text)
{
    cleanupImage();
    if (text.isEmpty()) {
        AbstractLogger::warning() << tr("No text was recognized.");
    } else {
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("Text OCR backend succeeded: chars=%1, text=%2")
               .arg(QString::number(text.size()), ocrTextPreview(text));
    }
    emit textCompleted(text);
    close();
}

void OcrTaskWidget::completeLatexOcr(const QString& latex)
{
    cleanupImage();
    emit latexCompleted(m_capture, latex);
    close();
}

void OcrTaskWidget::completePaddleOcr(const QString& text, const QString& latex)
{
    cleanupImage();
    if (text.isEmpty() && latex.isEmpty()) {
        AbstractLogger::warning() << tr("No text or formula was recognized.");
    } else {
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("PaddleOCR backend succeeded: textChars=%1, latexChars=%2, "
                "text=%3, latex=%4")
               .arg(QString::number(text.size()),
                    QString::number(latex.size()),
                    ocrTextPreview(text),
                    latexPreview(latex));
    }
    emit ocrCompleted(m_capture, text, latex);
    close();
}

void OcrTaskWidget::cleanupProcess()
{
    if (m_paddleOcrRequestId != 0) {
        const int requestId = m_paddleOcrRequestId;
        m_paddleOcrRequestId = 0;
        paddleOcrService()->cancel(requestId);
    }

    if (m_textellerRequestId != 0) {
        const int requestId = m_textellerRequestId;
        m_textellerRequestId = 0;
        textellerService()->cancel(requestId);
    }

    if (!m_process) {
        return;
    }

    m_process->disconnect(this);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    m_process->deleteLater();
    m_process = nullptr;
}

void OcrTaskWidget::cleanupImage()
{
    if (m_imagePath.isEmpty() && m_formulaImagePath.isEmpty()) {
        return;
    }

    if (keepOcrTempImage()) {
        if (!m_imagePath.isEmpty()) {
            AbstractLogger::info(AbstractLogger::Stderr)
              << tr("Keeping OCR temporary image: %1").arg(m_imagePath);
        }
        if (!m_formulaImagePath.isEmpty()) {
            AbstractLogger::info(AbstractLogger::Stderr)
              << tr("Keeping OCR temporary image: %1").arg(m_formulaImagePath);
        }
        m_imagePath.clear();
        m_formulaImagePath.clear();
        return;
    }

    if (!m_imagePath.isEmpty()) {
        QFile::remove(m_imagePath);
    }
    if (!m_formulaImagePath.isEmpty()) {
        QFile::remove(m_formulaImagePath);
    }
    m_imagePath.clear();
    m_formulaImagePath.clear();
}

void OcrTaskWidget::setStatus(const QString& status)
{
    emit statusChanged(status);
}
