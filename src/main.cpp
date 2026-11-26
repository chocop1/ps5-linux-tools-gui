#include <QApplication>
#include <QFont>
#include <QToolTip>

#include "config.h"
#include "mainwindow.h"
#include "workers.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(ps5::APP_NAME);
    app.setOrganizationName(ps5::ORG_NAME);
    app.setDesktopFileName("ps5tools_cpp");
    app.setWindowIcon(ps5::loadAppIcon());

    ps5::registerMetaTypes();

    // Capture KDE platform style & palette before user overrides
    ps5::captureInitialTheme(app);
    ps5::setActiveLanguage(ps5::getLanguageMode(), false);

    ps5::KDETheme theme;
    ps5::applyAppAppearance(app, theme, {}, {}, false);

    QToolTip::setFont(QFont("Noto Sans", 10));

    ps5::PS5ToolsWindow window(&theme);
    window.show();
    return app.exec();
}
