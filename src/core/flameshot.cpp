// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "flameshot.h"
#include "core/flameshotdaemon.h"
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#include "qhotkey.h"
#endif

#if defined(Q_OS_MACOS)
#include <QWindow>
#include <objc/message.h>

namespace {

constexpr long NSApplicationActivationPolicyRegular = 0;
constexpr long NSApplicationActivationPolicyAccessory = 1;

void setActivationPolicy(long policy)
{
    auto sharedApp = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend);
    auto setPolicy = reinterpret_cast<void (*)(id, SEL, long)>(objc_msgSend);
    id nsApp = sharedApp(reinterpret_cast<id>(objc_getClass("NSApplication")),
                         sel_registerName("sharedApplication"));
    setPolicy(nsApp, sel_registerName("setActivationPolicy:"), policy);
}

void setActivationPolicyRegular()
{
    setActivationPolicy(NSApplicationActivationPolicyRegular);
}

void setActivationPolicyAccessory()
{
    setActivationPolicy(NSApplicationActivationPolicyAccessory);
}

constexpr const char* visibleInDockProperty = "_visibleInDock";

} // namespace

#include <CoreGraphics/CoreGraphics.h>
#endif

#include "config/configresolver.h"
#include "config/configwindow.h"
#include "core/qguiappcurrentscreen.h"
#include "utils/abstractlogger.h"
#include "utils/confighandler.h"
#include "utils/screengrabber.h"
#include "utils/screenshotsaver.h"
#include "widgets/capture/capturewidget.h"
#include "widgets/capturelauncher.h"
#include "widgets/infowindow.h"

#ifdef ENABLE_IMGUR
#include "tools/imgupload/imguploadermanager.h"
#include "tools/imgupload/storages/imguploaderbase.h"
#include "widgets/imguploaddialog.h"
#include "widgets/uploadhistory.h"
#endif

#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QImageReader>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QTextDocument>
#include <QTextOption>
#include <QUrl>
#include <QVersionNumber>

#include <algorithm>
#include <cmath>

#if defined(Q_OS_MACOS)
#include <QScreen>
#endif

