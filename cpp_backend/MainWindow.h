#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QTableView>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QFrame>
#include <QScrollArea>
#include <QGridLayout>
#include <QTabWidget>
#include <QVector>
#include "GeminiClient.h"
#include "GoogleSheetsClient.h"
#include "DatabaseManager.h"

struct SheetTabInfo {
    QString sheetName;          // "2026" or "LOCAL  SUPPLY"
    QString tabTitle;           // "2026" or "LOCAL SUPPLY"
    QString fetchRange;         // "2026!A:Z" or "'LOCAL  SUPPLY'!A:Z"
    int crossColumnIndex = 0;   // 7 for 2026, 5 for LOCAL SUPPLY
    QString crossColumnLetter;  // "G" for 2026, "J" for LOCAL SUPPLY
    
    QPushButton *fetchBtn = nullptr;
    QLineEdit *filterEdit = nullptr;
    QTableView *tableView = nullptr;
    QStandardItemModel *tableModel = nullptr;
    QSortFilterProxyModel *proxyModel = nullptr;

    // Red Invoice Alert UI
    QFrame *redInvoiceFrame = nullptr;
    QLabel *redInvoiceCountLabel = nullptr;
    QPushButton *toggleRedListBtn = nullptr;
    QWidget *redBadgesWidget = nullptr;
    QGridLayout *redBadgesLayout = nullptr;

    // Green Invoice Alert UI
    QFrame *crossTodayFrame = nullptr;
    QLabel *crossTodayCountLabel = nullptr;
    QPushButton *toggleCrossTodayListBtn = nullptr;
    QWidget *crossTodayBadgesWidget = nullptr;
    QGridLayout *crossTodayBadgesLayout = nullptr;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void browseImage();
    void startProcessing();
    void fetchSheetData(const QString& sheetName);
    void onDataFetched(const QString& sheetName, const QList<QList<CellData>>& rows);
    void onGeminiFinished(const QList<DataRow>& rows);
    void onGeminiError(const QString& message);
    void onGoogleFinished();
    void onGoogleError(const QString& message);
    void log(const QString& message);
    void saveConfig();
    void toggleConfig();
    void toggleLog();
    void toggleAiInput();
    void updateActionButtons();
    void onCrossButtonClicked();
    void onToggleRedList();
    void onRedBadgeClicked();
    void onToggleCrossTodayList();
    void onCrossTodayBadgeClicked();
    void toggleEditMode();
    void onClearButtonClicked();

private:
    bool isRedColor(const QColor& color);
    bool isGreenColor(const QColor& color);
    void formatDateIfSerial(QString& val);
    QWidget* createSheetTabWidget(int tabIdx, const QStringList& headers, const QString& fetchBtnText);
    void populateSheetData(int tabIdx, const QList<QList<CellData>>& rows);
    void updateTabActionButtons(int tabIdx);

    QLineEdit *m_imgPathEdit = nullptr;
    QLineEdit *m_idsEdit = nullptr;
    QPlainTextEdit *m_logEdit = nullptr;
    QPushButton *m_processBtn = nullptr;

    QTabWidget *m_tabWidget = nullptr;
    QVector<SheetTabInfo> m_tabs;

    // Config UI
    QGroupBox *m_configGroup = nullptr;
    QLineEdit *m_geminiKeyEdit = nullptr;
    QLineEdit *m_aiModelEdit = nullptr;
    QLineEdit *m_spreadsheetIdEdit = nullptr;
    QPlainTextEdit *m_googleSecretEdit = nullptr;
    QPushButton *m_saveConfigBtn = nullptr;
    QPushButton *m_toggleConfigBtn = nullptr;

    // Log UI
    QGroupBox *m_logGroup = nullptr;
    QPushButton *m_toggleLogBtn = nullptr;

    // AI Input UI
    QGroupBox *m_aiInputGroup = nullptr;
    QPushButton *m_toggleAiInputBtn = nullptr;
    QPushButton *m_toggleEditModeBtn = nullptr;

    GeminiClient *m_geminiClient = nullptr;
    GoogleSheetsClient *m_sheetsClient = nullptr;
    DatabaseManager *m_dbManager = nullptr;

    void setupUi();
    void loadConfig();

    QString m_geminiApiKey;
    QString m_aiModelName;
    QString m_spreadsheetId;
    QString m_googleSecretData; // Can be path or raw JSON
    bool m_isEditMode;
};

#endif // MAINWINDOW_H
