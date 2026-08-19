#include <QApplication>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include "MainWindow.h"

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char *argv[]) {
    // Ensure Qt finds plugins (platforms/qwindows.dll, sqldrivers/qsqlite.dll) in application directory
    QString appDir = QCoreApplication::applicationDirPath();
    QCoreApplication::addLibraryPath(appDir);

    QApplication a(argc, argv);

    try {
        MainWindow w;
        w.show();
        return a.exec();
    } catch (const std::exception& e) {
#ifdef _WIN32
        MessageBoxA(NULL, e.what(), "Startup Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    } catch (...) {
#ifdef _WIN32
        MessageBoxA(NULL, "An unknown error occurred during startup.", "Startup Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