namespace {

QScreen* currentOrPrimaryScreen()
{
    QScreen* screen = QGuiAppCurrentScreen().currentScreen();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    return screen;
}

QSize logicalPixmapSize(const QPixmap& pixmap)
{
    if (pixmap.devicePixelRatio() <= 1.0) {
        return pixmap.size();
    }
    return QSize(static_cast<int>(std::ceil(pixmap.width() /
                                            pixmap.devicePixelRatio())),
                 static_cast<int>(std::ceil(pixmap.height() /
                                            pixmap.devicePixelRatio())));
}

QPixmap constrainPixmapToScreen(const QPixmap& pixmap)
{
    QScreen* screen = currentOrPrimaryScreen();
    if (!screen || pixmap.isNull()) {
        return pixmap;
    }

    constexpr int pinWindowPadding = 32;
    QSize maxSize =
      screen->availableGeometry().size() -
      QSize(pinWindowPadding, pinWindowPadding);
    maxSize = maxSize.expandedTo(QSize(1, 1));

    const QSize logicalSize = logicalPixmapSize(pixmap);
    if (logicalSize.width() <= maxSize.width() &&
        logicalSize.height() <= maxSize.height()) {
        return pixmap;
    }

    QSize targetLogicalSize = logicalSize;
    targetLogicalSize.scale(maxSize, Qt::KeepAspectRatio);

    const qreal dpr = pixmap.devicePixelRatio();
    const QSize targetPhysicalSize(
      std::max(1, static_cast<int>(std::ceil(targetLogicalSize.width() * dpr))),
      std::max(
        1, static_cast<int>(std::ceil(targetLogicalSize.height() * dpr))));

    QPixmap scaled = pixmap.scaled(targetPhysicalSize,
                                   Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    return scaled;
}

QRect centeredPinGeometry(const QPixmap& pixmap)
{
    QSize pinSize = logicalPixmapSize(pixmap);

    QRect geometry(QPoint(), pinSize);
    QScreen* screen = currentOrPrimaryScreen();
    if (screen) {
        geometry.moveCenter(screen->availableGeometry().center());
    }
    return geometry;
}

QPixmap pixmapFromImageData(const QVariant& imageData)
{
    if (imageData.canConvert<QImage>()) {
        const QImage image = qvariant_cast<QImage>(imageData);
        if (!image.isNull()) {
            return QPixmap::fromImage(image);
        }
    }
    if (imageData.canConvert<QPixmap>()) {
        const QPixmap pixmap = qvariant_cast<QPixmap>(imageData);
        if (!pixmap.isNull()) {
            return pixmap;
        }
    }
    return {};
}

QPixmap pixmapFromImageUrl(const QUrl& url)
{
    if (!url.isLocalFile()) {
        return {};
    }

    QImageReader reader(url.toLocalFile());
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }
    return QPixmap::fromImage(image);
}

QPixmap renderTextPin(const QString& text)
{
    QScreen* screen = currentOrPrimaryScreen();
    const QSize available =
      screen ? screen->availableGeometry().size() : QSize(1600, 900);
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;

    QFont font = QApplication::font();
    const int pointSize = font.pointSize() > 0 ? font.pointSize() : 11;
    font.setPointSize(std::max(pointSize, 12));

    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    QTextDocument doc;
    doc.setDefaultFont(font);
    doc.setDefaultTextOption(option);
    doc.setDocumentMargin(0);
    doc.setPlainText(text);

    const int padding = 22;
    const int minTextWidth = 260;
    const int maxTextWidth =
      std::clamp(static_cast<int>(available.width() * 0.55), 420, 920);
    doc.setTextWidth(maxTextWidth);
    const int textWidth =
      std::clamp(static_cast<int>(std::ceil(doc.idealWidth())),
                 minTextWidth,
                 maxTextWidth);
    doc.setTextWidth(textWidth);

    const QSize logicalSize(textWidth + padding * 2,
                            static_cast<int>(std::ceil(doc.size().height())) +
                              padding * 2);
    QPixmap pixmap(logicalSize * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QRectF card(QPointF(0, 0), QSizeF(logicalSize));
    card.adjust(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(card, 6, 6);
    painter.fillPath(path, QColor(250, 250, 250));
    painter.setPen(QPen(QColor(200, 200, 200), 1));
    painter.drawPath(path);

    painter.translate(padding, padding);
    doc.drawContents(&painter);
    return pixmap;
}

} // namespace

Flameshot::Flameshot()
  : m_haveExternalWidget(false)
  , m_captureWindow(nullptr)
#if (defined(Q_OS_MACOS) || defined(Q_OS_WIN))
  , m_HotkeyScreenshotCapture(nullptr)
#endif
#if (defined(Q_OS_MACOS) && ENABLE_IMGUR)
  , m_HotkeyScreenshotHistory(nullptr)
#endif
{
    QString StyleSheet = CaptureButton::globalStyleSheet();
    qApp->setStyleSheet(StyleSheet);

#if defined(Q_OS_MACOS)
    // Request Screen Recording permission via the proper CoreGraphics API
    if (!CGPreflightScreenCaptureAccess()) {
        CGRequestScreenCaptureAccess();
    }
#endif
#if (defined(Q_OS_MACOS) || defined(Q_OS_WIN))
    // Set global shortcuts for MacOS or Windows
    m_HotkeyScreenshotCapture = new QHotkey(
      QKeySequence(ConfigHandler().shortcut("TAKE_SCREENSHOT")), true, this);
    QObject::connect(m_HotkeyScreenshotCapture,
                     &QHotkey::activated,
                     qApp,
                     [this]() { gui(); });
#endif
#if (defined(Q_OS_MACOS) && ENABLE_IMGUR)
    m_HotkeyScreenshotHistory = new QHotkey(
      QKeySequence(ConfigHandler().shortcut("SCREENSHOT_HISTORY")), true, this);
    QObject::connect(m_HotkeyScreenshotHistory,
                     &QHotkey::activated,
                     qApp,
                     [this]() { history(); });
#endif
}

Flameshot* Flameshot::instance()
{
    static Flameshot c;
    return &c;
}

CaptureWidget* Flameshot::gui(const CaptureRequest& req)
{
    if (!resolveAnyConfigErrors()) {
        return nullptr;
    }

#if defined(Q_OS_MACOS)
    // This is required on MacOS because of Mission Control. If you'll switch to
    // another Desktop you cannot take a new screenshot from the tray, you have
    // to switch back to the Flameshot Desktop manually. It is not obvious and a
    // large number of users are confused and report a bug.
    if (m_captureWindow != nullptr) {
        m_captureWindow->close();
        delete m_captureWindow;
        m_captureWindow = nullptr;
    }
#endif

    if (nullptr == m_captureWindow) {
        // TODO is this unnecessary now?
        int timeout = 5000; // 5 seconds
        const int delay = 100;
        QWidget* modalWidget = nullptr;
        for (; timeout >= 0; timeout -= delay) {
            modalWidget = qApp->activeModalWidget();
            if (nullptr == modalWidget) {
                break;
            }
            modalWidget->close();
            modalWidget->deleteLater();
            QThread::msleep(delay);
        }
        if (0 == timeout) {
            QMessageBox::warning(
              nullptr, tr("Error"), tr("Unable to close active modal widgets"));
            return nullptr;
        }

        m_captureWindow = new CaptureWidget(req);

#ifdef Q_OS_WIN
        m_captureWindow->show();
#elif defined(Q_OS_MACOS)
        if (ConfigHandler().useNativeFullscreen()) {
            m_captureWindow->showFullScreen();
        } else {
            m_captureWindow->show();
        }
        m_captureWindow->activateWindow();
        m_captureWindow->raise();
#else
        m_captureWindow->showFullScreen();
//        m_captureWindow->show(); // For CaptureWidget Debugging under Linux
#endif
        return m_captureWindow;
    } else {
        emit captureFailed();
        return nullptr;
    }
}

void Flameshot::pinImage()
{
    if (!resolveAnyConfigErrors()) {
        return;
    }

    QStringList patterns;
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    for (const QByteArray& format : formats) {
        patterns << QStringLiteral("*.%1")
                      .arg(QString::fromLatin1(format).toLower());
    }
    patterns.removeDuplicates();
    patterns.sort(Qt::CaseInsensitive);

    const QString filter =
      patterns.isEmpty()
        ? tr("Images (*)")
        : tr("Images (%1)").arg(patterns.join(QLatin1Char(' ')));
    const QString path =
      QFileDialog::getOpenFileName(nullptr, tr("Pin Image"), QString(), filter);
    if (path.isEmpty()) {
        return;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        QMessageBox::warning(
          nullptr,
          tr("Error"),
          tr("Unable to open image:\n%1").arg(reader.errorString()));
        return;
    }

    const QPixmap pixmap = constrainPixmapToScreen(QPixmap::fromImage(image));
    FlameshotDaemon::createPin(pixmap, centeredPinGeometry(pixmap));
}

void Flameshot::pinClipboard()
{
    if (!resolveAnyConfigErrors()) {
        return;
    }

    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
    if (!mimeData) {
        return;
    }

    QPixmap pixmap;
    bool clipboardPixmapShouldFitScreen = false;
    if (mimeData->hasImage()) {
        pixmap = pixmapFromImageData(mimeData->imageData());
        clipboardPixmapShouldFitScreen = !pixmap.isNull();
    }

    if (pixmap.isNull() && mimeData->hasUrls()) {
        for (const QUrl& url : mimeData->urls()) {
            pixmap = pixmapFromImageUrl(url);
            if (!pixmap.isNull()) {
                clipboardPixmapShouldFitScreen = true;
                break;
            }
        }
    }

    if (pixmap.isNull() && mimeData->hasText()) {
        const QString text = mimeData->text();
        if (!text.isEmpty()) {
            pixmap = renderTextPin(text);
        }
    }

    if (pixmap.isNull()) {
        QMessageBox::information(
          nullptr,
          tr("Pin Clipboard"),
          tr("The clipboard does not contain an image or text."));
        return;
    }

    if (clipboardPixmapShouldFitScreen) {
        pixmap = constrainPixmapToScreen(pixmap);
    }

    FlameshotDaemon::createPin(pixmap, centeredPinGeometry(pixmap));
}

void Flameshot::screen(CaptureRequest req, const int screenNumber)
{
    if (!resolveAnyConfigErrors()) {
        return;
    }

    bool ok = false;
    QPixmap p;
    QRect geometry;

    if (screenNumber < 0) {
        ScreenGrabber grabber;
        p = grabber.grabEntireDesktop(ok);
        if (ok) {
            QScreen* selectedScreen = grabber.getSelectedScreen();
            if (selectedScreen) {
                geometry = ScreenGrabber().screenGeometry(selectedScreen);
            } else {
                ok = false;
            }
        }
    } else if (screenNumber >= qApp->screens().count()) {
        AbstractLogger() << QObject::tr(
          "Requested screen exceeds screen count");
        ok = false;
    } else {
        // Specific screen number provided - use grabScreen to bypass selector
        QScreen* screen = qApp->screens()[screenNumber];
        p = ScreenGrabber().grabScreen(screen, ok);
        if (ok) {
            geometry = ScreenGrabber().screenGeometry(screen);
        }
    }

    if (ok) {
        QRect region = req.initialSelection();
        if (region.isNull()) {
            region = geometry;
        } else {
            QRect screenGeom = geometry;
            screenGeom.moveTopLeft({ 0, 0 });
            region = region.intersected(screenGeom);
            p = p.copy(region);
        }
        if (req.tasks() & CaptureRequest::PIN) {
            // change geometry for pin task
            req.addPinTask(region);
        }
        exportCapture(p, geometry, req);
    } else {
        emit captureFailed();
    }
}

void Flameshot::full(const CaptureRequest& req)
{
    if (!resolveAnyConfigErrors()) {
        return;
    }

    bool ok = true;
    QPixmap p(ScreenGrabber().grabFullDesktop(ok));
    if (ok) {
        QRect selection; // `flameshot full` does not support region selection
        exportCapture(p, selection, req);
    } else {
        emit captureFailed();
    }
}

void Flameshot::launcher()
{
    if (!resolveAnyConfigErrors()) {
        return;
    }

    if (m_launcherWindow == nullptr) {
        m_launcherWindow = new CaptureLauncher();
    }
    m_launcherWindow->show();
#if defined(Q_OS_MACOS)
    showDockIcon(m_launcherWindow);
#endif
}

void Flameshot::config()
{
    if (!resolveAnyConfigErrors()) {
        return;
    }

    if (m_configWindow == nullptr) {
        m_configWindow = new ConfigWindow();
        m_configWindow->show();
        // Call show() first, otherwise the correct geometry cannot be fetched
        // for centering the window on the screen
        QRect position = m_configWindow->frameGeometry();
        QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
        position.moveCenter(currentScreen->availableGeometry().center());
        m_configWindow->move(position.topLeft());
#if defined(Q_OS_MACOS)
        showDockIcon(m_configWindow);
#endif
    }
}

void Flameshot::info()
{
    if (m_infoWindow == nullptr) {
        m_infoWindow = new InfoWindow();
#if defined(Q_OS_MACOS)
        showDockIcon(m_infoWindow);
#endif
    }
}

#ifdef ENABLE_IMGUR
void Flameshot::history()
{
    static UploadHistory* historyWidget = nullptr;
    if (historyWidget == nullptr) {
        historyWidget = new UploadHistory;
        historyWidget->loadHistory();
        connect(historyWidget, &QObject::destroyed, this, []() {
            historyWidget = nullptr;
        });
    }

    historyWidget->show();
    // Call show() first, otherwise the correct geometry cannot be fetched
    // for centering the window on the screen
    QRect position = historyWidget->frameGeometry();
    QScreen* currentScreen = QGuiAppCurrentScreen().currentScreen();
    position.moveCenter(currentScreen->availableGeometry().center());
    historyWidget->move(position.topLeft());

#if defined(Q_OS_MACOS)
    showDockIcon(historyWidget);
#endif
}
#endif

#if defined(Q_OS_MACOS)
void Flameshot::onWindowVisibilityChanged(QWindow::Visibility newVisibility)
{
    auto* qw = qobject_cast<QWindow*>(sender());
    if (!qw) {
        return;
    }

    if (newVisibility == QWindow::Hidden) {
        qw->setProperty(visibleInDockProperty, false);
        --m_dockIconVisibleCount;
        if (m_dockIconVisibleCount == 0) {
            setActivationPolicyAccessory();
        }
    } else {
        bool windowTrackedInDock = qw->property(visibleInDockProperty).toBool();
        if (!windowTrackedInDock) {
            qw->setProperty(visibleInDockProperty, true);
            ++m_dockIconVisibleCount;
            setActivationPolicyRegular();
        }
    }
}

void Flameshot::showDockIcon(QWidget* w)
{
    QWindow* qw = w->windowHandle();
    if (!qw) {
        return;
    }

    connect(qw,
            &QWindow::visibilityChanged,
            this,
            &Flameshot::onWindowVisibilityChanged);
}
#endif

void Flameshot::openSavePath()
{
    QString savePath = ConfigHandler().savePath();
    if (!savePath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(savePath));
    }
}

QVersionNumber Flameshot::getVersion()
{
    return QVersionNumber::fromString(
      QStringLiteral(APP_VERSION).replace("v", ""));
}

void Flameshot::setOrigin(Origin origin)
{
    m_origin = origin;
}

Flameshot::Origin Flameshot::origin()
{
    return m_origin;
}

/**
 * @brief Prompt the user to resolve config errors if necessary.
 * @return Whether errors were resolved.
 */
bool Flameshot::resolveAnyConfigErrors()
{
    bool resolved = true;
    ConfigHandler confighandler;
    if (!confighandler.checkUnrecognizedSettings() ||
        !confighandler.checkSemantics()) {
        auto* resolver = new ConfigResolver();
        QObject::connect(
          resolver, &ConfigResolver::rejected, [resolver, &resolved]() {
              resolved = false;
              resolver->deleteLater();
              if (origin() == CLI) {
                  exit(1);
              }
          });
        QObject::connect(
          resolver, &ConfigResolver::accepted, [resolver, &resolved]() {
              resolved = true;
              resolver->close();
              resolver->deleteLater();
              // Ensure that the dialog is closed before starting capture
              qApp->processEvents();
          });
        resolver->exec();
        qApp->processEvents();
    }
    return resolved;
}

void Flameshot::requestCapture(const CaptureRequest& request)
{
    if (!resolveAnyConfigErrors()) {
        return;
    }

    switch (request.captureMode()) {
        case CaptureRequest::FULLSCREEN_MODE:
            QTimer::singleShot(request.delay(),
                               [this, request] { full(request); });
            break;
        case CaptureRequest::SCREEN_MODE: {
            int&& number = request.data().toInt();
            QTimer::singleShot(request.delay(), [this, request, number]() {
                screen(request, number);
            });
            break;
        }
        case CaptureRequest::GRAPHICAL_MODE: {
            QTimer::singleShot(
              request.delay(), this, [this, request]() { gui(request); });
            break;
        }
        default:
            emit captureFailed();
            break;
    }
}

void Flameshot::exportCapture(const QPixmap& capture,
                              QRect& selection,
                              const CaptureRequest& req)
{
    using CR = CaptureRequest;
    int tasks = req.tasks(), mode = req.captureMode();
    QString path = req.path();

    if (tasks & CR::PRINT_GEOMETRY) {
        QTextStream(stdout)
          << selection.width() << "x" << selection.height() << "+"
          << selection.x() << "+" << selection.y() << "\n";
    }

    if (tasks & CR::PRINT_RAW) {
        QByteArray byteArray;
        QBuffer buffer(&byteArray);
        capture.save(&buffer, "PNG");
        if (QFile file; file.open(stdout, QIODevice::WriteOnly)) {
            file.write(byteArray);
            file.close();
        }
    }

    if (tasks & CR::SAVE) {
        if (req.path().isEmpty()) {
            saveToFilesystemGUI(capture);
        } else {
            saveToFilesystem(capture, path);
        }
    }

    if (tasks & CR::COPY) {
        FlameshotDaemon::copyToClipboard(capture);
    }

    if (tasks & CR::PIN) {
        FlameshotDaemon::createPin(capture, selection);
        if (mode == CR::SCREEN_MODE || mode == CR::FULLSCREEN_MODE) {
            AbstractLogger::info()
              << QObject::tr("Full screen screenshot pinned to screen");
        }
    }

#ifdef ENABLE_IMGUR
    if (tasks & CR::UPLOAD) {
        if (!ConfigHandler().uploadWithoutConfirmation()) {
            auto* dialog = new ImgUploadDialog();
            if (dialog->exec() == QDialog::Rejected) {
                return;
            }
        }

        ImgUploaderBase* widget = ImgUploaderManager().uploader(capture);
        widget->show();
        widget->activateWindow();
        // NOTE: lambda can't capture 'this' because it might be destroyed later
        CR::ExportTask tasks = tasks;
        QObject::connect(
          widget, &ImgUploaderBase::uploadOk, [=, this](const QUrl& url) {
              if (ConfigHandler().copyURLAfterUpload()) {
                  if (!(tasks & CR::COPY)) {
                      FlameshotDaemon::copyToClipboard(
                        url.toString(), tr("URL copied to clipboard."));
                  }
                  widget->showPostUploadDialog();
              }
          });
    }
#endif

    if (!(tasks & CR::UPLOAD)) {
        emit captureTaken(capture);
    }
}

void Flameshot::setExternalWidget(bool b)
{
    m_haveExternalWidget = b;
}
bool Flameshot::haveExternalWidget()
{
    return m_haveExternalWidget;
}

// STATIC ATTRIBUTES
Flameshot::Origin Flameshot::m_origin = Flameshot::DAEMON;
