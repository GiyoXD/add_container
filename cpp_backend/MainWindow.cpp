#include "MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QGroupBox>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDialog>
#include <QDateEdit>
#include <QLocale>
#include <QInputDialog>
#include <QShortcut>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_isEditMode(false) {
    setupUi();
    loadConfig();

    QString baseDir = QCoreApplication::applicationDirPath();
    // In dev, it might be in the source dir
    #ifdef QT_DEBUG
    baseDir = QDir::currentPath();
    #endif

    m_dbManager = new DatabaseManager(baseDir + "/shipping_data.db", this);
    if (!m_dbManager->setupDatabase()) {
        log("CRITICAL: Failed to setup local database.");
    }

    // Load cached data for both sheets if available
    QList<QList<CellData>> cached2026 = m_dbManager->loadSheetCache("2026");
    if (!cached2026.isEmpty()) {
        log(QString("Loaded %1 rows from local cache for '2026'.").arg(cached2026.size()));
        populateSheetData(0, cached2026);
    }

    QList<QList<CellData>> cachedLocal = m_dbManager->loadSheetCache("LOCAL  SUPPLY");
    if (!cachedLocal.isEmpty()) {
        log(QString("Loaded %1 rows from local cache for 'LOCAL  SUPPLY'.").arg(cachedLocal.size()));
        populateSheetData(1, cachedLocal);
    }

    m_geminiClient = new GeminiClient(m_geminiApiKey, m_aiModelName, this);
    m_sheetsClient = new GoogleSheetsClient(m_googleSecretData, m_spreadsheetId, this);

    connect(m_geminiClient, &GeminiClient::statusUpdate, this, &MainWindow::log);
    connect(m_geminiClient, &GeminiClient::finished, this, &MainWindow::onGeminiFinished);
    connect(m_geminiClient, &GeminiClient::error, this, &MainWindow::onGeminiError);

    connect(m_sheetsClient, &GoogleSheetsClient::statusUpdate, this, &MainWindow::log);
    connect(m_sheetsClient, &GoogleSheetsClient::finished, this, &MainWindow::onGoogleFinished);
    connect(m_sheetsClient, &GoogleSheetsClient::error, this, &MainWindow::onGoogleError);
    connect(m_sheetsClient, static_cast<void (GoogleSheetsClient::*)(const QString&, const QList<QList<CellData>>&)>(&GoogleSheetsClient::dataFetched),
            this, &MainWindow::onDataFetched);
}

MainWindow::~MainWindow() {}

void MainWindow::setupUi() {
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *layout = new QVBoxLayout(central);

    QLabel *title = new QLabel("Vision Logistics Data Entry", this);
    title->setStyleSheet("font-size: 18pt; font-weight: bold;");
    title->setAlignment(Qt::AlignCenter);
    
    m_toggleAiInputBtn = new QPushButton("⚡ AI Data Entry", this);
    m_toggleAiInputBtn->setFixedWidth(120);
    m_toggleEditModeBtn = new QPushButton("🔒 Edit Mode: OFF", this);
    m_toggleEditModeBtn->setFixedWidth(130);
    m_toggleEditModeBtn->setStyleSheet("background-color: #f5f5f5; border: 1px solid #ccc; border-radius: 4px; padding: 4px; font-weight: bold;");
    m_toggleConfigBtn = new QPushButton("⚙ Settings", this);
    m_toggleConfigBtn->setFixedWidth(100);

    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->addWidget(title);
    headerLayout->addWidget(m_toggleAiInputBtn);
    headerLayout->addWidget(m_toggleConfigBtn);
    headerLayout->addWidget(m_toggleEditModeBtn);
    layout->addLayout(headerLayout);

    // Config Panel (Hidden by default)
    m_configGroup = new QGroupBox("Configuration", this);
    QVBoxLayout *vConfig = new QVBoxLayout(m_configGroup);
    
    m_geminiKeyEdit = new QLineEdit(this);
    m_geminiKeyEdit->setPlaceholderText("Gemini API Key");
    m_aiModelEdit = new QLineEdit(this);
    m_aiModelEdit->setPlaceholderText("Gemini Model Name (e.g., gemini-1.5-flash)");
    m_spreadsheetIdEdit = new QLineEdit(this);
    m_spreadsheetIdEdit->setPlaceholderText("Spreadsheet ID");
    m_googleSecretEdit = new QPlainTextEdit(this);
    m_googleSecretEdit->setPlaceholderText("Google Secret JSON (Paste content here)");
    m_googleSecretEdit->setMaximumHeight(100);
    
    m_saveConfigBtn = new QPushButton("Save & Update Credentials", this);
    m_saveConfigBtn->setStyleSheet("background-color: #2196F3; color: white;");
    
    vConfig->addWidget(new QLabel("Gemini API Key:"));
    vConfig->addWidget(m_geminiKeyEdit);
    vConfig->addWidget(new QLabel("Gemini AI Model:"));
    vConfig->addWidget(m_aiModelEdit);
    vConfig->addWidget(new QLabel("Spreadsheet ID:"));
    vConfig->addWidget(m_spreadsheetIdEdit);
    vConfig->addWidget(new QLabel("Google Secret JSON:"));
    vConfig->addWidget(m_googleSecretEdit);
    vConfig->addWidget(m_saveConfigBtn);
    
    m_configGroup->setVisible(false);
    layout->addWidget(m_configGroup);

    // Compact AI Data Entry Tool (Hidden by default)
    m_aiInputGroup = new QGroupBox("AI Data Entry Tool", this);
    QHBoxLayout *hAi = new QHBoxLayout(m_aiInputGroup);
    
    // Manifest Image
    QVBoxLayout *vImage = new QVBoxLayout();
    vImage->addWidget(new QLabel("Manifest Image:", this));
    QHBoxLayout *hImageBrowse = new QHBoxLayout();
    m_imgPathEdit = new QLineEdit(this);
    m_imgPathEdit->setPlaceholderText("Select image path...");
    QPushButton *browseBtn = new QPushButton("Browse", this);
    hImageBrowse->addWidget(m_imgPathEdit);
    hImageBrowse->addWidget(browseBtn);
    vImage->addLayout(hImageBrowse);
    hAi->addLayout(vImage, 2);

    // Invoice IDs
    QVBoxLayout *vIds = new QVBoxLayout();
    vIds->addWidget(new QLabel("Invoice IDs (comma-separated):", this));
    m_idsEdit = new QLineEdit(this);
    m_idsEdit->setPlaceholderText("INV-2026-001, INV-2026-002");
    vIds->addWidget(m_idsEdit);
    hAi->addLayout(vIds, 2);

    // Extract & Sync button
    QVBoxLayout *vProcess = new QVBoxLayout();
    vProcess->addWidget(new QLabel("", this)); // alignment spacer label
    m_processBtn = new QPushButton("Extract & Sync", this);
    m_processBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold; padding: 6px;");
    vProcess->addWidget(m_processBtn);
    hAi->addLayout(vProcess, 1);

    m_aiInputGroup->setVisible(false);
    layout->addWidget(m_aiInputGroup);

    // Log Toggle
    m_toggleLogBtn = new QPushButton("Show Status Log", this);
    m_toggleLogBtn->setFixedWidth(120);
    layout->addWidget(m_toggleLogBtn);

    // Log
    m_logGroup = new QGroupBox("Status/Log", this);
    QVBoxLayout *vLog = new QVBoxLayout(m_logGroup);
    m_logEdit = new QPlainTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setBackgroundRole(QPalette::AlternateBase);
    vLog->addWidget(m_logEdit);
    m_logGroup->setVisible(false);
    layout->addWidget(m_logGroup);

    // Tab Widget hosting "2026" and "LOCAL SUPPLY"
    m_tabWidget = new QTabWidget(this);
    m_tabs.resize(2);

    // Tab 0: "2026"
    m_tabs[0].sheetName = "2026";
    m_tabs[0].tabTitle = "2026";
    m_tabs[0].fetchRange = "2026!A:Z";
    m_tabs[0].crossColumnIndex = 7;
    m_tabs[0].crossColumnLetter = "G";
    QStringList headers2026 = {"Client", "IFL-CLIENT", "Invoice No", "Ref No", "Invoice Date", "Container (2026)", "Bill (2026)", "Cross Border"};

    // Tab 1: "LOCAL SUPPLY"
    m_tabs[1].sheetName = "LOCAL  SUPPLY";
    m_tabs[1].tabTitle = "LOCAL SUPPLY";
    m_tabs[1].fetchRange = "'LOCAL  SUPPLY'!A:Z";
    m_tabs[1].crossColumnIndex = 5;
    m_tabs[1].crossColumnLetter = "J";
    QStringList headersLocalSupply = {"N.O", "Client Name", "Invoice No", "Ref No", "Invoice Date", "Cross"};

    m_tabWidget->addTab(createSheetTabWidget(0, headers2026, "Update 2026 Data"), "2026");
    m_tabWidget->addTab(createSheetTabWidget(1, headersLocalSupply, "Update LOCAL SUPPLY Data"), "LOCAL SUPPLY");
    m_tabWidget->setCurrentIndex(0); // Tab 0 active by default on startup

    layout->addWidget(m_tabWidget, 1); // Added stretch factor 1 so it takes all available vertical space

    // Ctrl+F shortcut to focus and select search bar content in currently selected tab
    QShortcut *searchShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        int currentIdx = m_tabWidget->currentIndex();
        if (currentIdx >= 0 && currentIdx < m_tabs.size() && m_tabs[currentIdx].filterEdit) {
            m_tabs[currentIdx].filterEdit->setFocus();
            m_tabs[currentIdx].filterEdit->selectAll();
        }
    });

    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::browseImage);
    connect(m_processBtn, &QPushButton::clicked, this, &MainWindow::startProcessing);

    connect(m_toggleConfigBtn, &QPushButton::clicked, this, &MainWindow::toggleConfig);
    connect(m_saveConfigBtn, &QPushButton::clicked, this, &MainWindow::saveConfig);

    connect(m_toggleLogBtn, &QPushButton::clicked, this, &MainWindow::toggleLog);
    connect(m_toggleAiInputBtn, &QPushButton::clicked, this, &MainWindow::toggleAiInput);
    connect(m_toggleEditModeBtn, &QPushButton::clicked, this, &MainWindow::toggleEditMode);

    resize(950, 700);
    setWindowTitle("Vision Logistics Data Entry");
}

