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
#include <QShortcut>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
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

    // Load cached data if any
    QList<QList<CellData>> cachedRows = m_dbManager->loadSheetCache();
    if (!cachedRows.isEmpty()) {
        log(QString("Loaded %1 rows from local cache.").arg(cachedRows.size()));
        onDataFetched(cachedRows);
    }

    m_geminiClient = new GeminiClient(m_geminiApiKey, m_aiModelName, this);
    m_sheetsClient = new GoogleSheetsClient(m_googleSecretData, m_spreadsheetId, this);

    connect(m_geminiClient, &GeminiClient::statusUpdate, this, &MainWindow::log);
    connect(m_geminiClient, &GeminiClient::finished, this, &MainWindow::onGeminiFinished);
    connect(m_geminiClient, &GeminiClient::error, this, &MainWindow::onGeminiError);

    connect(m_sheetsClient, &GoogleSheetsClient::statusUpdate, this, &MainWindow::log);
    connect(m_sheetsClient, &GoogleSheetsClient::finished, this, &MainWindow::onGoogleFinished);
    connect(m_sheetsClient, &GoogleSheetsClient::error, this, &MainWindow::onGoogleError);
    connect(m_sheetsClient, &GoogleSheetsClient::dataFetched, this, &MainWindow::onDataFetched);
}

MainWindow::~MainWindow() {}

