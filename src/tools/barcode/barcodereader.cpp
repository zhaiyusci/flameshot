// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "barcodereader.h"

#include <algorithm>

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QSet>
#include <QSize>

#if defined(FLAMESHOT_HAVE_ZXING)
#if __has_include(<ZXing/ReadBarcode.h>)
#include <ZXing/Barcode.h>
#include <ZXing/BarcodeFormat.h>
#include <ZXing/ImageView.h>
#include <ZXing/ReadBarcode.h>
#include <ZXing/ReaderOptions.h>
#else
#include <Barcode.h>
#include <BarcodeFormat.h>
#include <ImageView.h>
#include <ReadBarcode.h>
#include <ReaderOptions.h>
#endif
#endif

namespace {

QImage flattenImage(const QImage& source, const QColor& background = Qt::white)
{
    if (source.isNull()) {
        return {};
    }

    QImage canvas(source.size(), QImage::Format_RGB32);
    canvas.fill(background);

    QPainter painter(&canvas);
    painter.drawImage(QPoint(0, 0), source);
    painter.end();
    return canvas;
}

QColor estimatedBorderColor(const QImage& source)
{
    if (source.isNull()) {
        return Qt::white;
    }

    const QImage image = source.convertToFormat(QImage::Format_RGB32);
    const int margin =
      std::max(1, std::min(12, std::min(image.width(), image.height()) / 10));
    qint64 red = 0;
    qint64 green = 0;
    qint64 blue = 0;
    qint64 count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (x >= margin && y >= margin && x < image.width() - margin &&
                y < image.height() - margin) {
                continue;
            }
            const QColor color(image.pixel(x, y));
            red += color.red();
            green += color.green();
            blue += color.blue();
            ++count;
        }
    }

    if (count == 0) {
        return Qt::white;
    }
    return QColor(static_cast<int>(red / count),
                  static_cast<int>(green / count),
                  static_cast<int>(blue / count));
}

QImage paddedImage(const QImage& source, const QColor& background)
{
    if (source.isNull()) {
        return {};
    }

    const int padding =
      std::max(12, std::min(72, std::min(source.width(), source.height()) / 5));
    QImage image(source.size() + QSize(padding * 2, padding * 2),
                 QImage::Format_RGB32);
    image.fill(background);

    QPainter painter(&image);
    painter.drawImage(padding, padding, source);
    painter.end();
    return image;
}

QImage scaledIfSmall(const QImage& source)
{
    if (source.isNull()) {
        return {};
    }

    const int shortest = std::min(source.width(), source.height());
    if (shortest >= 360 || shortest <= 0) {
        return source;
    }

    const int factor =
      std::max(2, std::min(4, (360 + shortest - 1) / shortest));
    return source.scaled(
      source.size() * factor, Qt::IgnoreAspectRatio, Qt::FastTransformation);
}

QImage contrastStretchedGray(const QImage& source)
{
    if (source.isNull()) {
        return {};
    }

    const QImage rgb = source.convertToFormat(QImage::Format_RGB32);
    int histogram[256] = {};
    for (int y = 0; y < rgb.height(); ++y) {
        for (int x = 0; x < rgb.width(); ++x) {
            const QColor color(rgb.pixel(x, y));
            const int luminance =
              qRound(0.299 * color.red() + 0.587 * color.green() +
                     0.114 * color.blue());
            ++histogram[std::clamp(luminance, 0, 255)];
        }
    }

    const int total = rgb.width() * rgb.height();
    const int lowTarget = std::max(0, total / 100);
    const int highTarget = std::max(0, total - lowTarget);
    int low = 0;
    int high = 255;
    int accumulated = 0;
    for (int i = 0; i < 256; ++i) {
        accumulated += histogram[i];
        if (accumulated >= lowTarget) {
            low = i;
            break;
        }
    }
    accumulated = 0;
    for (int i = 0; i < 256; ++i) {
        accumulated += histogram[i];
        if (accumulated >= highTarget) {
            high = i;
            break;
        }
    }

    if (high <= low + 8) {
        return {};
    }

    QImage result(rgb.size(), QImage::Format_RGB32);
    for (int y = 0; y < rgb.height(); ++y) {
        for (int x = 0; x < rgb.width(); ++x) {
            const QColor color(rgb.pixel(x, y));
            const int luminance =
              qRound(0.299 * color.red() + 0.587 * color.green() +
                     0.114 * color.blue());
            const int stretched =
              std::clamp((luminance - low) * 255 / (high - low), 0, 255);
            result.setPixelColor(x, y, QColor(stretched, stretched, stretched));
        }
    }
    return result;
}