QWidget* MainWindow::createSheetTabWidget(int tabIdx, const QStringList& headers, const QString& fetchBtnText) {
    QWidget *container = new QWidget(this);
    QVBoxLayout *vTab = new QVBoxLayout(container);

    QHBoxLayout *hDataControls = new QHBoxLayout();
    m_tabs[tabIdx].fetchBtn = new QPushButton(fetchBtnText, container);
    m_tabs[tabIdx].filterEdit = new QLineEdit(container);
    m_tabs[tabIdx].filterEdit->setPlaceholderText("Search...");
    hDataControls->addWidget(m_tabs[tabIdx].fetchBtn);
    hDataControls->addWidget(m_tabs[tabIdx].filterEdit);
    vTab->addLayout(hDataControls);

    // Red Invoice Alert Panel (Hidden by default)
    m_tabs[tabIdx].redInvoiceFrame = new QFrame(container);
    m_tabs[tabIdx].redInvoiceFrame->setObjectName(QString("redInvoiceFrame_%1").arg(tabIdx));
    m_tabs[tabIdx].redInvoiceFrame->setStyleSheet(
        QString("QFrame#redInvoiceFrame_%1 { "
        "  border: 1px solid #f5c6cb; "
        "  border-left: 5px solid #dc3545; "
        "  border-radius: 4px; "
        "  background-color: #f8d7da; "
        "}").arg(tabIdx)
    );
    m_tabs[tabIdx].redInvoiceFrame->setVisible(false);

    QVBoxLayout *vRedMain = new QVBoxLayout(m_tabs[tabIdx].redInvoiceFrame);
    vRedMain->setContentsMargins(10, 8, 10, 8);
    vRedMain->setSpacing(4);

    QHBoxLayout *hRedTop = new QHBoxLayout();
    hRedTop->setContentsMargins(0, 0, 0, 0);

    QLabel *warningIcon = new QLabel("⚠️", m_tabs[tabIdx].redInvoiceFrame);
    warningIcon->setStyleSheet("font-size: 12pt;");

    m_tabs[tabIdx].redInvoiceCountLabel = new QLabel("We found 0 invoice(s) highlighted in red (yet to cross the border). Focus on these items first.", m_tabs[tabIdx].redInvoiceFrame);
    m_tabs[tabIdx].redInvoiceCountLabel->setStyleSheet("color: #721c24; font-weight: bold; font-size: 9pt;");

    m_tabs[tabIdx].toggleRedListBtn = new QPushButton("Show Invoices", m_tabs[tabIdx].redInvoiceFrame);
    m_tabs[tabIdx].toggleRedListBtn->setFixedWidth(110);
    m_tabs[tabIdx].toggleRedListBtn->setCursor(Qt::PointingHandCursor);
    m_tabs[tabIdx].toggleRedListBtn->setProperty("tabIdx", tabIdx);
    m_tabs[tabIdx].toggleRedListBtn->setStyleSheet(
        "QPushButton { "
        "  border: 1px solid #dc3545; "
        "  border-radius: 10px; "
        "  padding: 2px 6px; "
        "  color: #dc3545; "
        "  background-color: white; "
        "  font-weight: bold; "
        "  font-size: 8pt; "
        "} "
        "QPushButton:hover { "
        "  background-color: #dc3545; "
        "  color: white; "
        "}"
    );

    hRedTop->addWidget(warningIcon);
    hRedTop->addWidget(m_tabs[tabIdx].redInvoiceCountLabel, 1);
    hRedTop->addWidget(m_tabs[tabIdx].toggleRedListBtn);
    vRedMain->addLayout(hRedTop);

    m_tabs[tabIdx].redBadgesWidget = new QWidget(m_tabs[tabIdx].redInvoiceFrame);
    m_tabs[tabIdx].redBadgesWidget->setVisible(false);
    m_tabs[tabIdx].redBadgesLayout = new QGridLayout(m_tabs[tabIdx].redBadgesWidget);
    m_tabs[tabIdx].redBadgesLayout->setContentsMargins(0, 4, 0, 0);
    m_tabs[tabIdx].redBadgesLayout->setHorizontalSpacing(6);
    m_tabs[tabIdx].redBadgesLayout->setVerticalSpacing(6);
    vRedMain->addWidget(m_tabs[tabIdx].redBadgesWidget);

    vTab->addWidget(m_tabs[tabIdx].redInvoiceFrame);

    // Green Invoice Alert Panel (Hidden by default)
    m_tabs[tabIdx].crossTodayFrame = new QFrame(container);
    m_tabs[tabIdx].crossTodayFrame->setObjectName(QString("crossTodayFrame_%1").arg(tabIdx));
    m_tabs[tabIdx].crossTodayFrame->setStyleSheet(
        QString("QFrame#crossTodayFrame_%1 { "
        "  border: 1px solid #c3e6cb; "
        "  border-left: 5px solid #28a745; "
        "  border-radius: 4px; "
        "  background-color: #d4edda; "
        "}").arg(tabIdx)
    );
    m_tabs[tabIdx].crossTodayFrame->setVisible(false);

    QVBoxLayout *vGreenMain = new QVBoxLayout(m_tabs[tabIdx].crossTodayFrame);
    vGreenMain->setContentsMargins(10, 8, 10, 8);
    vGreenMain->setSpacing(4);

    QHBoxLayout *hGreenTop = new QHBoxLayout();
    hGreenTop->setContentsMargins(0, 0, 0, 0);

    QLabel *greenWarningIcon = new QLabel("✅", m_tabs[tabIdx].crossTodayFrame);
    greenWarningIcon->setStyleSheet("font-size: 12pt;");

    m_tabs[tabIdx].crossTodayCountLabel = new QLabel("We found 0 invoice(s) crossing today (green).", m_tabs[tabIdx].crossTodayFrame);
    m_tabs[tabIdx].crossTodayCountLabel->setStyleSheet("color: #155724; font-weight: bold; font-size: 9pt;");

    m_tabs[tabIdx].toggleCrossTodayListBtn = new QPushButton("Show Invoices", m_tabs[tabIdx].crossTodayFrame);
    m_tabs[tabIdx].toggleCrossTodayListBtn->setFixedWidth(110);
    m_tabs[tabIdx].toggleCrossTodayListBtn->setCursor(Qt::PointingHandCursor);
    m_tabs[tabIdx].toggleCrossTodayListBtn->setProperty("tabIdx", tabIdx);
    m_tabs[tabIdx].toggleCrossTodayListBtn->setStyleSheet(
        "QPushButton { "
        "  border: 1px solid #28a745; "
        "  border-radius: 10px; "
        "  padding: 2px 6px; "
        "  color: #28a745; "
        "  background-color: white; "
        "  font-weight: bold; "
        "  font-size: 8pt; "
        "} "
        "QPushButton:hover { "
        "  background-color: #28a745; "
        "  color: white; "
        "}"
    );

    hGreenTop->addWidget(greenWarningIcon);
    hGreenTop->addWidget(m_tabs[tabIdx].crossTodayCountLabel, 1);
    hGreenTop->addWidget(m_tabs[tabIdx].toggleCrossTodayListBtn);
    vGreenMain->addLayout(hGreenTop);

    m_tabs[tabIdx].crossTodayBadgesWidget = new QWidget(m_tabs[tabIdx].crossTodayFrame);
    m_tabs[tabIdx].crossTodayBadgesWidget->setVisible(false);
    m_tabs[tabIdx].crossTodayBadgesLayout = new QGridLayout(m_tabs[tabIdx].crossTodayBadgesWidget);
    m_tabs[tabIdx].crossTodayBadgesLayout->setContentsMargins(0, 4, 0, 0);
    m_tabs[tabIdx].crossTodayBadgesLayout->setHorizontalSpacing(6);
    m_tabs[tabIdx].crossTodayBadgesLayout->setVerticalSpacing(6);
    vGreenMain->addWidget(m_tabs[tabIdx].crossTodayBadgesWidget);

    vTab->addWidget(m_tabs[tabIdx].crossTodayFrame);

    // Table View & Models
    m_tabs[tabIdx].tableView = new QTableView(container);
    m_tabs[tabIdx].tableModel = new QStandardItemModel(0, headers.size(), container);
    m_tabs[tabIdx].tableModel->setHorizontalHeaderLabels(headers);

    m_tabs[tabIdx].proxyModel = new QSortFilterProxyModel(container);
    m_tabs[tabIdx].proxyModel->setSourceModel(m_tabs[tabIdx].tableModel);
    m_tabs[tabIdx].proxyModel->setFilterKeyColumn(-1);
    m_tabs[tabIdx].proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_tabs[tabIdx].tableView->setModel(m_tabs[tabIdx].proxyModel);
    vTab->addWidget(m_tabs[tabIdx].tableView);

    // Connections
    connect(m_tabs[tabIdx].fetchBtn, &QPushButton::clicked, this, [this, tabIdx]() {
        fetchSheetData(m_tabs[tabIdx].sheetName);
    });
    connect(m_tabs[tabIdx].filterEdit, &QLineEdit::textChanged, this, [this, tabIdx](const QString& text) {
        if (text.isEmpty()) {
            m_tabs[tabIdx].proxyModel->setFilterRegularExpression(QRegularExpression());
        } else {
            m_tabs[tabIdx].proxyModel->setFilterRegularExpression(
                QRegularExpression(QRegularExpression::escape(text), QRegularExpression::CaseInsensitiveOption));
        }
    });
    connect(m_tabs[tabIdx].proxyModel, &QSortFilterProxyModel::layoutChanged, this, [this, tabIdx]() {
        updateTabActionButtons(tabIdx);
    });
    connect(m_tabs[tabIdx].proxyModel, &QSortFilterProxyModel::modelReset, this, [this, tabIdx]() {
        updateTabActionButtons(tabIdx);
    });
    connect(m_tabs[tabIdx].toggleRedListBtn, &QPushButton::clicked, this, &MainWindow::onToggleRedList);
    connect(m_tabs[tabIdx].toggleCrossTodayListBtn, &QPushButton::clicked, this, &MainWindow::onToggleCrossTodayList);

    return container;
}

