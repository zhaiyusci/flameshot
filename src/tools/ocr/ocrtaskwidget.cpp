// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "ocrtaskwidget.h"

#include "tools/barcode/barcodereader.h"
#include "tools/ocr/ocrpreprocessor.h"
#include "utils/abstractlogger.h"
#include "utils/confighandler.h"

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
#include <QMetaObject>
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
    QString backend = QProcessEnvironment::systemEnvironment()
                        .value(QStringLiteral("FLAMESHOT_LATEX_OCR_BACKEND"),
                               QStringLiteral("auto"))
                        .trimmed()
                        .toLower();
    backend.replace(QLatin1Char('-'), QLatin1Char('_'));
    return backend.isEmpty() ? QStringLiteral("auto") : backend;
}

QString configuredOcrBackend()
{
    QString backend = QProcessEnvironment::systemEnvironment()
                        .value(QStringLiteral("FLAMESHOT_OCR_BACKEND"))
                        .trimmed()
                        .toLower();
    if (backend.isEmpty()) {
        backend = ConfigHandler().ocrBackend().trimmed().toLower();
    }
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
    const int timeout = QProcessEnvironment::systemEnvironment()
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
    return enabled != QStringLiteral("0") &&
           enabled != QStringLiteral("false") &&
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

int markerOcrTimeoutMs()
{
    bool ok = false;
    const int timeout =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_TIMEOUT_MS"))
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

int markerOcrIdleTimeoutMs()
{
    bool ok = false;
    const int timeout =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_IDLE_TIMEOUT_MS"))
        .toInt(&ok);
    return ok && timeout > 0 ? timeout : 30 * 60 * 1000;
}

int markerOcrThreads()
{
    bool ok = false;
    const int configured =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_THREADS"))
        .toInt(&ok);
    int threads = ok ? configured : ConfigHandler().markerOcrThreads();
    return std::max(1, std::min(128, threads));
}

int markerOcrParallelThreads()
{
    bool ok = false;
    const int configured =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_PARALLEL_THREADS"))
        .toInt(&ok);
    int threads = ok ? configured : ConfigHandler().markerOcrParallelThreads();
    return std::max(1, std::min(128, threads));
}

bool markerOcrParallelSmallImages()
{
    const QString configured =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_PARALLEL_SMALL_IMAGES"))
        .trimmed()
        .toLower();
    if (!configured.isEmpty()) {
        return configured != QStringLiteral("0") &&
               configured != QStringLiteral("false") &&
               configured != QStringLiteral("no") &&
               configured != QStringLiteral("off");
    }
    return ConfigHandler().markerOcrParallelSmallImages();
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
    QString mode = QProcessEnvironment::systemEnvironment()
                     .value(QStringLiteral("FLAMESHOT_LATEX_OCR_INVERT"),
                            QStringLiteral("auto"))
                     .trimmed()
                     .toLower();
    return mode.isEmpty() ? QStringLiteral("auto") : mode;
}

QString latexOcrPreprocessMode()
{
    QString mode = QProcessEnvironment::systemEnvironment()
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
    QString mode =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_OCR_INVERT"), QStringLiteral("auto"))
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
    const QString keep = QProcessEnvironment::systemEnvironment()
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

    const int target = std::max(1, std::min(total, qRound(total * percentile)));
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
            targetLine[x] =
              static_cast<uchar>(std::max(0, std::min(255, value)));
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
        threshold =
          std::max(24, histogramPercentile(histogram, nonZeroCount, 0.75));
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

    QRect cropRect =
      crop ? foregroundBounds(foregroundStrength, 8) : blackOnWhite.rect();
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
        working = working.scaled(
          scaledSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
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
    const bool invert =
      !useBackgroundNormalization && shouldInvertTextOcrImage(working);

    int threshold = -1;
    int edgeThreshold = -1;
    ForegroundNormalizationInfo foregroundInfo;
    QImage image;
    if (useBackgroundNormalization) {
        image =
          foregroundNormalizedImage(working, padding, true, &foregroundInfo);
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
                prepared =
                  edgeReinforcedImage(binary,
                                      contrast,
                                      mode == QStringLiteral("outline") ||
                                        mode == QStringLiteral("edge"),
                                      &edgeThreshold);
            }
        }

        image = borderedTextOcrImage(prepared, padding);
    }

    AbstractLogger::info(AbstractLogger::Stderr)
      << QObject::tr(
           "Text OCR preprocessing: mode=%1, backgroundNormalized=%2, "
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
        working = working.scaled(
          scaledSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    const QString mode = latexOcrPreprocessMode();
    const bool foregroundMode = mode == QStringLiteral("normalize") ||
                                mode == QStringLiteral("auto") ||
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
      << QObject::tr(
           "LaTeX OCR preprocessing: mode=%1, foregroundNormalized=%2, "
           "backgroundRgb=%3/%4/%5, backgroundLuma=%6, "
           "backgroundSpread90=%7, invert=%8, scale=%9, "
           "foregroundLow=%10, foregroundHigh=%11, size=%12x%13.")
           .arg(mode,
                foregroundMode ? QStringLiteral("true")
                               : QStringLiteral("false"),
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

    const QString configuredInRc = ConfigHandler().paddleOcrPython().trimmed();
    if (!configuredInRc.isEmpty()) {
        const QString executable = executablePathIfUsable(configuredInRc);
        if (!executable.isEmpty()) {
            return executable;
        }
        return configuredInRc;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QDir currentDir(QDir::currentPath());
    const QStringList candidates = {
        appDir.absoluteFilePath(
          QStringLiteral("../../../.venv-paddleocr/bin/python")),
        currentDir.absoluteFilePath(
          QStringLiteral("../.venv-paddleocr/bin/python")),
        currentDir.absoluteFilePath(
          QStringLiteral(".venv-paddleocr/bin/python")),
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

    const QString python3 =
      QStandardPaths::findExecutable(QStringLiteral("python3"));
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

    const QString configuredInRc = ConfigHandler().paddleOcrCache().trimmed();
    if (!configuredInRc.isEmpty()) {
        return configuredInRc;
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

QString markerExecutable()
{
    return firstExistingExecutable(
      QStringLiteral("marker_single"),
      { QDir::homePath() +
          QStringLiteral(
            "/.local/share/flameshot-ocr-backends/marker-py311/bin/"
            "marker_single"),
        QDir::homePath() +
          QStringLiteral(
            "/.local/share/flameshot-ocr-backends/marker/bin/"
            "marker_single") });
}

QString markerOcrPython()
{
    const QString configured =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_PYTHON"))
        .trimmed();
    if (!configured.isEmpty()) {
        const QString executable = executablePathIfUsable(configured);
        if (!executable.isEmpty()) {
            return executable;
        }
        return configured;
    }

    const QString configuredInRc = ConfigHandler().markerOcrPython().trimmed();
    if (!configuredInRc.isEmpty()) {
        const QString executable = executablePathIfUsable(configuredInRc);
        if (!executable.isEmpty()) {
            return executable;
        }
        return configuredInRc;
    }

    const QString shebangPython = pythonFromScriptShebang(markerExecutable());
    if (!shebangPython.isEmpty()) {
        return shebangPython;
    }

    return {};
}

QString markerOcrCacheHome()
{
    const QString configured =
      QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("FLAMESHOT_MARKER_OCR_CACHE"))
        .trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }

    const QString configuredInRc = ConfigHandler().markerOcrCache().trimmed();
    if (!configuredInRc.isEmpty()) {
        return configuredInRc;
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QDir currentDir(QDir::currentPath());
    const QStringList candidates = {
        appDir.absoluteFilePath(QStringLiteral("../../../.cache/datalab/models")),
        currentDir.absoluteFilePath(QStringLiteral("../.cache/datalab/models")),
        currentDir.absoluteFilePath(QStringLiteral(".cache/datalab/models")),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo(candidate).isDir()) {
            return QFileInfo(candidate).absoluteFilePath();
        }
    }

    return QDir::home().filePath(
      QStringLiteral(".cache/flameshot/datalab/models"));
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
            FORMULA_WORKER.stop()
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
    to_markdown = getattr(result, "_to_markdown", None)
    if callable(to_markdown):
        try:
            markdown = to_markdown(pretty=False, show_formula_number=False)
            if isinstance(markdown, dict):
                return markdown
        except TypeError:
            try:
                markdown = to_markdown()
                if isinstance(markdown, dict):
                    return markdown
            except Exception as error:
                log("to_markdown failed: " + str(error))
        except Exception as error:
            log("to_markdown failed: " + str(error))
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
    "display_formula",
    "formula",
    "formula_number",
    "inline_formula",
    "equation",
}

def normalize_markdown(text):
    return str(text or "").strip()

def markdown_formula_marker_count(text):
    text = str(text or "")
    return len(
        re.findall(
            r"\$\$[\s\S]+?\$\$|\\\[[\s\S]+?\\\]|\\\([\s\S]+?\\\)|"
            r"(?<!\$)\$[^$\n]{1,300}\$(?!\$)",
            text,
        )
    )

def average_score(scores):
    valid = []
    for score in scores or []:
        try:
            value = float(score)
        except Exception:
            continue
        if 0.0 <= value <= 1.0:
            valid.append(value)
    if not valid:
        return 0.0
    return sum(valid) / len(valid)

def structure_markdown(pipeline, outputs):
    markdown_list = []
    visual_block_count = 0
    formula_block_count = 0
    layout_scores = []
    ocr_scores = []
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
        for box in (data.get("layout_det_res") or {}).get("boxes") or []:
            if "score" in box:
                layout_scores.append(box.get("score"))
        ocr_res = data.get("overall_ocr_res") or {}
        rec_texts = ocr_res.get("rec_texts") or []
        rec_scores = ocr_res.get("rec_scores") or []
        for index, text in enumerate(rec_texts):
            if str(text).strip() and index < len(rec_scores):
                ocr_scores.append(rec_scores[index])

    markdown = ""
    if markdown_list:
        try:
            combined = pipeline.concatenate_markdown_pages(markdown_list)
            markdown = markdown_text(combined)
        except Exception as error:
            log("concatenate markdown failed: " + str(error))
        if not markdown:
            markdown = "\n\n".join(markdown_text(item) for item in markdown_list)

    markdown = normalize_markdown(markdown)
    formula_marker_count = markdown_formula_marker_count(markdown)
    non_text_block_count = visual_block_count + formula_block_count
    score = max(average_score(ocr_scores), average_score(layout_scores))
    if formula_block_count > 0 or formula_marker_count > 0:
        score = max(score, 1.0)
    if visual_block_count > 0 and formula_block_count == 0 and formula_marker_count == 0:
        score *= 0.5
    if not markdown:
        score = 0.0
    log(
        "structure markdown: chars="
        + str(len(markdown))
        + ", formulaBlocks="
        + str(formula_block_count)
        + ", mathMarkers="
        + str(formula_marker_count)
        + ", score="
        + ("%.3f" % score)
    )
    return markdown, non_text_block_count, score

def text_content(outputs):
    parts = []
    scores = []
    for result in outputs or []:
        data = result_dict(result)
        texts = data.get("rec_texts") or []
        rec_scores = data.get("rec_scores") or []
        if isinstance(texts, list):
            for index, item in enumerate(texts):
                text = str(item).strip()
                if not text:
                    continue
                parts.append(text)
                if index < len(rec_scores):
                    scores.append(rec_scores[index])
        elif str(texts).strip():
            parts.append(str(texts))
    return "\n".join(parts).strip(), average_score(scores)

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

def result_page(label, text="", latex="", score=0.0, error=""):
    return {
        "label": label,
        "text": str(text or "").strip(),
        "latex": str(latex or "").strip(),
        "score": float(score or 0.0),
        "error": str(error or "").strip(),
    }

def page_has_content(page):
    return bool(page.get("text") or page.get("latex"))

def combined_page_text(page):
    text = page.get("text") or ""
    latex = page.get("latex") or ""
    if text and latex:
        return text + "\n\nLaTeX:\n" + latex
    return text or latex

def route_label(page):
    label = page.get("label") or "OCR"
    error = page.get("error") or ""
    if error:
        return label + " failed"
    return label

def choose_pages(pages):
    usable = [page for page in pages if page_has_content(page)]
    if not usable:
        return result_page("OCR"), result_page("Fallback")
    usable.sort(
        key=lambda page: (
            page.get("score", 0.0),
            len(combined_page_text(page)),
        ),
        reverse=True,
    )
    primary = usable[0]
    fallback = usable[1] if len(usable) > 1 else result_page("Fallback")
    return primary, fallback

def error_page(label, error):
    return result_page(label, text=label + " failed:\n" + str(error), score=0.0)

def formula_markdown_from_latex(latex):
    latex = str(latex or "").strip()
    if not latex:
        return ""
    return "$$\n" + latex + "\n$$"

def recognize(image_path, formula_image_path):
    use_structure = truthy_env("FLAMESHOT_PADDLEOCR_STRUCTURE", True)
    structure_page = result_page(
        "Structure Markdown",
        text="Structure Markdown failed:\nPPStructureV3 is disabled.",
    )
    formula_page = result_page("Formula OCR", text="No formula was recognized.")
    text_page = result_page("Text OCR", text="No text was recognized.")

    try:
        log("text predict begin: " + image_path)
        text, text_score = text_content(init_text_pipeline().predict(input=image_path))
        log("text predict done")
        text_page = result_page(
            "Text OCR",
            text=text if text else "No text was recognized.",
            score=text_score,
        )
    except Exception as error:
        log("text predict failed: " + str(error))
        text_page = error_page("Text OCR", error)

    if use_structure:
        try:
            log("structure predict begin: " + image_path)
            pipeline = init_structure_pipeline()
            structure_text, structure_non_text_count, structure_score = structure_markdown(
                pipeline,
                pipeline.predict(input=image_path)
            )
            _ = structure_non_text_count
            log("structure predict done")
            structure_page = result_page(
                "Structure Markdown",
                text=structure_text if structure_text else "No structure Markdown was recognized.",
                score=structure_score,
            )
        except Exception as error:
            log("structure predict failed: " + str(error))
            structure_page = error_page("Structure Markdown", error)

    try:
        if formula_image_path:
            log("formula predict begin: " + formula_image_path)
            latex = formula_content(
                init_formula_pipeline().predict(input=formula_image_path)
            )
            log("formula predict done")
            formula_page = result_page(
                "Formula OCR",
                text=formula_markdown_from_latex(latex) or "No formula was recognized.",
                score=1.0 if latex else 0.0,
            )
    except Exception as error:
        log("formula predict failed: " + str(error))
        formula_page = error_page("Formula OCR", error)

    return structure_page, formula_page, text_page

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
        structure_page, formula_page, text_page = recognize(image_path, formula_image_path)
        send({
            "type": "result",
            "id": request_id,
            "ok": True,
            "text": structure_page.get("text", ""),
            "latex": structure_page.get("latex", ""),
            "result_info": route_label(structure_page),
            "fallback_text": formula_page.get("text", ""),
            "fallback_latex": formula_page.get("latex", ""),
            "fallback_info": route_label(formula_page),
            "extra_text": text_page.get("text", ""),
            "extra_latex": text_page.get("latex", ""),
            "extra_info": route_label(text_page),
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

QString markerOcrServicePythonScript()
{
    return QStringLiteral(R"PY(
import json
import os
import re
import subprocess
import sys
import time
import traceback
from concurrent.futures import ThreadPoolExecutor

os.environ.setdefault("GRPC_VERBOSITY", "ERROR")
os.environ.setdefault("GLOG_minloglevel", "2")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")

def log(message):
    print("flameshot-marker-worker: " + str(message), file=sys.stderr, flush=True)

def send(payload):
    print(json.dumps(payload, ensure_ascii=False), flush=True)

def truthy_env(name, default):
    value = os.environ.get(name)
    if value is None:
        return default
    return value.strip().lower() in ("1", "true", "yes", "on")

def result_page(label, text="", latex="", score=0.0, error=""):
    return {
        "label": label,
        "text": str(text or "").strip(),
        "latex": str(latex or "").strip(),
        "score": float(score or 0.0),
        "error": str(error or "").strip(),
    }

def page_has_content(page):
    return bool(page.get("text") or page.get("latex"))

def route_label(page):
    label = page.get("label") or "OCR"
    error = page.get("error") or ""
    if error:
        return label + " failed"
    return label

def error_page(label, error):
    return result_page(label, text=label + " failed:\n" + str(error), score=0.0, error=str(error))

def math_marker_count(text):
    text = str(text or "")
    return len(
        re.findall(
            r"\$\$[\s\S]+?\$\$|\\\[[\s\S]+?\\\]|\\\([\s\S]+?\\\)|"
            r"(?<!\$)\$[^$\n]{1,500}\$(?!\$)",
            text,
        )
    )

def has_markdown_table(text):
    text = str(text or "")
    return bool(re.search(r"^\|.*\|\s*$\n^\|[-:\s|]+\|\s*$", text, re.M))

def score_markdown(text, label):
    text = str(text or "").strip()
    if not text:
        return 0.0
    score = min(1.0, len(text) / 80.0)
    markers = math_marker_count(text)
    if markers:
        score += 0.4
    if has_markdown_table(text):
        score -= 0.7
    if label == "Marker Formula" and markers:
        score += 0.3
    return max(0.0, score)

def combined_page_text(page):
    text = page.get("text") or ""
    latex = page.get("latex") or ""
    if text and latex:
        return text + "\n\nLaTeX:\n" + latex
    return text or latex

def choose_pages(pages):
    usable = [page for page in pages if page_has_content(page)]
    if not usable:
        return result_page("OCR"), result_page("Fallback"), result_page("Text OCR")
    usable.sort(
        key=lambda page: (
            page.get("score", 0.0),
            len(combined_page_text(page)),
        ),
        reverse=True,
    )
    primary = usable[0]
    fallback = usable[1] if len(usable) > 1 else result_page("Fallback")
    extra = usable[2] if len(usable) > 2 else result_page("Text OCR")
    return primary, fallback, extra

def image_size(image_path):
    try:
        from PIL import Image
        with Image.open(image_path) as image:
            return image.size
    except Exception as error:
        log("image size probe failed: " + str(error))
        return None

def is_small_fallback_image(size):
    if not size:
        return False
    width, height = size
    return height <= 280 or width * height <= 180000

def should_run_formula_fallback(image_path, size=None):
    mode = os.environ.get("FLAMESHOT_MARKER_OCR_FORMULA_FALLBACK", "off").strip().lower()
    if mode in ("0", "false", "no", "off", "never"):
        return False
    if mode in ("1", "true", "yes", "on", "always"):
        return True
    return is_small_fallback_image(size if size is not None else image_size(image_path))

try:
    import torch
    marker_threads = int(os.environ.get("FLAMESHOT_MARKER_OCR_THREADS", "8"))
    marker_threads = max(1, marker_threads)
    marker_parallel_threads = int(
        os.environ.get(
            "FLAMESHOT_MARKER_OCR_PARALLEL_THREADS",
            str(max(1, marker_threads // 2)),
        )
    )
    marker_parallel_threads = max(1, marker_parallel_threads)
    torch.set_num_threads(marker_threads)
    try:
        torch.set_num_interop_threads(1)
    except RuntimeError:
        pass
    log("torch threads configured: num_threads=" + str(torch.get_num_threads()))
except Exception as error:
    torch = None
    marker_threads = int(os.environ.get("FLAMESHOT_MARKER_OCR_THREADS", "8") or "8")
    marker_parallel_threads = max(1, marker_threads // 2)
    log("torch thread configuration failed: " + str(error))

MARKER_PARALLEL_SMALL_IMAGES = truthy_env(
    "FLAMESHOT_MARKER_OCR_PARALLEL_SMALL_IMAGES", False
)

log("import marker")
from marker.converters.pdf import PdfConverter
from marker.models import create_model_dict
from marker.output import text_from_rendered

RENDERER = "marker.renderers.markdown.MarkdownRenderer"

log("load models begin")
MODELS = create_model_dict()
log("load models done")
send({"type": "ready"})

def convert_marker(image_path, label, force_layout_block=None):
    config = {
        "pdftext_workers": 1,
        "disable_tqdm": True,
        "extract_images": False,
    }
    if force_layout_block:
        config["force_layout_block"] = force_layout_block
    converter = PdfConverter(
        config=config,
        artifact_dict=MODELS.copy(),
        renderer=RENDERER,
    )
    start = time.time()
    rendered = converter(image_path)
    markdown, _, _ = text_from_rendered(rendered)
    elapsed = time.time() - start
    markdown = str(markdown or "").strip()
    log(label + " done: seconds=%.2f, chars=%d, math=%d" % (
        elapsed,
        len(markdown),
        math_marker_count(markdown),
    ))
    return result_page(
        label,
        text=markdown,
        score=score_markdown(markdown, label),
    )

def set_torch_threads(threads, reason):
    if torch is None:
        return None
    previous = torch.get_num_threads()
    if previous != threads:
        torch.set_num_threads(threads)
        log(reason + ": torch num_threads=" + str(torch.get_num_threads()))
    return previous

def restore_torch_threads(previous, reason):
    if torch is None or previous is None:
        return
    if torch.get_num_threads() != previous:
        torch.set_num_threads(previous)
        log(reason + ": torch num_threads=" + str(torch.get_num_threads()))

def convert_marker_safely(image_path, label, force_layout_block=None):
    try:
        return convert_marker(image_path, label, force_layout_block)
    except Exception as error:
        log(label + " failed: " + str(error))
        return error_page(label, error)

ROUTE_WORKER_SCRIPT = r'''
import json
import os
import re
import sys
import threading
import time
import traceback

os.environ.setdefault("GRPC_VERBOSITY", "ERROR")
os.environ.setdefault("GLOG_minloglevel", "2")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")

def log(message):
    print("flameshot-marker-route-worker: " + str(message), file=sys.stderr, flush=True)

def send(payload):
    print(json.dumps(payload, ensure_ascii=False), flush=True)

def start_parent_monitor():
    try:
        parent_pid = int(os.environ.get("FLAMESHOT_MARKER_PARENT_PID", "0"))
    except Exception:
        return
    if parent_pid <= 0:
        return

    def monitor():
        while True:
            time.sleep(1.0)
            if os.getppid() == 1:
                os._exit(0)
            try:
                os.kill(parent_pid, 0)
            except OSError:
                os._exit(0)

    threading.Thread(target=monitor, daemon=True).start()

start_parent_monitor()

def result_page(label, text="", latex="", score=0.0, error=""):
    return {
        "label": label,
        "text": str(text or "").strip(),
        "latex": str(latex or "").strip(),
        "score": float(score or 0.0),
        "error": str(error or "").strip(),
    }

def error_page(label, error):
    return result_page(label, text=label + " failed:\n" + str(error), score=0.0, error=str(error))

def math_marker_count(text):
    text = str(text or "")
    return len(
        re.findall(
            r"\$\$[\s\S]+?\$\$|\\\[[\s\S]+?\\\]|\\\([\s\S]+?\\\)|"
            r"(?<!\$)\$[^$\n]{1,500}\$(?!\$)",
            text,
        )
    )

def has_markdown_table(text):
    text = str(text or "")
    return bool(re.search(r"^\|.*\|\s*$\n^\|[-:\s|]+\|\s*$", text, re.M))

def score_markdown(text, label):
    text = str(text or "").strip()
    if not text:
        return 0.0
    score = min(1.0, len(text) / 80.0)
    markers = math_marker_count(text)
    if markers:
        score += 0.4
    if has_markdown_table(text):
        score -= 0.7
    if label == "Marker Formula" and markers:
        score += 0.3
    return max(0.0, score)

try:
    import torch
    route_threads = int(os.environ.get("FLAMESHOT_MARKER_ROUTE_THREADS", "4"))
    route_threads = max(1, route_threads)
    torch.set_num_threads(route_threads)
    try:
        torch.set_num_interop_threads(1)
    except RuntimeError:
        pass
    log("torch threads configured: num_threads=" + str(torch.get_num_threads()))
except Exception as error:
    log("torch thread configuration failed: " + str(error))

log("import marker")
from marker.converters.pdf import PdfConverter
from marker.models import create_model_dict
from marker.output import text_from_rendered

RENDERER = "marker.renderers.markdown.MarkdownRenderer"

log("load models begin")
MODELS = create_model_dict()
log("load models done")
send({"type": "ready"})

def convert_marker(image_path, label, force_layout_block=None):
    config = {
        "pdftext_workers": 1,
        "disable_tqdm": True,
        "extract_images": False,
    }
    if force_layout_block:
        config["force_layout_block"] = force_layout_block
    converter = PdfConverter(
        config=config,
        artifact_dict=MODELS.copy(),
        renderer=RENDERER,
    )
    start = time.time()
    rendered = converter(image_path)
    markdown, _, _ = text_from_rendered(rendered)
    elapsed = time.time() - start
    markdown = str(markdown or "").strip()
    log(label + " done: seconds=%.2f, chars=%d, math=%d" % (
        elapsed,
        len(markdown),
        math_marker_count(markdown),
    ))
    return result_page(
        label,
        text=markdown,
        score=score_markdown(markdown, label),
    )

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
        label = request.get("label") or "Marker Formula"
        force_layout_block = request.get("force_layout_block") or None
        if not image_path:
            send({
                "type": "result",
                "id": request_id,
                "ok": False,
                "page": error_page(label, "empty image path"),
            })
            continue
        try:
            page = convert_marker(image_path, label, force_layout_block)
            send({"type": "result", "id": request_id, "ok": True, "page": page})
        except Exception as error:
            send({
                "type": "result",
                "id": request_id,
                "ok": False,
                "page": error_page(label, error),
                "traceback": traceback.format_exc(),
            })
    except Exception as error:
        send({
            "type": "result",
            "id": request.get("id"),
            "ok": False,
            "page": error_page("Marker Formula", error),
            "traceback": traceback.format_exc(),
        })
log("route worker exiting")
'''

class FormulaRouteWorker:
    def __init__(self):
        self.process = None
        self.next_id = 1

    def ensure_started(self):
        if self.process is not None and self.process.poll() is None:
            return
        if self.process is not None:
            log("formula route worker exited: code=" + str(self.process.poll()))
        self.stop()
        environment = os.environ.copy()
        environment["FLAMESHOT_MARKER_ROUTE_THREADS"] = str(marker_parallel_threads)
        environment["FLAMESHOT_MARKER_PARENT_PID"] = str(os.getpid())
        self.process = subprocess.Popen(
            [sys.executable, "-u", "-c", ROUTE_WORKER_SCRIPT],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=sys.stderr,
            text=True,
            env=environment,
        )
        while True:
            message = self.read_message()
            if message.get("type") == "ready":
                return

    def read_message(self):
        if self.process is None or self.process.stdout is None:
            raise RuntimeError("formula route worker is not running")
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise RuntimeError("formula route worker exited before replying")
            line = line.strip()
            if not line:
                continue
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                log("formula route worker returned non-JSON output: " + line[:240])

    def recognize(self, image_path):
        self.ensure_started()
        request_id = self.next_id
        self.next_id += 1
        request = {
            "cmd": "recognize",
            "id": request_id,
            "image": image_path,
            "label": "Marker Formula",
            "force_layout_block": "Equation",
        }
        try:
            self.process.stdin.write(json.dumps(request, ensure_ascii=False) + "\n")
            self.process.stdin.flush()
        except Exception:
            self.stop()
            self.ensure_started()
            self.process.stdin.write(json.dumps(request, ensure_ascii=False) + "\n")
            self.process.stdin.flush()

        while True:
            message = self.read_message()
            if message.get("type") != "result":
                continue
            if message.get("id") != request_id:
                log("formula route worker returned unexpected id: " + str(message.get("id")))
                continue
            page = message.get("page") or {}
            if not message.get("ok", False):
                log("formula route worker failed")
            return page

    def stop(self):
        process = self.process
        self.process = None
        if process is None:
            return
        try:
            if process.poll() is None and process.stdin is not None:
                process.stdin.write('{"cmd":"quit"}\n')
                process.stdin.flush()
                process.stdin.close()
        except Exception:
            pass
        try:
            process.wait(timeout=1.5)
        except Exception:
            process.kill()
            process.wait(timeout=1.0)

FORMULA_WORKER = FormulaRouteWorker()

def recognize_parallel_formula_fallback(image_path):
    pages = []
    formula_page = None
    previous_threads = set_torch_threads(
        marker_parallel_threads, "parallel markdown route begin"
    )
    try:
        with ThreadPoolExecutor(max_workers=1) as executor:
            formula_future = executor.submit(FORMULA_WORKER.recognize, image_path)
            pages.append(convert_marker_safely(image_path, "Marker Markdown"))
            try:
                formula_page = formula_future.result()
            except Exception as error:
                log("formula route worker failed: " + str(error))
                formula_page = error_page("Marker Formula", error)
    finally:
        restore_torch_threads(previous_threads, "parallel markdown route end")
    if formula_page is not None:
        pages.append(formula_page)
    return pages

def recognize(image_path):
    pages = []
    size = image_size(image_path)
    run_formula_fallback = should_run_formula_fallback(image_path, size)
    if (
        run_formula_fallback
        and MARKER_PARALLEL_SMALL_IMAGES
        and is_small_fallback_image(size)
    ):
        log(
            "parallel small-image fallback begin: "
            + image_path
            + ", threads="
            + str(marker_parallel_threads)
        )
        return choose_pages(recognize_parallel_formula_fallback(image_path))

    try:
        log("document predict begin: " + image_path)
        pages.append(convert_marker(image_path, "Marker Markdown"))
    except Exception as error:
        log("document predict failed: " + str(error))
        pages.append(error_page("Marker Markdown", error))

    if run_formula_fallback:
        try:
            log("formula fallback begin: " + image_path)
            pages.append(convert_marker(image_path, "Marker Formula", "Equation"))
        except Exception as error:
            log("formula fallback failed: " + str(error))
            pages.append(error_page("Marker Formula", error))

    return choose_pages(pages)

def recognize_formula(image_path):
    try:
        return FORMULA_WORKER.recognize(image_path)
    except Exception as error:
        log("manual formula route failed: " + str(error))
        return error_page("Marker Formula", error)

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    request = {}
    try:
        request = json.loads(line)
        if request.get("cmd") == "quit":
            break
        command = request.get("cmd") or "recognize"
        request_id = request.get("id")
        image_path = request.get("image") or ""
        if not image_path:
            send({
                "type": "result",
                "id": request_id,
                "ok": False,
                "error": "empty image path",
            })
            continue
        if command == "recognize_formula":
            page = recognize_formula(image_path)
            send({
                "type": "result",
                "id": request_id,
                "ok": page_has_content(page) and not bool(page.get("error")),
                "text": page.get("text", ""),
                "latex": page.get("latex", ""),
                "result_info": route_label(page),
                "fallback_text": "",
                "fallback_latex": "",
                "fallback_info": "",
                "extra_text": "",
                "extra_latex": "",
                "extra_info": "",
                "error": page.get("error", ""),
            })
            continue
        if command != "recognize":
            send({
                "type": "result",
                "id": request_id,
                "ok": False,
                "error": "unknown command: " + str(command),
            })
            continue
        primary, fallback, extra = recognize(image_path)
        send({
            "type": "result",
            "id": request_id,
            "ok": page_has_content(primary),
            "text": primary.get("text", ""),
            "latex": primary.get("latex", ""),
            "result_info": route_label(primary),
            "fallback_text": fallback.get("text", ""),
            "fallback_latex": fallback.get("latex", ""),
            "fallback_info": route_label(fallback),
            "extra_text": extra.get("text", ""),
            "extra_latex": extra.get("latex", ""),
            "extra_info": route_label(extra),
            "error": primary.get("error", ""),
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

bool prepareLatexOcrCommands(const QString& imagePath,
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
        appendLatexBackendCommand(
          QStringLiteral("texteller"), imagePath, commands);
        appendLatexBackendCommand(
          QStringLiteral("pix2text"), imagePath, commands);
        appendLatexBackendCommand(
          QStringLiteral("pix2tex"), imagePath, commands);
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
        environment.insert(QStringLiteral("HF_HUB_OFFLINE"),
                           QStringLiteral("1"));
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
    if (!environment.contains(QStringLiteral("MODEL_CACHE_DIR"))) {
        environment.insert(QStringLiteral("MODEL_CACHE_DIR"),
                           markerOcrCacheHome());
    }
    if (!environment.contains(QStringLiteral("TORCH_DEVICE"))) {
        environment.insert(QStringLiteral("TORCH_DEVICE"),
                           QStringLiteral("cpu"));
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
        environment.insert(
          QStringLiteral("PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK"),
          QStringLiteral("True"));
    }
    QDir().mkpath(environment.value(QStringLiteral("PADDLE_PDX_CACHE_HOME")));
    QDir().mkpath(environment.value(QStringLiteral("MODEL_CACHE_DIR")));
    QDir().mkpath(environment.value(QStringLiteral("HF_HOME")));
    QDir().mkpath(environment.value(QStringLiteral("MPLCONFIGDIR")));
    QDir().mkpath(environment.value(QStringLiteral("YOLO_CONFIG_DIR")));
    QDir().mkpath(environment.value(QStringLiteral("MODELSCOPE_CACHE")));
    return environment;
}

QProcessEnvironment markerOcrProcessEnvironment()
{
    QProcessEnvironment environment = ocrProcessEnvironment();
    const QString threads = QString::number(markerOcrThreads());
    const QString parallelThreads = QString::number(markerOcrParallelThreads());
    environment.insert(QStringLiteral("FLAMESHOT_MARKER_OCR_THREADS"), threads);
    environment.insert(QStringLiteral("FLAMESHOT_MARKER_OCR_PARALLEL_THREADS"),
                       parallelThreads);
    environment.insert(
      QStringLiteral("FLAMESHOT_MARKER_OCR_PARALLEL_SMALL_IMAGES"),
      markerOcrParallelSmallImages() ? QStringLiteral("1")
                                     : QStringLiteral("0"));
    if (!environment.contains(QStringLiteral("OMP_NUM_THREADS"))) {
        environment.insert(QStringLiteral("OMP_NUM_THREADS"), threads);
    }
    if (!environment.contains(QStringLiteral("MKL_NUM_THREADS"))) {
        environment.insert(QStringLiteral("MKL_NUM_THREADS"), threads);
    }
    if (!environment.contains(QStringLiteral("NUMEXPR_NUM_THREADS"))) {
        environment.insert(QStringLiteral("NUMEXPR_NUM_THREADS"), threads);
    }
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
            AbstractLogger::info(AbstractLogger::Stderr) << QObject::tr(
              "Texteller worker idle timeout reached; stopping.");
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
                request.callback(false,
                                 QObject::tr("texteller task cancelled"));
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
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
            drainStdout();
        });
        connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
            drainStderr();
        });
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
              << QObject::tr(
                   "Texteller worker returned an unexpected result id: %1")
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

    explicit PaddleOcrService(QObject* parent = nullptr)
      : QObject(parent)
      , m_idleTimer(new QTimer(this))
    {
        m_idleTimer->setSingleShot(true);
        connect(m_idleTimer, &QTimer::timeout, this, [this]() {
            AbstractLogger::info(AbstractLogger::Stderr) << QObject::tr(
              "PaddleOCR worker idle timeout reached; stopping.");
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
                request.callback(false,
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
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
                         QString(),
                         QString(),
                         QString(),
                         QString(),
                         QString(),
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
                request.callback(false,
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QObject::tr("PaddleOCR worker was stopped"));
            }
        }
        m_queue.clear();
        if (m_current.id != 0 && m_current.callback) {
            m_current.callback(false,
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
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
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
            drainStdout();
        });
        connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
            drainStderr();
        });
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
              << QObject::tr(
                   "PaddleOCR worker returned an unexpected result id: %1")
                   .arg(QString::number(id));
            return;
        }

        const Callback callback = m_current.callback;
        const bool ok = object.value(QStringLiteral("ok")).toBool();
        const QString text = object.value(QStringLiteral("text")).toString();
        const QString latex = object.value(QStringLiteral("latex")).toString();
        const QString fallbackText =
          object.value(QStringLiteral("fallback_text")).toString();
        const QString fallbackLatex =
          object.value(QStringLiteral("fallback_latex")).toString();
        const QString resultInfo =
          object.value(QStringLiteral("result_info")).toString();
        const QString fallbackInfo =
          object.value(QStringLiteral("fallback_info")).toString();
        const QString extraText =
          object.value(QStringLiteral("extra_text")).toString();
        const QString extraLatex =
          object.value(QStringLiteral("extra_latex")).toString();
        const QString extraInfo =
          object.value(QStringLiteral("extra_info")).toString();
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
            callback(ok,
                     text,
                     latex,
                     fallbackText,
                     fallbackLatex,
                     resultInfo,
                     fallbackInfo,
                     extraText,
                     extraLatex,
                     extraInfo,
                     error);
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
                         QString(),
                         QString(),
                         QString(),
                         QString(),
                         QString(),
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
                request.callback(false,
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 error);
            }
        }
        m_queue.clear();
        if (m_current.id != 0 && m_current.callback) {
            m_current.callback(false,
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               error);
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

class MarkerOcrService : public QObject
{
public:
    using Callback = PaddleOcrService::Callback;

    explicit MarkerOcrService(QObject* parent = nullptr)
      : QObject(parent)
      , m_idleTimer(new QTimer(this))
    {
        m_idleTimer->setSingleShot(true);
        connect(m_idleTimer, &QTimer::timeout, this, [this]() {
            AbstractLogger::info(AbstractLogger::Stderr) << QObject::tr(
              "Marker OCR worker idle timeout reached; stopping.");
            stopProcess();
        });
    }

    int recognize(const QString& imagePath, Callback callback)
    {
        return enqueue(imagePath, false, std::move(callback));
    }

    int recognizeFormula(const QString& imagePath, Callback callback)
    {
        return enqueue(imagePath, true, std::move(callback));
    }

    int enqueue(const QString& imagePath, bool formulaOnly, Callback callback)
    {
        m_idleTimer->stop();
        Request request;
        request.id = m_nextRequestId++;
        request.imagePath = imagePath;
        request.formulaOnly = formulaOnly;
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
                request.callback(false,
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QObject::tr("Marker OCR task cancelled"));
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
                         QString(),
                         QString(),
                         QString(),
                         QString(),
                         QString(),
                         QString(),
                         QString(),
                         QObject::tr("Marker OCR task cancelled"));
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
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QObject::tr("Marker OCR worker was stopped"));
            }
        }
        m_queue.clear();
        if (m_current.id != 0 && m_current.callback) {
            m_current.callback(false,
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QObject::tr("Marker OCR worker was stopped"));
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
        bool formulaOnly = false;
        Callback callback;
    };

    void ensureProcess()
    {
        if (m_process && m_process->state() != QProcess::NotRunning) {
            return;
        }

        const QString python = markerOcrPython();
        if (python.isEmpty()) {
            failPending(QObject::tr(
              "Marker OCR requires Python with marker-pdf installed."));
            return;
        }

        m_idleTimer->stop();
        m_ready = false;
        m_stdoutBuffer.clear();
        m_process = new QProcess(this);
        m_process->setProcessEnvironment(markerOcrProcessEnvironment());

        connect(m_process, &QProcess::started, this, [this]() {
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("Marker OCR worker started: pid=%1.")
                   .arg(QString::number(m_process->processId()));
        });
        connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
            drainStdout();
        });
        connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
            drainStderr();
        });
        connect(m_process,
                &QProcess::finished,
                this,
                [this](int exitCode, QProcess::ExitStatus exitStatus) {
                    handleFinished(exitCode, exitStatus);
                });

        AbstractLogger::info(AbstractLogger::Stderr)
          << QObject::tr(
               "Marker OCR worker launching: program=%1, threads=%2, "
               "parallelThreads=%3.")
               .arg(python,
                    QString::number(markerOcrThreads()),
                    QString::number(markerOcrParallelThreads()));
        m_process->start(python,
                         { QStringLiteral("-u"),
                           QStringLiteral("-c"),
                           markerOcrServicePythonScript() });
    }

    void startNextRequest()
    {
        if (!m_ready || m_current.id != 0 || m_queue.isEmpty() || !m_process ||
            m_process->state() != QProcess::Running) {
            return;
        }

        m_current = m_queue.takeFirst();
        QJsonObject request;
        request.insert(QStringLiteral("cmd"),
                       m_current.formulaOnly
                         ? QStringLiteral("recognize_formula")
                         : QStringLiteral("recognize"));
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
              << QObject::tr("Marker OCR worker stderr: %1")
                   .arg(line.left(500));
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
              << QObject::tr("Marker OCR worker returned non-JSON output: %1")
                   .arg(line.left(500));
            return;
        }

        const QJsonObject object = document.object();
        const QString type = object.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("ready")) {
            m_ready = true;
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("Marker OCR worker ready.");
            startNextRequest();
            return;
        }

        if (type != QStringLiteral("result")) {
            return;
        }

        const int id = object.value(QStringLiteral("id")).toInt();
        if (m_current.id == 0 || id != m_current.id) {
            AbstractLogger::warning(AbstractLogger::Stderr)
              << QObject::tr(
                   "Marker OCR worker returned an unexpected result id: %1")
                   .arg(QString::number(id));
            return;
        }

        const Callback callback = m_current.callback;
        const bool ok = object.value(QStringLiteral("ok")).toBool();
        const QString text = object.value(QStringLiteral("text")).toString();
        const QString latex = object.value(QStringLiteral("latex")).toString();
        const QString fallbackText =
          object.value(QStringLiteral("fallback_text")).toString();
        const QString fallbackLatex =
          object.value(QStringLiteral("fallback_latex")).toString();
        const QString resultInfo =
          object.value(QStringLiteral("result_info")).toString();
        const QString fallbackInfo =
          object.value(QStringLiteral("fallback_info")).toString();
        const QString extraText =
          object.value(QStringLiteral("extra_text")).toString();
        const QString extraLatex =
          object.value(QStringLiteral("extra_latex")).toString();
        const QString extraInfo =
          object.value(QStringLiteral("extra_info")).toString();
        QString error = object.value(QStringLiteral("error")).toString();
        const QString traceback =
          object.value(QStringLiteral("traceback")).toString();
        if (!traceback.isEmpty()) {
            AbstractLogger::warning(AbstractLogger::Stderr)
              << QObject::tr("Marker OCR worker traceback: %1")
                   .arg(traceback.left(1200));
        }
        if (!ok && error.isEmpty()) {
            error = QObject::tr("Marker OCR failed.");
        }

        m_current = {};
        if (callback) {
            callback(ok,
                     text,
                     latex,
                     fallbackText,
                     fallbackLatex,
                     resultInfo,
                     fallbackInfo,
                     extraText,
                     extraLatex,
                     extraInfo,
                     error);
        }

        if (m_queue.isEmpty()) {
            const int idleTimeout = markerOcrIdleTimeoutMs();
            AbstractLogger::info(AbstractLogger::Stderr)
              << QObject::tr("Marker OCR worker idle timer started: %1 ms.")
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
                         QString(),
                         QString(),
                         QString(),
                         QString(),
                         QString(),
                         QString(),
                         QString(),
                         QObject::tr("Marker OCR worker exited unexpectedly"));
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
                request.callback(false,
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 QString(),
                                 error);
            }
        }
        m_queue.clear();
        if (m_current.id != 0 && m_current.callback) {
            m_current.callback(false,
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               QString(),
                               error);
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

MarkerOcrService* markerOcrService()
{
    static MarkerOcrService* service = new MarkerOcrService(qApp);
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

OcrTaskWidget::OcrTaskWidget(Kind kind, const QPixmap& capture, QWidget* parent)
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

void OcrTaskWidget::stopMarkerOcrService()
{
    markerOcrService()->stop();
}

bool OcrTaskWidget::isPaddleOcrServiceRunning()
{
    return paddleOcrService()->isRunning();
}

bool OcrTaskWidget::isMarkerOcrServiceRunning()
{
    return markerOcrService()->isRunning();
}

int OcrTaskWidget::requestMarkerFormulaOcr(const QPixmap& capture,
                                           MarkerFormulaCallback callback)
{
    if (capture.isNull()) {
        QTimer::singleShot(0, qApp, [callback = std::move(callback)]() {
            if (callback) {
                callback(false,
                         QString(),
                         QString(),
                         QString(),
                         QObject::tr("Unable to prepare image."));
            }
        });
        return 0;
    }

    QTemporaryFile imageFile(
      QDir::tempPath() +
      QStringLiteral("/flameshot-marker-formula-route-XXXXXX.png"));
    imageFile.setAutoRemove(false);
    if (!imageFile.open() || !capture.toImage()
                                .convertToFormat(QImage::Format_RGB32)
                                .save(&imageFile, "PNG")) {
        QFile::remove(imageFile.fileName());
        QTimer::singleShot(0, qApp, [callback = std::move(callback)]() {
            if (callback) {
                callback(false,
                         QString(),
                         QString(),
                         QString(),
                         QObject::tr("Unable to create a temporary image for "
                                     "formula OCR."));
            }
        });
        return 0;
    }
    imageFile.close();
    const QString imagePath = QFileInfo(imageFile.fileName()).absoluteFilePath();

    AbstractLogger::info(AbstractLogger::Stderr)
      << QObject::tr("Marker formula route queued in background: image=%1, "
                     "timeout=%2 ms.")
           .arg(imagePath, QString::number(markerOcrTimeoutMs()));

    return markerOcrService()->recognizeFormula(
      imagePath,
      [imagePath, callback = std::move(callback)](
        bool ok,
        const QString& text,
        const QString& latex,
        const QString& fallbackText,
        const QString& fallbackLatex,
        const QString& resultInfo,
        const QString& fallbackInfo,
        const QString& extraText,
        const QString& extraLatex,
        const QString& extraInfo,
        const QString& error) {
          Q_UNUSED(fallbackText)
          Q_UNUSED(fallbackLatex)
          Q_UNUSED(fallbackInfo)
          Q_UNUSED(extraText)
          Q_UNUSED(extraLatex)
          Q_UNUSED(extraInfo)
          if (keepOcrTempImage()) {
              AbstractLogger::info(AbstractLogger::Stderr)
                << QObject::tr("Keeping OCR temporary image: %1")
                     .arg(imagePath);
          } else {
              QFile::remove(imagePath);
          }
          if (callback) {
              callback(ok, text, latex, resultInfo, error);
          }
      });
}

void OcrTaskWidget::cancelMarkerOcrRequest(int requestId)
{
    if (requestId != 0) {
        markerOcrService()->cancel(requestId);
    }
}

void OcrTaskWidget::start()
{
    if (m_capture.isNull()) {
        failTask(tr("Unable to prepare image."));
        return;
    }

    if (m_kind == Kind::Barcode) {
        startBarcodeScan();
        return;
    }

    const QString backend = configuredOcrBackend();
    if (backend == QStringLiteral("marker") ||
        (backend == QStringLiteral("auto") && !markerOcrPython().isEmpty())) {
        startMarkerOcr();
        return;
    }

    startPaddleOcr();
}

void OcrTaskWidget::startBarcodeScan()
{
    setStatus(tr("Scanning barcode..."));
    const QImage image = m_capture.toImage();
    auto* thread =
      QThread::create([guard = QPointer<OcrTaskWidget>(this), image]() {
          const BarcodeReader::ScanResult scan =
            BarcodeReader::scanImage(image);
          const QString result = BarcodeReader::formatResults(scan.results);
          const QString error = scan.error;
          if (!guard) {
              return;
          }
          QMetaObject::invokeMethod(
            guard,
            [guard, result, error]() {
                if (guard) {
                    guard->handleBarcodeScanFinished(result, error);
                }
            },
            Qt::QueuedConnection);
      });
    m_barcodeThread = thread;
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_barcodeThread == thread) {
            m_barcodeThread = nullptr;
        }
        thread->deleteLater();
    });
    thread->start();
}