QImage invertedImage(const QImage& source)
{
    if (source.isNull()) {
        return {};
    }

    QImage image = source.convertToFormat(QImage::Format_RGB32);
    image.invertPixels(QImage::InvertRgb);
    return image;
}

QList<QImage> scanCandidates(const QImage& image)
{
    QList<QImage> candidates;
    const QImage base = flattenImage(image);
    if (base.isNull()) {
        return candidates;
    }

    const QColor border = estimatedBorderColor(base);
    const QImage scaled = scaledIfSmall(base);
    const QImage contrast = contrastStretchedGray(base);

    candidates << base;
    candidates << paddedImage(base, border);
    if (scaled.size() != base.size()) {
        candidates << scaled;
        candidates << paddedImage(scaled, border);
    }
    if (!contrast.isNull()) {
        candidates << contrast;
        candidates << paddedImage(contrast, estimatedBorderColor(contrast));
    }
    const QImage inverted = invertedImage(base);
    candidates << inverted;
    candidates << paddedImage(inverted, estimatedBorderColor(inverted));
    return candidates;
}

QString cleanedBarcodeText(QString text)
{
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    return text;
}

#if defined(FLAMESHOT_HAVE_ZXING)
QList<BarcodeReader::Result> scanCandidate(const QImage& image)
{
    QList<BarcodeReader::Result> results;
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    if (rgb.isNull()) {
        return results;
    }

    ZXing::ReaderOptions options;
    options.setFormats(ZXing::BarcodeFormat::Any)
      .setTryHarder(true)
      .setTryRotate(true)
      .setTryInvert(true)
      .setTryDownscale(true)
      .setMaxNumberOfSymbols(64)
      .setTextMode(ZXing::TextMode::HRI);

    const ZXing::ImageView view(rgb.constBits(),
                                rgb.width(),
                                rgb.height(),
                                ZXing::ImageFormat::RGB,
                                rgb.bytesPerLine());
    for (const auto& barcode : ZXing::ReadBarcodes(view, options)) {
        if (!barcode.isValid()) {
            continue;
        }
        const std::string text = barcode.text();
        if (text.empty()) {
            continue;
        }

        BarcodeReader::Result result;
        const std::string format = ZXing::ToString(barcode.format());
        result.format = QString::fromUtf8(
          format.data(), static_cast<qsizetype>(format.size()));
        result.text =
          QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
        result.orientation = barcode.orientation();
        result.inverted = barcode.isInverted();
        result.mirrored = barcode.isMirrored();
        results.append(result);
    }
    return results;
}
#endif

}

namespace BarcodeReader {

ScanResult scanImage(const QImage& image)
{
    ScanResult scan;
    if (image.isNull()) {
        scan.error = QObject::tr("Unable to prepare image for barcode scan.");
        return scan;
    }

#if !defined(FLAMESHOT_HAVE_ZXING)
    scan.error =
      QObject::tr("Barcode recognition requires zxing-cpp at build time.");
    return scan;
#else
    QSet<QString> seen;
    for (const QImage& candidate : scanCandidates(image)) {
        for (const Result& result : scanCandidate(candidate)) {
            const QString key = result.format + QChar(0x1f) + result.text;
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            scan.results.append(result);
        }
    }

    if (scan.results.isEmpty()) {
        scan.error = QObject::tr("No barcode or 2D code was recognized.");
    }
    return scan;
#endif
}

QString formatResults(const QList<Result>& results)
{
    QStringList lines;
    for (const Result& result : results) {
        lines << QStringLiteral("%1\t%2").arg(result.format,
                                              cleanedBarcodeText(result.text));
    }
    return lines.join(QLatin1Char('\n'));
}

}