void MainWindow::browseImage() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select Image", "", "Images (*.png *.jpg *.jpeg)");
    if (!fileName.isEmpty()) {
        m_imgPathEdit->setText(fileName);
    }
}

void MainWindow::startProcessing() {
    QString imgPath = m_imgPathEdit->text().trimmed();
    QString idsRaw = m_idsEdit->text().trimmed();

    if (imgPath.isEmpty() || idsRaw.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select an image and enter at least one ID.");
        return;
    }

    // Handle file URL prefix if pasted
    if (imgPath.startsWith("file:///")) {
        imgPath = QUrl(imgPath).toLocalFile();
    }

    QStringList ids = idsRaw.split(QRegularExpression("[,\\n]+"), Qt::SkipEmptyParts);
    for (QString& id : ids) id = id.trimmed();

    m_processBtn->setEnabled(false);
    m_processBtn->setText("Processing...");
    log(QString("Starting process for %1 IDs...").arg(ids.size()));

    m_geminiClient->processImage(imgPath, ids);
}

void MainWindow::onGeminiFinished(const QList<DataRow>& rows) {
    if (rows.isEmpty()) {
        log("Error: Gemini could not extract any data.");
        m_processBtn->setEnabled(true);
        m_processBtn->setText("Extract & Sync");
        return;
    }

    QList<DataRow> newRows;
    int skipCount = 0;
    for (const auto& row : rows) {
        if (m_dbManager->existsLocally(row.bill, row.container_no, row.invoice_no)) {
            log(QString("Skipping: BILL '%1' already exists.").arg(row.bill));
            skipCount++;
        } else {
            newRows.append(row);
        }
    }

    if (!newRows.isEmpty()) {
        m_dbManager->saveBatch(newRows);
        m_sheetsClient->appendRows(newRows);
    } else {
        log(QString("Process complete. All %1 rows already exist.").arg(skipCount));
        m_processBtn->setEnabled(true);
        m_processBtn->setText("Extract & Sync");
    }
}

