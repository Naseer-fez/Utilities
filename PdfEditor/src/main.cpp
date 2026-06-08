#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QIcon>
#include "ui/PdfView.h"
#include "core/OverlayItem.h"
#include "core/OverlayModel.h"

int main(int argc, char *argv[])
{
    // Force Fusion style for custom look and feel and to avoid customization errors
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");

    QGuiApplication app(argc, argv);

    // Register custom C++ types to QML
    qmlRegisterType<PdfEditor::PdfView>("PdfEditor", 1, 0, "PdfView");
    qmlRegisterUncreatableType<PdfEditor::OverlayItem>("PdfEditor", 1, 0, "OverlayItem", "Cannot create OverlayItem in QML");
    qmlRegisterUncreatableType<PdfEditor::TextOverlay>("PdfEditor", 1, 0, "TextOverlay", "Cannot create TextOverlay in QML");
    qmlRegisterUncreatableType<PdfEditor::HighlightOverlay>("PdfEditor", 1, 0, "HighlightOverlay", "Cannot create HighlightOverlay in QML");
    qmlRegisterType<PdfEditor::OverlayModel>("PdfEditor", 1, 0, "OverlayModel");

    QQmlApplicationEngine engine;
    
    // Crucial: Add qrc:/ to import path so QML module can find its components in resource system
    engine.addImportPath("qrc:/");

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("PdfEditor", "Main");

    return app.exec();
}
