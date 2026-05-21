// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "ocrpreprocessor.h"

#include "utils/abstractlogger.h"

#include <algorithm>

#include <QImage>
#include <QPainter>
#include <QProcessEnvironment>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

namespace {
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
                              mode == QStringLiteral("official");
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
                                mode == QStringLiteral("white_background");
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

}

QImage OcrPreprocessor::preparedTextOcrImage(const QImage& image)
{
    return preparedTextOcrImageFromImage(image);
}

QImage OcrPreprocessor::preparedLatexOcrImage(const QImage& image)
{
    return preparedLatexOcrImageFromImage(image);
}