void MainWindow::onGeminiError(const QString& message) {
    log("GEMINI ERROR: " + message);
    m_processBtn->setEnabled(true);
    m_processBtn->setText("Extract & Sync");
}

void MainWindow::onGoogleFinished() {
    log("Process complete successfully.");
    m_processBtn->setEnabled(true);
    m_processBtn->setText("Extract & Sync");
    int currentIdx = m_tabWidget ? m_tabWidget->currentIndex() : 0;
    if (currentIdx >= 0 && currentIdx < m_tabs.size()) {
        fetchSheetData(m_tabs[currentIdx].sheetName);
    } else {
        fetchSheetData("2026");
    }
}

void MainWindow::onGoogleError(const QString& message) {
    log("GOOGLE ERROR: " + message);
    m_processBtn->setEnabled(true);
    m_processBtn->setText("Extract & Sync");
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].fetchBtn) {
            m_tabs[i].fetchBtn->setEnabled(true);
            m_tabs[i].fetchBtn->setText(QString("Update %1 Data").arg(m_tabs[i].tabTitle));
        }
    }
}

void MainWindow::log(const QString& message) {
    m_logEdit->appendPlainText("> " + message);
}

void MainWindow::fetchSheetData(const QString& sheetName) {
    int tabIdx = -1;
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].sheetName == sheetName || m_tabs[i].tabTitle == sheetName) {
            tabIdx = i;
            break;
        }
    }
    if (tabIdx == -1) tabIdx = 0;

    m_tabs[tabIdx].fetchBtn->setEnabled(false);
    m_tabs[tabIdx].fetchBtn->setText("Fetching...");
    log(QString("Requesting data from '%1' sheet...").arg(m_tabs[tabIdx].sheetName));
    m_sheetsClient->fetchSheetData(m_tabs[tabIdx].sheetName, m_tabs[tabIdx].fetchRange);
}

bool MainWindow::isRedColor(const QColor& color) {
    if (!color.isValid()) return false;
    int r = color.red();
    int g = color.green();
    int b = color.blue();
    return (r > g + 15 && r > b + 15);
}

bool MainWindow::isGreenColor(const QColor& color) {
    if (!color.isValid()) return false;
    int r = color.red();
    int g = color.green();
    int b = color.blue();
    return (g > r + 8 && g > b + 8);
}

void MainWindow::formatDateIfSerial(QString& val) {
    bool ok;
    double serial = val.toDouble(&ok);
    if (ok && serial > 30000 && serial < 60000) { // Reasonable range for 20th/21st century
        QDate baseDate(1899, 12, 30);
        val = QLocale::c().toString(baseDate.addDays(static_cast<qint64>(serial)), "dd-MMM-yyyy");
    }
}

void MainWindow::onDataFetched(const QString& sheetName, const QList<QList<CellData>>& rows) {
    m_dbManager->saveSheetCache(sheetName, rows);

    int tabIdx = -1;
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].sheetName == sheetName || m_tabs[i].tabTitle == sheetName) {
            tabIdx = i;
            break;
        }
    }

    if (tabIdx != -1) {
        populateSheetData(tabIdx, rows);
        m_tabs[tabIdx].fetchBtn->setEnabled(true);
        m_tabs[tabIdx].fetchBtn->setText(tabIdx == 0 ? "Update 2026 Data" : "Update LOCAL SUPPLY Data");
    }
}