void MainWindow::setupUi() {
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *layout = new QVBoxLayout(central);

    QLabel *title = new QLabel("Vision Logistics Data Entry (C++)", this);
    title->setStyleSheet("font-size: 18pt; font-weight: bold;");
    title->setAlignment(Qt::AlignCenter);
    
    m_toggleAiInputBtn = new QPushButton("⚡ AI Data Entry", this);
    m_toggleAiInputBtn->setFixedWidth(120);
    m_toggleConfigBtn = new QPushButton("⚙ Settings", this);
    m_toggleConfigBtn->setFixedWidth(100);
    
    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->addWidget(title);
    headerLayout->addWidget(m_toggleAiInputBtn);
    headerLayout->addWidget(m_toggleConfigBtn);
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

    // Data Viewer
    QGroupBox *dataGroup = new QGroupBox("2026 Sheet Data", this);
    QVBoxLayout *vData = new QVBoxLayout(dataGroup);
    
    QHBoxLayout *hDataControls = new QHBoxLayout();
    m_fetchBtn = new QPushButton("Update 2026 Data", this);
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Search...");
    hDataControls->addWidget(m_fetchBtn);
    hDataControls->addWidget(m_filterEdit);
    vData->addLayout(hDataControls);

    // Ctrl+F shortcut to focus and select search bar content
    QShortcut *searchShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        m_filterEdit->setFocus();
        m_filterEdit->selectAll();
    });

    // Red Invoice Alert Panel (Hidden by default)
    m_redInvoiceFrame = new QFrame(this);
    m_redInvoiceFrame->setObjectName("redInvoiceFrame");
    m_redInvoiceFrame->setStyleSheet(
        "QFrame#redInvoiceFrame { "
        "  border: 1px solid #f5c6cb; "
        "  border-left: 5px solid #dc3545; "
        "  border-radius: 4px; "
        "  background-color: #f8d7da; "
        "}"
    );
    m_redInvoiceFrame->setVisible(false);

    QVBoxLayout *vRedMain = new QVBoxLayout(m_redInvoiceFrame);
    vRedMain->setContentsMargins(10, 8, 10, 8);
    vRedMain->setSpacing(4);

    QHBoxLayout *hRedTop = new QHBoxLayout();
    hRedTop->setContentsMargins(0, 0, 0, 0);

    QLabel *warningIcon = new QLabel("⚠️", m_redInvoiceFrame);
    warningIcon->setStyleSheet("font-size: 12pt;");

    m_redInvoiceCountLabel = new QLabel("We found 0 invoice(s) highlighted in red (yet to cross the border). Focus on these items first.", m_redInvoiceFrame);
    m_redInvoiceCountLabel->setStyleSheet("color: #721c24; font-weight: bold; font-size: 9pt;");

    m_toggleRedListBtn = new QPushButton("Show Invoices", m_redInvoiceFrame);
    m_toggleRedListBtn->setFixedWidth(110);
    m_toggleRedListBtn->setCursor(Qt::PointingHandCursor);
    m_toggleRedListBtn->setStyleSheet(
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
    hRedTop->addWidget(m_redInvoiceCountLabel, 1);
    hRedTop->addWidget(m_toggleRedListBtn);
    vRedMain->addLayout(hRedTop);

    m_redBadgesWidget = new QWidget(m_redInvoiceFrame);
    m_redBadgesWidget->setVisible(false);
    m_redBadgesLayout = new QGridLayout(m_redBadgesWidget);
    m_redBadgesLayout->setContentsMargins(0, 4, 0, 0);
    m_redBadgesLayout->setHorizontalSpacing(6);
    m_redBadgesLayout->setVerticalSpacing(6);
    vRedMain->addWidget(m_redBadgesWidget);

    vData->addWidget(m_redInvoiceFrame);

    // Green Invoice Alert Panel (Hidden by default)
    m_crossTodayFrame = new QFrame(this);
    m_crossTodayFrame->setObjectName("crossTodayFrame");
    m_crossTodayFrame->setStyleSheet(
        "QFrame#crossTodayFrame { "
        "  border: 1px solid #c3e6cb; "
        "  border-left: 5px solid #28a745; "
        "  border-radius: 4px; "
        "  background-color: #d4edda; "
        "}"
    );
    m_crossTodayFrame->setVisible(false);

    QVBoxLayout *vGreenMain = new QVBoxLayout(m_crossTodayFrame);
    vGreenMain->setContentsMargins(10, 8, 10, 8);
    vGreenMain->setSpacing(4);

    QHBoxLayout *hGreenTop = new QHBoxLayout();
    hGreenTop->setContentsMargins(0, 0, 0, 0);

    QLabel *greenWarningIcon = new QLabel("✅", m_crossTodayFrame);
    greenWarningIcon->setStyleSheet("font-size: 12pt;");

    m_crossTodayCountLabel = new QLabel("We found 0 invoice(s) crossing today (green).", m_crossTodayFrame);
    m_crossTodayCountLabel->setStyleSheet("color: #155724; font-weight: bold; font-size: 9pt;");

    m_toggleCrossTodayListBtn = new QPushButton("Show Invoices", m_crossTodayFrame);
    m_toggleCrossTodayListBtn->setFixedWidth(110);
    m_toggleCrossTodayListBtn->setCursor(Qt::PointingHandCursor);
    m_toggleCrossTodayListBtn->setStyleSheet(
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
    hGreenTop->addWidget(m_crossTodayCountLabel, 1);
    hGreenTop->addWidget(m_toggleCrossTodayListBtn);
    vGreenMain->addLayout(hGreenTop);

    m_crossTodayBadgesWidget = new QWidget(m_crossTodayFrame);
    m_crossTodayBadgesWidget->setVisible(false);
    m_crossTodayBadgesLayout = new QGridLayout(m_crossTodayBadgesWidget);
    m_crossTodayBadgesLayout->setContentsMargins(0, 4, 0, 0);
    m_crossTodayBadgesLayout->setHorizontalSpacing(6);
    m_crossTodayBadgesLayout->setVerticalSpacing(6);
    vGreenMain->addWidget(m_crossTodayBadgesWidget);

    vData->addWidget(m_crossTodayFrame);

    m_tableView = new QTableView(this);
    m_tableModel = new QStandardItemModel(0, 8, this);
    m_tableModel->setHorizontalHeaderLabels({"Client", "Invoice No", "Ref No", "Invoice Date", "Container (2026)", "Bill (2026)", "Pallet Gross (2026)", "Cross Border"});
    
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_tableModel);
    m_proxyModel->setFilterKeyColumn(-1);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    
    m_tableView->setModel(m_proxyModel);
    vData->addWidget(m_tableView);

    // Re-draw buttons whenever table is filtered or sorted
    connect(m_proxyModel, &QSortFilterProxyModel::layoutChanged, this, &MainWindow::updateActionButtons);
    connect(m_proxyModel, &QSortFilterProxyModel::modelReset, this, &MainWindow::updateActionButtons);
    
    layout->addWidget(dataGroup, 1); // Added stretch factor 1 so it takes all available vertical space

    connect(m_fetchBtn, &QPushButton::clicked, this, &MainWindow::fetchSheetData);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterChanged);

    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::browseImage);
    connect(m_processBtn, &QPushButton::clicked, this, &MainWindow::startProcessing);

    connect(m_toggleConfigBtn, &QPushButton::clicked, this, &MainWindow::toggleConfig);
    connect(m_saveConfigBtn, &QPushButton::clicked, this, &MainWindow::saveConfig);

    connect(m_toggleLogBtn, &QPushButton::clicked, this, &MainWindow::toggleLog);
    connect(m_toggleAiInputBtn, &QPushButton::clicked, this, &MainWindow::toggleAiInput);
    connect(m_toggleRedListBtn, &QPushButton::clicked, this, &MainWindow::onToggleRedList);
    connect(m_toggleCrossTodayListBtn, &QPushButton::clicked, this, &MainWindow::onToggleCrossTodayList);

    resize(900, 650);
    setWindowTitle("Vision Logistics Data Entry");
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
    fetchSheetData();
}