void OcrTaskWidget::startPaddleOcr()
{
    setStatus(tr("Preparing PaddleOCR..."));

    QTemporaryFile imageFile(QDir::tempPath() +
                             QStringLiteral("/flameshot-paddleocr-XXXXXX.png"));
    imageFile.setAutoRemove(false);
    if (!imageFile.open() || !m_capture.toImage()
                                .convertToFormat(QImage::Format_RGB32)
                                .save(&imageFile, "PNG")) {
        QFile::remove(imageFile.fileName());
        failTask(tr("Unable to create a temporary image for OCR."));
        return;
    }
    imageFile.close();
    m_imagePath = QFileInfo(imageFile.fileName()).absoluteFilePath();
    emit preparedImageReady(m_imagePath);

    QTemporaryFile formulaImageFile(
      QDir::tempPath() +
      QStringLiteral("/flameshot-paddleocr-formula-XXXXXX.png"));
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
    m_paddleOcrRequestId = paddleOcrService()->recognize(
      m_imagePath,
      m_formulaImagePath,
      [guard = QPointer<OcrTaskWidget>(this)](bool ok,
                                              const QString& text,
                                              const QString& latex,
                                              const QString& fallbackText,
                                              const QString& fallbackLatex,
                                              const QString& resultInfo,
                                              const QString& fallbackInfo,
                                              const QString& extraText,
                                              const QString& extraLatex,
                                              const QString& extraInfo,
                                              const QString& error) {
          if (guard) {
              guard->handlePaddleOcrServiceFinished(ok,
                                                    text,
                                                    latex,
                                                    fallbackText,
                                                    fallbackLatex,
                                                    resultInfo,
                                                    fallbackInfo,
                                                    extraText,
                                                    extraLatex,
                                                    extraInfo,
                                                    error);
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

void OcrTaskWidget::startMarkerOcr()
{
    setStatus(tr("Preparing Marker OCR..."));

    QTemporaryFile imageFile(QDir::tempPath() +
                             QStringLiteral("/flameshot-marker-ocr-XXXXXX.png"));
    imageFile.setAutoRemove(false);
    if (!imageFile.open() || !m_capture.toImage()
                                .convertToFormat(QImage::Format_RGB32)
                                .save(&imageFile, "PNG")) {
        QFile::remove(imageFile.fileName());
        failTask(tr("Unable to create a temporary image for OCR."));
        return;
    }
    imageFile.close();
    m_imagePath = QFileInfo(imageFile.fileName()).absoluteFilePath();
    emit preparedImageReady(m_imagePath);

    AbstractLogger::info(AbstractLogger::Stderr)
      << tr("Marker OCR queued in background: image=%1, timeout=%2 ms.")
           .arg(m_imagePath, QString::number(markerOcrTimeoutMs()));

    setStatus(tr("Running Marker OCR..."));
    m_markerOcrRequestTimedOut = false;
    m_markerOcrRequestId = markerOcrService()->recognize(
      m_imagePath,
      [guard = QPointer<OcrTaskWidget>(this)](bool ok,
                                              const QString& text,
                                              const QString& latex,
                                              const QString& fallbackText,
                                              const QString& fallbackLatex,
                                              const QString& resultInfo,
                                              const QString& fallbackInfo,
                                              const QString& extraText,
                                              const QString& extraLatex,
                                              const QString& extraInfo,
                                              const QString& error) {
          if (guard) {
              guard->handleMarkerOcrServiceFinished(ok,
                                                    text,
                                                    latex,
                                                    fallbackText,
                                                    fallbackLatex,
                                                    resultInfo,
                                                    fallbackInfo,
                                                    extraText,
                                                    extraLatex,
                                                    extraInfo,
                                                    error);
          }
      });

    const int requestId = m_markerOcrRequestId;
    QTimer::singleShot(markerOcrTimeoutMs(), this, [this, requestId]() {
        if (m_markerOcrRequestId != requestId) {
            return;
        }
        m_markerOcrRequestTimedOut = true;
        m_lastError = tr("Marker OCR backend timed out.");
        AbstractLogger::warning(AbstractLogger::Stderr) << m_lastError;
        markerOcrService()->cancel(requestId);
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
            for (const TextOcrCandidateResult& result :
                 m_textCandidateResults) {
                if (!result.ok || result.wordCount <= 0) {
                    continue;
                }
                qreal score = result.confidence;
                const bool hasChinese = result.chineseCount >= 2;
                const bool hasLatin = result.latinCount >= 2;
                if (result.language == QStringLiteral("eng")) {
                    score +=
                      result.latinCount >= result.chineseCount ? 3.0 : -10.0;
                } else if (result.language == QStringLiteral("chi_sim")) {
                    score +=
                      result.chineseCount >= result.latinCount ? 3.0 : -5.0;
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
            bestLanguage = ocrLanguage(
              QStandardPaths::findExecutable(QStringLiteral("tesseract")));
        }
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("Text OCR language selected: %1.")
               .arg(bestLanguage.isEmpty() ? QStringLiteral("default")
                                           : bestLanguage);
        startFinalTextOcr(bestLanguage);
        return;
    }

    const QString tesseract =
      QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (tesseract.isEmpty()) {
        failTask(tr("OCR requires the tesseract command to be installed."));
        return;
    }

    const QString language =
      m_textLanguageCandidates.at(m_currentTextCandidate);
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
    command.backendName = language.isEmpty()
                            ? QStringLiteral("tesseract")
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
      << tr("LaTeX OCR queued in background: image=%1, backends=%2, timeout=%3 "
            "ms.")
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
        m_textellerRequestId = textellerService()->recognize(
          m_imagePath,
          [guard = QPointer<OcrTaskWidget>(this),
           backendName](bool ok, const QString& result) {
              if (guard) {
                  guard->handleTextellerServiceFinished(
                    backendName, ok, result);
              }
          });

        const int requestId = m_textellerRequestId;
        QTimer::singleShot(
          latexOcrTimeoutMs(), this, [this, requestId, backendName]() {
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
        const QString backendName =
          m_process->property("backendName").toString();
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
        m_lastError = timedOut
                        ? tr("%1 backend timed out.").arg(backendName)
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
                                                   const QString& fallbackText,
                                                   const QString& fallbackLatex,
                                                   const QString& resultInfo,
                                                   const QString& fallbackInfo,
                                                   const QString& extraText,
                                                   const QString& extraLatex,
                                                   const QString& extraInfo,
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
        m_lastError = timedOut ? tr("PaddleOCR backend timed out.")
                               : tr("PaddleOCR backend failed: %1").arg(error);
        AbstractLogger::warning(AbstractLogger::Stderr) << m_lastError;
        failTask(m_lastError);
        return;
    }

    completePaddleOcr(text.trimmed(),
                      latex.trimmed(),
                      fallbackText.trimmed(),
                      fallbackLatex.trimmed(),
                      resultInfo.trimmed(),
                      fallbackInfo.trimmed(),
                      extraText.trimmed(),
                      extraLatex.trimmed(),
                      extraInfo.trimmed());
}

void OcrTaskWidget::handleMarkerOcrServiceFinished(bool ok,
                                                   const QString& text,
                                                   const QString& latex,
                                                   const QString& fallbackText,
                                                   const QString& fallbackLatex,
                                                   const QString& resultInfo,
                                                   const QString& fallbackInfo,
                                                   const QString& extraText,
                                                   const QString& extraLatex,
                                                   const QString& extraInfo,
                                                   const QString& error)
{
    if (m_markerOcrRequestId == 0) {
        return;
    }

    const bool timedOut = m_markerOcrRequestTimedOut;
    m_markerOcrRequestId = 0;
    m_markerOcrRequestTimedOut = false;

    if (m_cancelled) {
        emit cancelled();
        close();
        return;
    }

    if (!ok || timedOut) {
        m_lastError = timedOut ? tr("Marker OCR backend timed out.")
                               : tr("Marker OCR backend failed: %1").arg(error);
        AbstractLogger::warning(AbstractLogger::Stderr) << m_lastError;
        failTask(m_lastError);
        return;
    }

    completePaddleOcr(text.trimmed(),
                      latex.trimmed(),
                      fallbackText.trimmed(),
                      fallbackLatex.trimmed(),
                      resultInfo.trimmed(),
                      fallbackInfo.trimmed(),
                      extraText.trimmed(),
                      extraLatex.trimmed(),
                      extraInfo.trimmed());
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
                    confIndex >= columns.size() ||
                    textIndex >= columns.size()) {
                    continue;
                }
                bool confidenceOk = false;
                const qreal confidence =
                  columns.at(confIndex).toDouble(&confidenceOk);
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
        m_lastError = processErrorMessage(
          tr("%1 backend failed").arg(backendName), m_processErrorOutput);
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

    QString output =
      QString::fromUtf8(process->readAllStandardOutput()).trimmed();
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
    m_lastError = tr("%1 backend did not start: %2")
                    .arg(backendName, process->errorString());
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
    setStatus(tr("Cancelling task..."));
    AbstractLogger::info(AbstractLogger::Stderr)
      << tr("Background task cancelled.");
    if (m_barcodeThread && m_barcodeThread->isRunning()) {
        m_barcodeThread->requestInterruption();
        return;
    }
    if (m_paddleOcrRequestId != 0) {
        paddleOcrService()->cancel(m_paddleOcrRequestId);
        return;
    }
    if (m_markerOcrRequestId != 0) {
        markerOcrService()->cancel(m_markerOcrRequestId);
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

void OcrTaskWidget::handleBarcodeScanFinished(const QString& result,
                                              const QString& error)
{
    if (m_cancelled) {
        emit cancelled();
        close();
        return;
    }
    if (result.isEmpty()) {
        failTask(error.isEmpty() ? tr("No barcode or 2D code was recognized.")
                                 : error);
        return;
    }
    completeBarcodeScan(result);
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

void OcrTaskWidget::completePaddleOcr(const QString& text,
                                      const QString& latex,
                                      const QString& fallbackText,
                                      const QString& fallbackLatex,
                                      const QString& resultInfo,
                                      const QString& fallbackInfo,
                                      const QString& extraText,
                                      const QString& extraLatex,
                                      const QString& extraInfo)
{
    cleanupImage();
    if (text.isEmpty() && latex.isEmpty() && fallbackText.isEmpty() &&
        fallbackLatex.isEmpty() && extraText.isEmpty() &&
        extraLatex.isEmpty()) {
        AbstractLogger::warning() << tr("No text or formula was recognized.");
    } else {
        AbstractLogger::info(AbstractLogger::Stderr)
          << tr("OCR backend succeeded: textChars=%1, latexChars=%2, "
                "fallbackChars=%3, extraChars=%4, text=%5, latex=%6")
               .arg(QString::number(text.size()),
                    QString::number(latex.size()),
                    QString::number(fallbackText.size() + fallbackLatex.size()),
                    QString::number(extraText.size() + extraLatex.size()),
                    ocrTextPreview(text),
                    latexPreview(latex));
    }
    emit ocrCompleted(m_capture,
                      text,
                      latex,
                      fallbackText,
                      fallbackLatex,
                      resultInfo,
                      fallbackInfo,
                      extraText,
                      extraLatex,
                      extraInfo);
    close();
}

void OcrTaskWidget::completeBarcodeScan(const QString& result)
{
    AbstractLogger::info(AbstractLogger::Stderr)
      << tr("Barcode scan succeeded: chars=%1, text=%2")
           .arg(QString::number(result.size()), ocrTextPreview(result));
    emit ocrCompleted(m_capture,
                      result,
                      QString(),
                      QString(),
                      QString(),
                      QString(),
                      QString(),
                      QString(),
                      QString(),
                      QString());
    close();
}

void OcrTaskWidget::cleanupProcess()
{
    if (m_barcodeThread) {
        QThread* thread = m_barcodeThread;
        m_barcodeThread = nullptr;
        thread->requestInterruption();
        thread->wait();
        thread->disconnect(this);
        thread->deleteLater();
    }

    if (m_paddleOcrRequestId != 0) {
        const int requestId = m_paddleOcrRequestId;
        m_paddleOcrRequestId = 0;
        paddleOcrService()->cancel(requestId);
    }

    if (m_markerOcrRequestId != 0) {
        const int requestId = m_markerOcrRequestId;
        m_markerOcrRequestId = 0;
        markerOcrService()->cancel(requestId);
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
