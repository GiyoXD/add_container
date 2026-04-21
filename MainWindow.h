#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
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

    GeminiClient *m_geminiClient;
    GoogleSheetsClient *m_sheetsClient;
    DatabaseManager *m_dbManager;

    void setupUi();
};

#endif // MAINWINDOW_H
