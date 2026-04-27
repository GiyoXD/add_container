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
#include "GeminiClient.h"
#include "GoogleSheetsClient.h"
#include "DatabaseManager.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void browseImage();
    void startProcessing();
    void fetchSheetData();
    void onDataFetched(const QList<QList<CellData>>& rows);
    void onFilterChanged(const QString& text);
    void onGeminiFinished(const QList<DataRow>& rows);
    void onGeminiError(const QString& message);
    void onGoogleFinished();
    void onGoogleError(const QString& message);
    void log(const QString& message);
    void saveConfig();
    void toggleConfig();
    void toggleLog();

private:
    QLineEdit *m_imgPathEdit;
    QPlainTextEdit *m_idsEdit;
    QPlainTextEdit *m_logEdit;
    QPushButton *m_processBtn;
    
    QPushButton *m_fetchBtn;
    QLineEdit *m_filterEdit;
    QTableView *m_tableView;
    QStandardItemModel *m_tableModel;
    QSortFilterProxyModel *m_proxyModel;

    // Config UI
    QGroupBox *m_configGroup;
    QLineEdit *m_geminiKeyEdit;
    QLineEdit *m_aiModelEdit;
    QLineEdit *m_spreadsheetIdEdit;
    QPlainTextEdit *m_googleSecretEdit;
    QPushButton *m_saveConfigBtn;
    QPushButton *m_toggleConfigBtn;

    // Log UI
    QGroupBox *m_logGroup;
    QPushButton *m_toggleLogBtn;

    GeminiClient *m_geminiClient;
    GoogleSheetsClient *m_sheetsClient;
    DatabaseManager *m_dbManager;

    void setupUi();
    void loadConfig();

    QString m_geminiApiKey;
    QString m_aiModelName;
    QString m_spreadsheetId;
    QString m_googleSecretData; // Can be path or raw JSON
};

#endif // MAINWINDOW_H