void MainWindow::populateSheetData(int tabIdx, const QList<QList<CellData>>& rows) {
    if (tabIdx < 0 || tabIdx >= m_tabs.size()) return;
    SheetTabInfo& tab = m_tabs[tabIdx];
    tab.tableModel->removeRows(0, tab.tableModel->rowCount());

    int totalRows = rows.size();
    QStringList redInvoices;
    QStringList crossTodayInvoices;

    if (tabIdx == 0) {
        // Tab 0: "2026"
        // Headers: Client (Col B), IFL-CLIENT (Col K), Invoice No (Col C), Ref No (Col D), Invoice Date (Col F), Container (2026) (Col I), Bill (2026) (Col J), Cross Border (Col G)
        for (int i = totalRows - 1; i >= 0; --i) {
            const QList<CellData>& row = rows[i];
            if (row.isEmpty()) continue;
            int originalRowIndex = i + 1;
            
            CellData c_client = row.size() > 1 ? row[1] : CellData{"", Qt::white};
            CellData c_invNo = row.size() > 2 ? row[2] : CellData{"", Qt::white};
            CellData c_refNo = row.size() > 3 ? row[3] : CellData{"", Qt::white};
            CellData c_invDate = row.size() > 5 ? row[5] : CellData{"", Qt::white};
            formatDateIfSerial(c_invDate.value);

            CellData c_crossBorder = row.size() > 6 ? row[6] : CellData{"", Qt::white};
            formatDateIfSerial(c_crossBorder.value);

            CellData c_container2026 = row.size() > 8 ? row[8] : CellData{"", Qt::white};
            CellData c_bill2026 = row.size() > 9 ? row[9] : CellData{"", Qt::white};
            CellData c_iflClient2026 = row.size() > 10 ? row[10] : CellData{"", Qt::white};

            // Skip header row if it is one
            if (c_client.value.toLower() == "client" || c_invNo.value.toLower() == "invoice_no" || c_invNo.value.toLower() == "invoice") continue;
            
            // Skip if all relevant columns are empty
            if (c_client.value.isEmpty() && c_invNo.value.isEmpty() && c_refNo.value.isEmpty()) continue;

            QList<QStandardItem*> items;
            auto addItem = [&](const CellData& cell, bool isFirst = false, const QString& invoiceId = "") {
                QStandardItem* item = new QStandardItem(cell.value);
                if (cell.bgColor != Qt::white) {
                    item->setBackground(cell.bgColor);
                }
                if (isFirst) {
                    item->setData(originalRowIndex, Qt::UserRole + 1);
                    item->setData(invoiceId, Qt::UserRole + 2);
                }
                items.append(item);
            };

            addItem(c_client, true, c_invNo.value.trimmed());
            addItem(c_iflClient2026);
            addItem(c_invNo);
            addItem(c_refNo);
            addItem(c_invDate);
            addItem(c_container2026);
            addItem(c_bill2026);
            addItem(c_crossBorder);
            
            tab.tableModel->appendRow(items);
        }

        // Identify red invoices (where REF NO Col D is red) and green invoices (where CROSS BORDER Col G is green)
        for (int i = 0; i < rows.size(); ++i) {
            const QList<CellData>& row = rows[i];
            if (row.isEmpty()) continue;
            
            CellData c_client = row.size() > 1 ? row[1] : CellData{"", Qt::white};
            CellData c_invNo = row.size() > 2 ? row[2] : CellData{"", Qt::white};
            CellData c_ref = row.size() > 3 ? row[3] : CellData{"", Qt::white};
            CellData c_crossBorder = row.size() > 6 ? row[6] : CellData{"", Qt::white};

            // Skip header row if it is one
            if (c_client.value.toLower() == "client" || c_invNo.value.toLower() == "invoice_no" || c_invNo.value.toLower() == "invoice" || c_invNo.value.toLower() == "invoice no") continue;
            if (c_client.value.isEmpty() && c_invNo.value.isEmpty() && c_ref.value.isEmpty()) continue;

            if (isRedColor(c_ref.bgColor)) {
                QString invoiceVal = c_invNo.value.trimmed();
                if (!invoiceVal.isEmpty()) {
                    redInvoices.append(invoiceVal);
                }
            }

            if (isGreenColor(c_crossBorder.bgColor)) {
                QString invoiceVal = c_invNo.value.trimmed();
                if (!invoiceVal.isEmpty()) {
                    crossTodayInvoices.append(invoiceVal);
                }
            }
        }
    } else if (tabIdx == 1) {
        // Tab 1: "LOCAL SUPPLY"
        // Columns mapped:
        // Col A (index 0): N.O
        // Col B (index 1): Client Name
        // Col F (index 5): Invoice No
        // Col G (index 6): Ref No
        // Col I (index 8): Invoice Date (Auto-format serial dates >30000 and <60000 to dd-MMM-yyyy)
        // Col J (index 9): Cross (Auto-format serial dates >30000 and <60000 to dd-MMM-yyyy)
        for (int i = totalRows - 1; i >= 0; --i) {
            const QList<CellData>& row = rows[i];
            if (row.isEmpty()) continue;
            int originalRowIndex = i + 1;

            CellData c_no = row.size() > 0 ? row[0] : CellData{"", Qt::white}; // Col A (0)
            CellData c_clientName = row.size() > 1 ? row[1] : CellData{"", Qt::white}; // Col B (1)
            CellData c_invoiceNo = row.size() > 5 ? row[5] : CellData{"", Qt::white}; // Col F (5)
            CellData c_refNo = row.size() > 6 ? row[6] : CellData{"", Qt::white}; // Col G (6)
            CellData c_invoiceDate = row.size() > 8 ? row[8] : CellData{"", Qt::white}; // Col I (8)
            formatDateIfSerial(c_invoiceDate.value);

            CellData c_cross = row.size() > 9 ? row[9] : CellData{"", Qt::white}; // Col J (9)
            formatDateIfSerial(c_cross.value);

            // Skip header row
            if (c_no.value.toLower() == "n.o" || c_no.value.toLower() == "no" || c_invoiceNo.value.toLower() == "invoice no" || c_invoiceNo.value.toLower() == "invoice_no" || c_clientName.value.toLower() == "client name") continue;

            // Skip if empty
            if (c_no.value.isEmpty() && c_clientName.value.isEmpty() && c_invoiceNo.value.isEmpty() && c_refNo.value.isEmpty()) continue;

            QList<QStandardItem*> items;
            auto addItem = [&](const CellData& cell, bool isFirst = false, const QString& invoiceId = "") {
                QStandardItem* item = new QStandardItem(cell.value);
                if (cell.bgColor != Qt::white) {
                    item->setBackground(cell.bgColor);
                }
                if (isFirst) {
                    item->setData(originalRowIndex, Qt::UserRole + 1);
                    item->setData(invoiceId, Qt::UserRole + 2);
                }
                items.append(item);
            };

            addItem(c_no, true, c_invoiceNo.value.trimmed());
            addItem(c_clientName);
            addItem(c_invoiceNo);
            addItem(c_refNo);
            addItem(c_invoiceDate);
            addItem(c_cross);

            tab.tableModel->appendRow(items);
        }

        // Identify red alerts (Ref No Col G / index 6 is red -> badge shows Invoice No Col F / index 5)
        // and green alerts (Cross Col J / index 9 is green -> badge shows Invoice No Col F / index 5)
        for (int i = 0; i < rows.size(); ++i) {
            const QList<CellData>& row = rows[i];
            if (row.isEmpty()) continue;

            CellData c_no = row.size() > 0 ? row[0] : CellData{"", Qt::white};
            CellData c_clientName = row.size() > 1 ? row[1] : CellData{"", Qt::white};
            CellData c_invoiceNo = row.size() > 5 ? row[5] : CellData{"", Qt::white};
            CellData c_refNo = row.size() > 6 ? row[6] : CellData{"", Qt::white};
            CellData c_cross = row.size() > 9 ? row[9] : CellData{"", Qt::white};

            // Skip header row
            if (c_no.value.toLower() == "n.o" || c_no.value.toLower() == "no" || c_invoiceNo.value.toLower() == "invoice no" || c_invoiceNo.value.toLower() == "invoice_no" || c_clientName.value.toLower() == "client name") continue;

            if (c_invoiceNo.value.isEmpty() && c_refNo.value.isEmpty()) continue;

            if (isRedColor(c_refNo.bgColor)) {
                QString invoiceVal = c_invoiceNo.value.trimmed();
                if (!invoiceVal.isEmpty()) {
                    redInvoices.append(invoiceVal);
                }
            }

            if (isGreenColor(c_cross.bgColor)) {
                QString invoiceVal = c_invoiceNo.value.trimmed();
                if (!invoiceVal.isEmpty()) {
                    crossTodayInvoices.append(invoiceVal);
                }
            }
        }
    }

    log(QString("Fetched and displayed %1 rows for '%2'.").arg(tab.tableModel->rowCount()).arg(tab.tabTitle));

    // Red Invoices UI
    QStringList uniqueRedInvoices;
    for (const QString& inv : redInvoices) {
        if (!uniqueRedInvoices.contains(inv)) {
            uniqueRedInvoices.append(inv);
        }
    }

    if (!uniqueRedInvoices.isEmpty()) {
        tab.redInvoiceCountLabel->setText(QString("We found %1 invoice(s) highlighted in red (yet to cross the border). Focus on these items first.").arg(uniqueRedInvoices.size()));
        tab.redInvoiceFrame->setVisible(true);

        // Clear existing badges
        QLayoutItem *child;
        while ((child = tab.redBadgesLayout->takeAt(0)) != nullptr) {
            if (child->widget()) {
                child->widget()->deleteLater();
            }
            delete child;
        }

        int colCount = 8;
        for (int idx = 0; idx < uniqueRedInvoices.size(); ++idx) {
            const QString& invNo = uniqueRedInvoices[idx];
            QPushButton *badge = new QPushButton(invNo, tab.redInvoiceFrame);
            badge->setCursor(Qt::PointingHandCursor);
            badge->setProperty("tabIdx", tabIdx);
            badge->setProperty("invoiceNo", invNo);
            badge->setStyleSheet(
                "QPushButton { "
                "  border: 1px solid #dc3545; "
                "  border-radius: 10px; "
                "  padding: 2px 8px; "
                "  color: white; "
                "  background-color: #dc3545; "
                "  font-weight: bold; "
                "  font-size: 8pt; "
                "} "
                "QPushButton:hover { "
                "  background-color: #c82333; "
                "  border-color: #bd2130; "
                "}"
            );
            connect(badge, &QPushButton::clicked, this, &MainWindow::onRedBadgeClicked);
            
            int r = idx / colCount;
            int c = idx % colCount;
            tab.redBadgesLayout->addWidget(badge, r, c, Qt::AlignLeft | Qt::AlignVCenter);
        }
        tab.redBadgesLayout->setColumnStretch(colCount, 1);
    } else {
        tab.redInvoiceFrame->setVisible(false);
    }

    // Green Invoices UI
    QStringList uniqueCrossTodayInvoices;
    for (const QString& inv : crossTodayInvoices) {
        if (!uniqueCrossTodayInvoices.contains(inv)) {
            uniqueCrossTodayInvoices.append(inv);
        }
    }

    if (!uniqueCrossTodayInvoices.isEmpty()) {
        tab.crossTodayCountLabel->setText(QString("We found %1 invoice(s) crossing today (green).").arg(uniqueCrossTodayInvoices.size()));
        tab.crossTodayFrame->setVisible(true);

        // Clear existing badges
        QLayoutItem *child;
        while ((child = tab.crossTodayBadgesLayout->takeAt(0)) != nullptr) {
            if (child->widget()) {
                child->widget()->deleteLater();
            }
            delete child;
        }

        int colCount = 8;
        for (int idx = 0; idx < uniqueCrossTodayInvoices.size(); ++idx) {
            const QString& invNo = uniqueCrossTodayInvoices[idx];
            QPushButton *badge = new QPushButton(invNo, tab.crossTodayFrame);
            badge->setCursor(Qt::PointingHandCursor);
            badge->setProperty("tabIdx", tabIdx);
            badge->setProperty("invoiceNo", invNo);
            badge->setStyleSheet(
                "QPushButton { "
                "  border: 1px solid #28a745; "
                "  border-radius: 10px; "
                "  padding: 2px 8px; "
                "  color: white; "
                "  background-color: #28a745; "
                "  font-weight: bold; "
                "  font-size: 8pt; "
                "} "
                "QPushButton:hover { "
                "  background-color: #218838; "
                "  border-color: #1e7e34; "
                "}"
            );
            connect(badge, &QPushButton::clicked, this, &MainWindow::onCrossTodayBadgeClicked);
            
            int r = idx / colCount;
            int c = idx % colCount;
            tab.crossTodayBadgesLayout->addWidget(badge, r, c, Qt::AlignLeft | Qt::AlignVCenter);
        }
        tab.crossTodayBadgesLayout->setColumnStretch(colCount, 1);
    } else {
        tab.crossTodayFrame->setVisible(false);
    }

    updateTabActionButtons(tabIdx);
}

