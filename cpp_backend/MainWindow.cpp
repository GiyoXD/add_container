#include "MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QGroupBox>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUi();

    QString baseDir = QCoreApplication::applicationDirPath();
    // In dev, it might be in the source dir
    #ifdef QT_DEBUG
    baseDir = QDir::currentPath();
    #endif

    m_dbManager = new DatabaseManager(baseDir + "/shipping_data.db", this);
    if (!m_dbManager->setupDatabase()) {
        log("CRITICAL: Failed to setup local database.");
    }

    m_geminiClient = new GeminiClient("YOUR_GEMINI_API_KEY", this);
    m_sheetsClient = new GoogleSheetsClient(baseDir + "/secret.json", "1piQv1mpEWWBw3hUITAD2Q5ZHdvnqP38n0OLUbK5Y1Hk", this);

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
    layout->addWidget(title);

    // Step 1: Image Selection
    QGroupBox *step1 = new QGroupBox("Step 1: Image Selection", this);
    QHBoxLayout *h1 = new QHBoxLayout(step1);
    m_imgPathEdit = new QLineEdit(this);
    QPushButton *browseBtn = new QPushButton("Browse", this);
    h1->addWidget(m_imgPathEdit);
    h1->addWidget(browseBtn);
    layout->addWidget(step1);

    // Step 2: Invoice IDs
    QGroupBox *step2 = new QGroupBox("Step 2: Enter Invoice IDs (one per line)", this);
    QVBoxLayout *v2 = new QVBoxLayout(step2);
    m_idsEdit = new QPlainTextEdit(this);
    v2->addWidget(m_idsEdit);
    layout->addWidget(step2);

    // Step 3: Process
    m_processBtn = new QPushButton("Step 3: Process & Sync", this);
    m_processBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold; padding: 10px;");
    layout->addWidget(m_processBtn);

    // Log
    QGroupBox *logGroup = new QGroupBox("Status/Log", this);
    QVBoxLayout *vLog = new QVBoxLayout(logGroup);
    m_logEdit = new QPlainTextEdit(this);
    m_logEdit->setReadOnly(true);
    m_logEdit->setBackgroundRole(QPalette::AlternateBase);
    vLog->addWidget(m_logEdit);
    layout->addWidget(logGroup);

    // Data Viewer
    QGroupBox *dataGroup = new QGroupBox("2026 Sheet Data", this);
    QVBoxLayout *vData = new QVBoxLayout(dataGroup);
    
    QHBoxLayout *hDataControls = new QHBoxLayout();
    m_fetchBtn = new QPushButton("Fetch 2026 Data", this);
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Search...");
    hDataControls->addWidget(m_fetchBtn);
    hDataControls->addWidget(m_filterEdit);
    vData->addLayout(hDataControls);

    m_tableView = new QTableView(this);
    m_tableModel = new QStandardItemModel(0, 6, this);
    m_tableModel->setHorizontalHeaderLabels({"Invoice No", "Container No", "Ref No", "Invoice Date", "Container (2026)", "Bill (2026)"});
    
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_tableModel);
    m_proxyModel->setFilterKeyColumn(-1);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    
    m_tableView->setModel(m_proxyModel);
    vData->addWidget(m_tableView);
    
    layout->addWidget(dataGroup);

    connect(m_fetchBtn, &QPushButton::clicked, this, &MainWindow::fetchSheetData);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterChanged);

    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::browseImage);
    connect(m_processBtn, &QPushButton::clicked, this, &MainWindow::startProcessing);

    resize(800, 900);
}

void MainWindow::browseImage() {
    QString fileName = QFileDialog::getOpenFileName(this, "Select Image", "", "Images (*.png *.jpg *.jpeg)");
    if (!fileName.isEmpty()) {
        m_imgPathEdit->setText(fileName);
    }
}

void MainWindow::startProcessing() {
    QString imgPath = m_imgPathEdit->text().trimmed();
    QString idsRaw = m_idsEdit->toPlainText().trimmed();

    if (imgPath.isEmpty() || idsRaw.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select an image and enter at least one ID.");
        return;
    }

    // Handle file URL prefix if pasted
    if (imgPath.startsWith("file:///")) {
        imgPath = QUrl(imgPath).toLocalFile();
    }

    QStringList ids = idsRaw.split('\n', Qt::SkipEmptyParts);
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
        m_processBtn->setText("Step 3: Process & Sync");
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
        m_processBtn->setText("Step 3: Process & Sync");
    }
}

void MainWindow::onGeminiError(const QString& message) {
    log("GEMINI ERROR: " + message);
    m_processBtn->setEnabled(true);
    m_processBtn->setText("Step 3: Process & Sync");
}

void MainWindow::onGoogleFinished() {
    log("Process complete successfully.");
    m_processBtn->setEnabled(true);
    m_processBtn->setText("Step 3: Process & Sync");
}

void MainWindow::onGoogleError(const QString& message) {
    log("GOOGLE ERROR: " + message);
    m_processBtn->setEnabled(true);
    m_processBtn->setText("Step 3: Process & Sync");
}

void MainWindow::log(const QString& message) {
    m_logEdit->appendPlainText("> " + message);
}

void MainWindow::fetchSheetData() {
    m_fetchBtn->setEnabled(false);
    m_fetchBtn->setText("Fetching...");
    log("Requesting data from 2026 sheet...");
    m_sheetsClient->fetchSheetData("2026!A:J");
}

void MainWindow::onDataFetched(const QList<QList<CellData>>& rows) {
    m_tableModel->removeRows(0, m_tableModel->rowCount());

    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        const QList<CellData>& row = *it;
        if (row.isEmpty()) continue;
        
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

        CellData c_container2026 = row.size() > 8 ? row[8] : CellData{"", Qt::white};
        CellData c_bill2026 = row.size() > 9 ? row[9] : CellData{"", Qt::white};

        // Skip header row if it is one
        if (c_invoice.value.toLower() == "invoice_no" || c_invoice.value.toLower() == "invoice") continue;
        
        // Skip if all relevant columns are empty
        if (c_invoice.value.isEmpty() && c_container.value.isEmpty() && c_type.value.isEmpty()) continue;

        QList<QStandardItem*> items;
        auto addItem = [&](const CellData& cell) {
            QStandardItem* item = new QStandardItem(cell.value);
            if (cell.bgColor != Qt::white) {
                item->setBackground(cell.bgColor);
            }
            items.append(item);
        };

        addItem(c_invoice);
        addItem(c_container);
        addItem(c_type);
        addItem(c_truck);
        addItem(c_container2026);
        addItem(c_bill2026);
        
        m_tableModel->appendRow(items);
    }
    
    m_fetchBtn->setEnabled(true);
    m_fetchBtn->setText("Fetch 2026 Data");
    log(QString("Fetched and displayed %1 rows.").arg(m_tableModel->rowCount()));
}

void MainWindow::onFilterChanged(const QString& text) {
    m_proxyModel->setFilterRegularExpression(text);
}
