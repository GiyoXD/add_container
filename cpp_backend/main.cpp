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
#ifdef _WIN32
    // Ensure only a single instance of the application runs at any time
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"VisionLogisticsSingleInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hwnd = FindWindowW(NULL, L"Vision Logistics Data Entry");
        if (hwnd) {
            if (IsIconic(hwnd)) {
                ShowWindow(hwnd, SW_RESTORE);
            }
            SetForegroundWindow(hwnd);
        }
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }
#endif

    // Ensure Qt finds plugins (platforms/qwindows.dll, sqldrivers/qsqlite.dll) in application directory
    QString appDir = QCoreApplication::applicationDirPath();
    QCoreApplication::addLibraryPath(appDir);

    QApplication a(argc, argv);

    try {
        MainWindow w;
        w.show();
        int result = a.exec();

#ifdef _WIN32
        if (hMutex) {
            CloseHandle(hMutex);
        }
#endif
        return result;
    } catch (const std::exception& e) {
#ifdef _WIN32
        if (hMutex) CloseHandle(hMutex);
        MessageBoxA(NULL, e.what(), "Startup Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    } catch (...) {
#ifdef _WIN32
        if (hMutex) CloseHandle(hMutex);
        MessageBoxA(NULL, "An unknown error occurred during startup.", "Startup Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