void MainWindow::toggleConfig() {
    m_configGroup->setVisible(!m_configGroup->isVisible());
    m_toggleConfigBtn->setText(m_configGroup->isVisible() ? "✖ Close" : "⚙ Settings");
}

void MainWindow::toggleLog() {
    m_logGroup->setVisible(!m_logGroup->isVisible());
    m_toggleLogBtn->setText(m_logGroup->isVisible() ? "Hide Status Log" : "Show Status Log");
}

void MainWindow::toggleAiInput() {
    m_aiInputGroup->setVisible(!m_aiInputGroup->isVisible());
    m_toggleAiInputBtn->setText(m_aiInputGroup->isVisible() ? "✖ Close AI Entry" : "⚡ AI Data Entry");
}

void MainWindow::toggleEditMode() {
    m_isEditMode = !m_isEditMode;
    if (m_isEditMode) {
        m_toggleEditModeBtn->setText("✏️ Edit Mode: ON");
        m_toggleEditModeBtn->setStyleSheet("background-color: #FF9800; color: white; font-weight: bold; border-radius: 4px; padding: 4px;");
        log("Edit mode enabled. Revision buttons are now visible.");
    } else {
        m_toggleEditModeBtn->setText("🔒 Edit Mode: OFF");
        m_toggleEditModeBtn->setStyleSheet("background-color: #f5f5f5; border: 1px solid #ccc; border-radius: 4px; padding: 4px; font-weight: bold;");
        log("Edit mode disabled. Revision buttons are hidden.");
    }
    updateActionButtons();
}