void MainWindow::onGoogleError(const QString& message) {
    log("GOOGLE ERROR: " + message);
    m_processBtn->setEnabled(true);
    m_processBtn->setText("Extract & Sync");
}

void MainWindow::log(const QString& message) {
    m_logEdit->appendPlainText("> " + message);
}

void MainWindow::fetchSheetData() {
    m_fetchBtn->setEnabled(false);
    m_fetchBtn->setText("Fetching...");
    log("Requesting data from 2026 sheet...");
    m_sheetsClient->fetchSheetData("2026!A:L");
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
    return (g > r + 15 && g > b + 15);
}

void MainWindow::onDataFetched(const QList<QList<CellData>>& rows) {
    m_tableModel->removeRows(0, m_tableModel->rowCount());

    // Cache the data
    m_dbManager->saveSheetCache(rows);

    int totalRows = rows.size();
    for (int i = totalRows - 1; i >= 0; --i) {
        const QList<CellData>& row = rows[i];
        if (row.isEmpty()) continue;
        int originalRowIndex = i + 1;
        
        CellData c_invoice = row.size() > 1 ? row[1] : CellData{"", Qt::white};
        CellData c_container = row.size() > 2 ? row[2] : CellData{"", Qt::white};
        CellData c_type = row.size() > 3 ? row[3] : CellData{"", Qt::white};
        CellData c_truck = row.size() > 5 ? row[5] : CellData{"", Qt::white};
        
        // Handle numeric date conversion
        bool ok;
        double serial = c_truck.value.toDouble(&ok);
        if (ok && serial > 30000 && serial < 60000) { // Reasonable range for 20th/21st century
            QDate baseDate(1899, 12, 30);
            c_truck.value = baseDate.addDays(static_cast<qint64>(serial)).toString("dd/MM/yyyy");
        }

        CellData c_crossBorder = row.size() > 6 ? row[6] : CellData{"", Qt::white};
        double serialG = c_crossBorder.value.toDouble(&ok);
        if (ok && serialG > 30000 && serialG < 60000) {
            QDate baseDate(1899, 12, 30);
            c_crossBorder.value = baseDate.addDays(static_cast<qint64>(serialG)).toString("dd/MM/yyyy");
        }

        CellData c_container2026 = row.size() > 8 ? row[8] : CellData{"", Qt::white};
        CellData c_bill2026 = row.size() > 9 ? row[9] : CellData{"", Qt::white};
        CellData c_pallet2026 = row.size() > 11 ? row[11] : CellData{"", Qt::white};

        // Skip header row if it is one
        if (c_invoice.value.toLower() == "invoice_no" || c_invoice.value.toLower() == "invoice") continue;
        
        // Skip if all relevant columns are empty
        if (c_invoice.value.isEmpty() && c_container.value.isEmpty() && c_type.value.isEmpty()) continue;

        QList<QStandardItem*> items;
        auto addItem = [&](const CellData& cell, bool isInvoice = false) {
            QStandardItem* item = new QStandardItem(cell.value);
            if (cell.bgColor != Qt::white) {
                item->setBackground(cell.bgColor);
            }
            if (isInvoice) {
                item->setData(originalRowIndex, Qt::UserRole + 1);
            }
            items.append(item);
        };

        addItem(c_invoice, true);
        addItem(c_container);
        addItem(c_type);
        addItem(c_truck);
        addItem(c_container2026);
        addItem(c_bill2026);
        addItem(c_pallet2026);
        addItem(c_crossBorder);
        
        m_tableModel->appendRow(items);
    }
    
    m_fetchBtn->setEnabled(true);
    m_fetchBtn->setText("Update 2026 Data");
    log(QString("Fetched and displayed %1 rows.").arg(m_tableModel->rowCount()));

    // Identify red invoices (where REF NO column has red background) and green invoices (where CROSS BORDER column has green background)
    QStringList redInvoices;
    QStringList crossTodayInvoices;
    for (int i = 1; i < rows.size(); ++i) {
        const QList<CellData>& row = rows[i];
        if (row.isEmpty()) continue;
        
        CellData c_client = row.size() > 1 ? row[1] : CellData{"", Qt::white};
        CellData c_invNo = row.size() > 2 ? row[2] : CellData{"", Qt::white};
        CellData c_ref = row.size() > 3 ? row[3] : CellData{"", Qt::white};
        CellData c_crossBorder = row.size() > 6 ? row[6] : CellData{"", Qt::white};
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

    if (!redInvoices.isEmpty()) {
        m_redInvoiceCountLabel->setText(QString("We found %1 invoice(s) highlighted in red (yet to cross the border). Focus on these items first.").arg(redInvoices.size()));
        m_redInvoiceFrame->setVisible(true);

        // Clear existing badges
        QLayoutItem *child;
        while ((child = m_redBadgesLayout->takeAt(0)) != nullptr) {
            if (child->widget()) {
                child->widget()->deleteLater();
            }
            delete child;
        }

        // Deduplicate
        QStringList uniqueRedInvoices;
        for (const QString& inv : redInvoices) {
            if (!uniqueRedInvoices.contains(inv)) {
                uniqueRedInvoices.append(inv);
            }
        }

        // Add buttons
        int colCount = 8;
        for (int idx = 0; idx < uniqueRedInvoices.size(); ++idx) {
            const QString& invNo = uniqueRedInvoices[idx];
            QPushButton *badge = new QPushButton(invNo, m_redInvoiceFrame);
            badge->setCursor(Qt::PointingHandCursor);
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
            m_redBadgesLayout->addWidget(badge, r, c, Qt::AlignLeft | Qt::AlignVCenter);
        }
        m_redBadgesLayout->setColumnStretch(colCount, 1);
    } else {
        m_redInvoiceFrame->setVisible(false);
    }

    if (!crossTodayInvoices.isEmpty()) {
        m_crossTodayCountLabel->setText(QString("We found %1 invoice(s) crossing today (green).").arg(crossTodayInvoices.size()));
        m_crossTodayFrame->setVisible(true);

        // Clear existing badges
        QLayoutItem *child;
        while ((child = m_crossTodayBadgesLayout->takeAt(0)) != nullptr) {
            if (child->widget()) {
                child->widget()->deleteLater();
            }
            delete child;
        }

        // Deduplicate
        QStringList uniqueCrossTodayInvoices;
        for (const QString& inv : crossTodayInvoices) {
            if (!uniqueCrossTodayInvoices.contains(inv)) {
                uniqueCrossTodayInvoices.append(inv);
            }
        }

        // Add buttons
        int colCount = 8;
        for (int idx = 0; idx < uniqueCrossTodayInvoices.size(); ++idx) {
            const QString& invNo = uniqueCrossTodayInvoices[idx];
            QPushButton *badge = new QPushButton(invNo, m_crossTodayFrame);
            badge->setCursor(Qt::PointingHandCursor);
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
            m_crossTodayBadgesLayout->addWidget(badge, r, c, Qt::AlignLeft | Qt::AlignVCenter);
        }
        m_crossTodayBadgesLayout->setColumnStretch(colCount, 1);
    } else {
        m_crossTodayFrame->setVisible(false);
    }

    // Populate buttons for action cell
    updateActionButtons();
}

void MainWindow::onFilterChanged(const QString& text) {
    m_proxyModel->setFilterRegularExpression(text);
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
    for (int row = 0; row < m_proxyModel->rowCount(); ++row) {
        QModelIndex proxyIndex = m_proxyModel->index(row, 7);
        QModelIndex sourceIndex = m_proxyModel->mapToSource(proxyIndex);
        
        QString val = m_tableModel->data(m_tableModel->index(sourceIndex.row(), 7)).toString().trimmed();
        if (val.isEmpty()) {
            QModelIndex clientSourceIndex = m_tableModel->index(sourceIndex.row(), 0);
            QModelIndex invSourceIndex = m_tableModel->index(sourceIndex.row(), 1);
            QString invoiceId = m_tableModel->data(invSourceIndex).toString();
            
            QStandardItem* item = m_tableModel->itemFromIndex(clientSourceIndex);
            if (!item) continue;
            
            int originalRowIndex = item->data(Qt::UserRole + 1).toInt();
            if (originalRowIndex <= 0) continue;
            
            QPushButton *btn = new QPushButton("Cross", m_tableView);
            btn->setStyleSheet("background-color: #2196F3; color: white; font-weight: bold; border: none; border-radius: 3px; padding: 2px;");
            btn->setCursor(Qt::PointingHandCursor);
            btn->setProperty("invoiceId", invoiceId);
            btn->setProperty("originalRowIndex", originalRowIndex);
            
            connect(btn, &QPushButton::clicked, this, &MainWindow::onCrossButtonClicked);
            m_tableView->setIndexWidget(proxyIndex, btn);
        } else {
            m_tableView->setIndexWidget(proxyIndex, nullptr);
        }
    }
}

void MainWindow::onCrossButtonClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    QString invoiceId = btn->property("invoiceId").toString();
    int originalRowIndex = btn->property("originalRowIndex").toInt();

    // Create a modern modal date dialog
    QDialog dialog(this);
    dialog.setWindowTitle("Commit Cross Border Date");
    dialog.setModal(true);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QLabel *label = new QLabel(QString("Select Border Crossing Date for Invoice \"%1\":").arg(invoiceId), &dialog);
    QDateEdit *dateEdit = new QDateEdit(QDate::currentDate(), &dialog);
    dateEdit->setCalendarPopup(true);
    dateEdit->setDisplayFormat("dd/MM/yyyy");
    
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
        QString dateStr = selectedDate.toString("dd/MM/yyyy");
        
        // Ask for confirmation
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, 
            "Confirm", 
            QString("Are you sure you want to mark Invoice \"%1\" as crossed border on %2?").arg(invoiceId).arg(dateStr),
            QMessageBox::Yes | QMessageBox::No
        );
        
        if (reply == QMessageBox::Yes) {
            log(QString("Committing border crossing date %1 for Invoice %2 (Row %3)...").arg(dateStr).arg(invoiceId).arg(originalRowIndex));
            m_sheetsClient->updateCell(QString("2026!G%1").arg(originalRowIndex), dateStr);
        }
    }
}

void MainWindow::onToggleRedList() {
    bool visible = !m_redBadgesWidget->isVisible();
    m_redBadgesWidget->setVisible(visible);
    m_toggleRedListBtn->setText(visible ? "Hide Invoices" : "Show Invoices");
}

void MainWindow::onRedBadgeClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    QString invNo = btn->property("invoiceNo").toString();
    if (m_filterEdit->text() == invNo) {
        m_filterEdit->clear();
    } else {
        m_filterEdit->setText(invNo);
    }
}

void MainWindow::onToggleCrossTodayList() {
    bool visible = !m_crossTodayBadgesWidget->isVisible();
    m_crossTodayBadgesWidget->setVisible(visible);
    m_toggleCrossTodayListBtn->setText(visible ? "Hide Invoices" : "Show Invoices");
}

void MainWindow::onCrossTodayBadgeClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    QString invNo = btn->property("invoiceNo").toString();
    if (m_filterEdit->text() == invNo) {
        m_filterEdit->clear();
    } else {
        m_filterEdit->setText(invNo);
    }
}
