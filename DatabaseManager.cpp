#include "DatabaseManager.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>
#include <QFileInfo>
#include <QDir>

DatabaseManager::DatabaseManager(const QString& dbPath, QObject *parent)
    : QObject(parent), m_dbPath(dbPath) {
}

DatabaseManager::~DatabaseManager() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool DatabaseManager::setupDatabase() {
    QFileInfo dbFileInfo(m_dbPath);
    QDir().mkpath(dbFileInfo.absolutePath());

    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        qCritical() << "Error opening database:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery query;
    QString createTable = R"(
        CREATE TABLE IF NOT EXISTS container_table (
            bill TEXT,
            invoice_no TEXT,
            container_no TEXT,
            type TEXT,
            seal_no TEXT,
            truck_no TEXT,
            driver_name TEXT,
            cnee TEXT,
            date TEXT,
            PRIMARY KEY (bill, container_no, invoice_no)
        )
    )";

    if (!query.exec(createTable)) {
        qCritical() << "Error creating table:" << query.lastError().text();
        return false;
    }

    return true;
}

bool DatabaseManager::existsLocally(const QString& bill, const QString& container, const QString& invoice) {
    QSqlQuery query;
    query.prepare("SELECT 1 FROM container_table WHERE bill=? AND container_no=? AND invoice_no=?");
    query.addBindValue(bill);
    query.addBindValue(container);
    query.addBindValue(invoice);

    if (!query.exec()) {
        qWarning() << "Error checking existence:" << query.lastError().text();
        return false;
    }

    return query.next();
}

bool DatabaseManager::saveBatch(const QList<DataRow>& rows) {
    if (rows.isEmpty()) return true;

    m_db.transaction();
    QSqlQuery query;
    query.prepare(R"(
        INSERT OR IGNORE INTO container_table 
        (bill, invoice_no, container_no, type, seal_no, truck_no, driver_name, cnee, date) 
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");

    for (const auto& row : rows) {
        query.addBindValue(row.bill);
        query.addBindValue(row.invoice_no);
        query.addBindValue(row.container_no);
        query.addBindValue(row.type);
        query.addBindValue(row.seal_no);
        query.addBindValue(row.truck_no);
        query.addBindValue(row.driver_name);
        query.addBindValue(row.cnee);
        query.addBindValue(row.date);

        if (!query.exec()) {
            qWarning() << "Error inserting row:" << query.lastError().text();
        }
    }

    return m_db.commit();
}