void MainWindow::saveConfig() {
    m_geminiApiKey = m_geminiKeyEdit->text().trimmed();
    m_aiModelName = m_aiModelEdit->text().trimmed();
    m_spreadsheetId = m_spreadsheetIdEdit->text().trimmed();
    m_googleSecretData = m_googleSecretEdit->toPlainText().trimmed();

    if (m_geminiApiKey.isEmpty() || m_aiModelName.isEmpty() || m_spreadsheetId.isEmpty() || m_googleSecretData.isEmpty()) {
        QMessageBox::warning(this, "Error", "All fields are required.");
        return;
    }

    // Save to INI
    QString baseDir = QCoreApplication::applicationDirPath();
    #ifdef QT_DEBUG
    baseDir = QDir::currentPath();
    #endif
    
    QSettings settings(baseDir + "/config.ini", QSettings::IniFormat);
    settings.beginGroup("Credentials");
    settings.setValue("GeminiApiKey", m_geminiApiKey);
    settings.setValue("AiModelName", m_aiModelName);
    settings.setValue("SpreadsheetId", m_spreadsheetId);
    
    // If it's JSON, save it as a special key, otherwise save as file path
    if (m_googleSecretData.startsWith("{")) {
        settings.setValue("GoogleSecretJSON", m_googleSecretData);
        settings.remove("GoogleSecretFile");
    } else {
        settings.setValue("GoogleSecretFile", m_googleSecretData);
        settings.remove("GoogleSecretJSON");
    }
    settings.endGroup();
    settings.sync();

    // Update clients
    m_geminiClient->setApiKey(m_geminiApiKey);
    m_geminiClient->setModelName(m_aiModelName);
    m_sheetsClient->setSpreadsheetId(m_spreadsheetId);
    m_sheetsClient->setServiceAccountData(m_googleSecretData);

    log("Configuration updated and saved.");
    toggleConfig(); // Hide after save
}

void MainWindow::loadConfig() {
    QString baseDir = QCoreApplication::applicationDirPath();
    #ifdef QT_DEBUG
    baseDir = QDir::currentPath();
    #endif

    QString configPath = baseDir + "/config.ini";
    QSettings settings(configPath, QSettings::IniFormat);

    settings.beginGroup("Credentials");
    m_geminiApiKey = settings.value("GeminiApiKey", "AIzaSyB4HLZDCPhiQj0g3oAKOnIa13Bren9sIIk").toString();
    m_aiModelName = settings.value("AiModelName", "gemini-1.5-flash").toString();
    m_spreadsheetId = settings.value("SpreadsheetId", "1piQv1mpEWWBw3hUITAD2Q5ZHdvnqP38n0OLUbK5Y1Hk").toString();
    
    if (settings.contains("GoogleSecretJSON")) {
        m_googleSecretData = settings.value("GoogleSecretJSON").toString();
    } else {
        QString secretFile = settings.value("GoogleSecretFile", "secret.json").toString();
        QString fullPath;
        if (QFileInfo(secretFile).isRelative()) {
            fullPath = baseDir + "/" + secretFile;
        } else {
            fullPath = secretFile;
        }
        m_googleSecretData = fullPath;
    }
    settings.endGroup();

    // Populate UI
    m_geminiKeyEdit->setText(m_geminiApiKey);
    m_aiModelEdit->setText(m_aiModelName);
    m_spreadsheetIdEdit->setText(m_spreadsheetId);
    
    // If m_googleSecretData is a file path, try to read it to show JSON in UI
    if (!m_googleSecretData.trimmed().startsWith("{")) {
        QFile file(m_googleSecretData);
        if (file.open(QIODevice::ReadOnly)) {
            m_googleSecretEdit->setPlainText(file.readAll());
        } else {
            m_googleSecretEdit->setPlainText(m_googleSecretData); // Just show path if can't read
        }
    } else {
        m_googleSecretEdit->setPlainText(m_googleSecretData);
    }
}

void MainWindow::updateActionButtons() {
    for (int tabIdx = 0; tabIdx < m_tabs.size(); ++tabIdx) {
        updateTabActionButtons(tabIdx);
    }
}

void MainWindow::updateTabActionButtons(int tabIdx) {
    if (tabIdx < 0 || tabIdx >= m_tabs.size()) return;
    SheetTabInfo& tab = m_tabs[tabIdx];
    int crossCol = tab.crossColumnIndex;

    tab.tableView->showColumn(crossCol);

    int rowCount = tab.proxyModel->rowCount();
    for (int row = 0; row < rowCount; ++row) {
        QModelIndex proxyIndex = tab.proxyModel->index(row, crossCol);

        QModelIndex sourceIndex = tab.proxyModel->mapToSource(proxyIndex);
        if (!sourceIndex.isValid()) {
            if (tab.tableView->indexWidget(proxyIndex)) {
                tab.tableView->setIndexWidget(proxyIndex, nullptr);
            }
            continue;
        }
        
        QString val = tab.tableModel->data(tab.tableModel->index(sourceIndex.row(), crossCol)).toString().trimmed();
        QStandardItem* firstItem = tab.tableModel->item(sourceIndex.row(), 0);
        if (!firstItem) {
            if (tab.tableView->indexWidget(proxyIndex)) {
                tab.tableView->setIndexWidget(proxyIndex, nullptr);
            }
            continue;
        }

        int originalRowIndex = firstItem->data(Qt::UserRole + 1).toInt();
        QString invoiceId = firstItem->data(Qt::UserRole + 2).toString();
        if (invoiceId.isEmpty()) {
            invoiceId = tab.tableModel->data(tab.tableModel->index(sourceIndex.row(), 2)).toString();
        }
        if (originalRowIndex <= 0) {
            if (tab.tableView->indexWidget(proxyIndex)) {
                tab.tableView->setIndexWidget(proxyIndex, nullptr);
            }
            continue;
        }

        if (m_isEditMode) {
            // Show two buttons: Cross/Revise + Clear (Clear only on tab 0)
            QWidget *container = new QWidget(tab.tableView);
            QHBoxLayout *cellLayout = new QHBoxLayout(container);
            cellLayout->setContentsMargins(2, 2, 2, 2);
            cellLayout->setSpacing(4);

            QPushButton *actionBtn = nullptr;
            if (val.isEmpty()) {
                actionBtn = new QPushButton("Cross", container);
                actionBtn->setStyleSheet("background-color: #2196F3; color: white; font-weight: bold; border: none; border-radius: 3px; padding: 2px;");
                actionBtn->setProperty("currentVal", "");
            } else {
                actionBtn = new QPushButton("Revise (" + val + ")", container);
                actionBtn->setStyleSheet("background-color: #FF9800; color: white; font-weight: bold; border: none; border-radius: 3px; padding: 2px;");
                actionBtn->setProperty("currentVal", val);
            }
            actionBtn->setCursor(Qt::PointingHandCursor);
            actionBtn->setProperty("tabIdx", tabIdx);
            actionBtn->setProperty("invoiceId", invoiceId);
            actionBtn->setProperty("originalRowIndex", originalRowIndex);
            connect(actionBtn, &QPushButton::clicked, this, &MainWindow::onCrossButtonClicked);

            cellLayout->addWidget(actionBtn);

            if (tabIdx == 0) {
                QPushButton *clearBtn = new QPushButton("🗑️ Clear", container);
                clearBtn->setStyleSheet("background-color: #dc3545; color: white; font-weight: bold; border: none; border-radius: 3px; padding: 2px;");
                clearBtn->setCursor(Qt::PointingHandCursor);
                clearBtn->setProperty("invoiceId", invoiceId);
                connect(clearBtn, &QPushButton::clicked, this, &MainWindow::onClearButtonClicked);
                cellLayout->addWidget(clearBtn);
            }

            container->setLayout(cellLayout);
            tab.tableView->setIndexWidget(proxyIndex, container);
        } else {
            // Normal Mode: Show Cross button if empty, show plain text date if filled
            if (val.isEmpty()) {
                QPushButton *btn = new QPushButton("Cross", tab.tableView);
                btn->setStyleSheet("background-color: #2196F3; color: white; font-weight: bold; border: none; border-radius: 3px; padding: 2px;");
                btn->setCursor(Qt::PointingHandCursor);
                btn->setProperty("tabIdx", tabIdx);
                btn->setProperty("invoiceId", invoiceId);
                btn->setProperty("originalRowIndex", originalRowIndex);
                btn->setProperty("currentVal", "");
                
                connect(btn, &QPushButton::clicked, this, &MainWindow::onCrossButtonClicked);
                tab.tableView->setIndexWidget(proxyIndex, btn);
            } else {
                if (tab.tableView->indexWidget(proxyIndex)) {
                    tab.tableView->setIndexWidget(proxyIndex, nullptr);
                }
            }
        }
    }
}

