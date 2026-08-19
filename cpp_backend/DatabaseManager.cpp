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
            pallet_gross TEXT,
            PRIMARY KEY (bill, container_no, invoice_no)
        )
    )";

    if (!query.exec(createTable)) {
        qCritical() << "Error creating table:" << query.lastError().text();
        return false;
    }

    // Migration: Add pallet_gross column if it doesn't exist
    query.exec("ALTER TABLE container_table ADD COLUMN pallet_gross TEXT;");

    QString createCacheTable = R"(
        CREATE TABLE IF NOT EXISTS sheet_cache (
            sheet_name TEXT DEFAULT '2026',
            row_idx INT,
            col_idx INT,
            value TEXT,
            color TEXT
        )
    )";
    if (!query.exec(createCacheTable)) {
        qCritical() << "Error creating cache table:" << query.lastError().text();
        return false;
    }

    // Migration: Add sheet_name column if it doesn't exist
    query.exec("ALTER TABLE sheet_cache ADD COLUMN sheet_name TEXT DEFAULT '2026';");

    return true;
}

bool DatabaseManager::existsLocally(const QString& bill, const QString& container, const QString& invoice) {
    // 1. Check container_table
    QSqlQuery dbQuery;
    if (container.isEmpty()) {
        dbQuery.prepare("SELECT 1 FROM container_table WHERE bill = ? AND invoice_no = ? LIMIT 1");
        dbQuery.addBindValue(bill);
        dbQuery.addBindValue(invoice);
    } else {
        dbQuery.prepare("SELECT 1 FROM container_table WHERE bill = ? AND (container_no = ? OR container_no LIKE '%' || ? || '%') AND invoice_no = ? LIMIT 1");
        dbQuery.addBindValue(bill);
        dbQuery.addBindValue(container);
        dbQuery.addBindValue(container);
        dbQuery.addBindValue(invoice);
    }

    if (dbQuery.exec() && dbQuery.next()) {
        return true;
    }

    // 2. Check sheet_cache
    QSqlQuery query;
    if (container.isEmpty()) {
        query.prepare(R"(
            SELECT 1 FROM sheet_cache s1
            JOIN sheet_cache s3 ON s1.row_idx = s3.row_idx AND (s1.sheet_name = s3.sheet_name OR (s1.sheet_name IS NULL AND s3.sheet_name IS NULL))
            LEFT JOIN sheet_cache s2 ON s1.row_idx = s2.row_idx AND s2.col_idx = 8 AND (s1.sheet_name = s2.sheet_name OR (s1.sheet_name IS NULL AND s2.sheet_name IS NULL))
            WHERE (s1.sheet_name = '2026' OR s1.sheet_name IS NULL)
              AND s1.col_idx = 9 AND s1.value = ?
              AND s3.col_idx = 2 AND s3.value = ?
              AND (s2.value IS NULL OR s2.value = '')
            LIMIT 1
        )");
        query.addBindValue(bill);
        query.addBindValue(invoice);
    } else {
        query.prepare(R"(
            SELECT 1 FROM sheet_cache s1
            JOIN sheet_cache s2 ON s1.row_idx = s2.row_idx AND (s1.sheet_name = s2.sheet_name OR (s1.sheet_name IS NULL AND s2.sheet_name IS NULL))
            JOIN sheet_cache s3 ON s1.row_idx = s3.row_idx AND (s1.sheet_name = s3.sheet_name OR (s1.sheet_name IS NULL AND s3.sheet_name IS NULL))
            WHERE (s1.sheet_name = '2026' OR s1.sheet_name IS NULL)
              AND s1.col_idx = 9 AND s1.value = ?
              AND s2.col_idx = 8 AND s2.value LIKE '%' || ? || '%'
              AND s3.col_idx = 2 AND s3.value = ?
            LIMIT 1
        )");
        query.addBindValue(bill);
        query.addBindValue(container);
        query.addBindValue(invoice);
    }

    if (!query.exec()) {
        qWarning() << "Error checking existence in sheet_cache:" << query.lastError().text();
        return false;
    }

    return query.next();
}

bool DatabaseManager::saveBatch(const QList<DataRow>& rows) {
    if (rows.isEmpty()) return true;

    if (!m_db.transaction()) {
        qWarning() << "Failed to begin transaction:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery query;
    query.prepare(R"(
        INSERT OR IGNORE INTO container_table 
        (bill, invoice_no, container_no, type, seal_no, truck_no, driver_name, cnee, date, pallet_gross) 
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
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
        query.addBindValue(row.pallet_gross);

        if (!query.exec()) {
            qWarning() << "Error inserting row:" << query.lastError().text();
            m_db.rollback();
            return false;
        }
    }

    return m_db.commit();
}

bool DatabaseManager::saveSheetCache(const QString& sheetName, const QList<QList<CellData>>& rows) {
    if (!m_db.transaction()) {
        qWarning() << "Failed to begin transaction:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery query;
    query.prepare("DELETE FROM sheet_cache WHERE sheet_name = ?");
    query.addBindValue(sheetName);
    if (!query.exec()) {
        qWarning() << "Error clearing sheet cache for" << sheetName << ":" << query.lastError().text();
        m_db.rollback();
        return false;
    }

    query.prepare("INSERT INTO sheet_cache (sheet_name, row_idx, col_idx, value, color) VALUES (?, ?, ?, ?, ?)");

    for (int r = 0; r < rows.size(); ++r) {
        for (int c = 0; c < rows[r].size(); ++c) {
            query.addBindValue(sheetName);
            query.addBindValue(r);
            query.addBindValue(c);
            query.addBindValue(rows[r][c].value);
            query.addBindValue(rows[r][c].bgColor.name());
            if (!query.exec()) {
                qWarning() << "Error caching sheet data:" << query.lastError().text();
                m_db.rollback();
                return false;
            }
        }
    }
    return m_db.commit();
}

bool DatabaseManager::saveSheetCache(const QList<QList<CellData>>& rows) {
    return saveSheetCache("2026", rows);
}

QList<QList<CellData>> DatabaseManager::loadSheetCache(const QString& sheetName) {
    QList<QList<CellData>> rows;
    QSqlQuery query;
    query.prepare("SELECT row_idx, col_idx, value, color FROM sheet_cache WHERE sheet_name = ? ORDER BY row_idx, col_idx");
    query.addBindValue(sheetName);

    if (query.exec()) {
        while (query.next()) {
            int r = query.value(0).toInt();
            int c = query.value(1).toInt();
            QString val = query.value(2).toString();
            QString color = query.value(3).toString();

            if (r < 0 || c < 0) continue;

            while (rows.size() <= r) {
                rows.append(QList<CellData>());
            }
            while (rows[r].size() <= c) {
                rows[r].append(CellData{"", Qt::white});
            }
            rows[r][c] = CellData{val, QColor(color)};
        }
    } else {
        qWarning() << "Error loading sheet cache for" << sheetName << ":" << query.lastError().text();
    }
    return rows;
}

bool DatabaseManager::deleteLocally(const QString& invoiceId) {
    QSqlQuery query;
    query.prepare("DELETE FROM container_table WHERE invoice_no = ?");
    query.addBindValue(invoiceId);
    if (!query.exec()) {
        qWarning() << "Error deleting locally:" << query.lastError().text();
        return false;
    }
    return true;
}
