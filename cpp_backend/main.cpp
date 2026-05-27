#include <QApplication>
#include "MainWindow.h"

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // Create a unique named mutex for the current user session
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "Local\\AddContainerSingleInstanceMutex_JPZ031127");
    if (hMutex == NULL) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance is already running. Find the existing window.
        HWND hwnd = FindWindowA(NULL, "Vision Logistics Data Entry");
        if (hwnd) {
            // Restore window if minimized
            if (IsIconic(hwnd)) {
                ShowWindow(hwnd, SW_RESTORE);
            }
            // Bring to foreground
            SetForegroundWindow(hwnd);
        }
        CloseHandle(hMutex);
        return 0;
    }
#endif

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    
    int result = a.exec();

#ifdef _WIN32
    CloseHandle(hMutex);
#endif

    return result;
}