void MainWindow::onCrossButtonClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    int tabIdx = btn->property("tabIdx").toInt();
    QString invoiceId = btn->property("invoiceId").toString();
    int originalRowIndex = btn->property("originalRowIndex").toInt();
    QString currentVal = btn->property("currentVal").toString();

    if (tabIdx < 0 || tabIdx >= m_tabs.size()) return;
    const SheetTabInfo& tab = m_tabs[tabIdx];

    // Create a modern modal date dialog
    QDialog dialog(this);
    dialog.setWindowTitle(currentVal.isEmpty() ? "Commit Cross Border Date" : "Revise Cross Border Date");
    dialog.setModal(true);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QLabel *label = new QLabel(QString(currentVal.isEmpty() 
        ? "Select Border Crossing Date for Invoice \"%1\":"
        : "Revise Border Crossing Date for Invoice \"%1\":").arg(invoiceId), &dialog);
        
    QDate initialDate = QDate::currentDate();
    if (!currentVal.isEmpty()) {
        QDate parsed = QLocale::c().toDate(currentVal, "dd-MMM-yyyy");
        if (!parsed.isValid()) {
            parsed = QDate::fromString(currentVal, "dd/MM/yyyy");
        }
        if (!parsed.isValid()) {
            parsed = QDate::fromString(currentVal, "dd-MM-yyyy");
        }
        if (parsed.isValid()) {
            initialDate = parsed;
        }
    }
    
    QDateEdit *dateEdit = new QDateEdit(initialDate, &dialog);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat("dd-MMM-yyyy");
    
    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *okBtn = new QPushButton("Confirm", &dialog);
    okBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
    QPushButton *cancelBtn = new QPushButton("Cancel", &dialog);
    
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);

    layout->addWidget(label);
    layout->addWidget(dateEdit);
    layout->addLayout(btnLayout);

    connect(okBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QDate selectedDate = dateEdit->date();
        QString dateStr = QLocale::c().toString(selectedDate, "dd-MMM-yyyy");
        
        // Ask for confirmation
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, 
            "Confirm", 
            QString("Are you sure you want to mark Invoice \"%1\" as crossed border on %2?").arg(invoiceId).arg(dateStr),
            QMessageBox::Yes | QMessageBox::No
        );
        
        if (reply == QMessageBox::Yes) {
            QString updateRange;
            if (tab.sheetName.contains(" ")) {
                updateRange = QString("'%1'!%2%3").arg(tab.sheetName).arg(tab.crossColumnLetter).arg(originalRowIndex);
            } else {
                updateRange = QString("%1!%2%3").arg(tab.sheetName).arg(tab.crossColumnLetter).arg(originalRowIndex);
            }
            log(QString("Committing border crossing date %1 for Invoice %2 in %3 (Row %4)...")
                .arg(dateStr).arg(invoiceId).arg(tab.sheetName).arg(originalRowIndex));
            m_sheetsClient->updateCell(updateRange, dateStr);
        }
    }
}

void MainWindow::onToggleRedList() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    int tabIdx = btn->property("tabIdx").toInt();
    if (tabIdx < 0 || tabIdx >= m_tabs.size()) return;

    bool visible = !m_tabs[tabIdx].redBadgesWidget->isVisible();
    m_tabs[tabIdx].redBadgesWidget->setVisible(visible);
    m_tabs[tabIdx].toggleRedListBtn->setText(visible ? "Hide Invoices" : "Show Invoices");
}

void MainWindow::onRedBadgeClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    int tabIdx = btn->property("tabIdx").toInt();
    if (tabIdx < 0 || tabIdx >= m_tabs.size()) return;

    QString invNo = btn->property("invoiceNo").toString();
    if (m_tabs[tabIdx].filterEdit->text() == invNo) {
        m_tabs[tabIdx].filterEdit->clear();
    } else {
        m_tabs[tabIdx].filterEdit->setText(invNo);
    }
}

void MainWindow::onToggleCrossTodayList() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    int tabIdx = btn->property("tabIdx").toInt();
    if (tabIdx < 0 || tabIdx >= m_tabs.size()) return;

    bool visible = !m_tabs[tabIdx].crossTodayBadgesWidget->isVisible();
    m_tabs[tabIdx].crossTodayBadgesWidget->setVisible(visible);
    m_tabs[tabIdx].toggleCrossTodayListBtn->setText(visible ? "Hide Invoices" : "Show Invoices");
}

void MainWindow::onCrossTodayBadgeClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    int tabIdx = btn->property("tabIdx").toInt();
    if (tabIdx < 0 || tabIdx >= m_tabs.size()) return;

    QString invNo = btn->property("invoiceNo").toString();
    if (m_tabs[tabIdx].filterEdit->text() == invNo) {
        m_tabs[tabIdx].filterEdit->clear();
    } else {
        m_tabs[tabIdx].filterEdit->setText(invNo);
    }
}

void MainWindow::onClearButtonClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    QString invoiceId = btn->property("invoiceId").toString();
    if (invoiceId.isEmpty()) return;

    QMessageBox::StandardButton reply = QMessageBox::question(
        this, 
        "Confirm Clear", 
        QString("Are you sure you want to clear all container data for Invoice \"%1\"? "
                "This will delete matching rows from Google Sheets (CONTAINER sheet) and the local database cache.").arg(invoiceId),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        log(QString("Clearing container entries locally for Invoice %1...").arg(invoiceId));
        m_dbManager->deleteLocally(invoiceId);
        
        log(QString("Requesting Google Sheets to clear rows for Invoice %1...").arg(invoiceId));
        btn->setEnabled(false);
        btn->setText("Clearing...");
        
        m_sheetsClient->deleteContainerRow(invoiceId);
    }
}
