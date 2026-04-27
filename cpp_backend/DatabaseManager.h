#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include "DataRow.h"

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    explicit DatabaseManager(const QString& dbPath, QObject *parent = nullptr);
    ~DatabaseManager();

    bool setupDatabase();
    bool existsLocally(const QString& bill, const QString& container, const QString& invoice);
    bool saveBatch(const QList<DataRow>& rows);

    // Cache fetched sheet data
    bool saveSheetCache(const QList<QList<CellData>>& rows);
    QList<QList<CellData>> loadSheetCache();

private:
    QSqlDatabase m_db;
    QString m_dbPath;
};

#endif // DATABASEMANAGER_H
