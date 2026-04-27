#ifndef DATAROW_H
#define DATAROW_H

#include <QString>
#include <QStringList>
#include <QColor>

struct CellData {
    QString value;
    QColor bgColor;
};

struct DataRow {
    QString bill;
    QString invoice_no;
    QString container_no;
    QString type;
    QString seal_no;
    QString truck_no;
    QString driver_name;
    QString cnee;
    QString date;

    static DataRow fromCsvRow(const QStringList& items) {
        DataRow row;
        if (items.size() >= 1) row.bill = items[0].trimmed();
        if (items.size() >= 2) row.invoice_no = items[1].trimmed();
        if (items.size() >= 3) row.container_no = items[2].trimmed();
        if (items.size() >= 4) row.type = items[3].trimmed();
        if (items.size() >= 5) row.seal_no = items[4].trimmed();
        if (items.size() >= 6) row.truck_no = items[5].trimmed();
        if (items.size() >= 7) row.driver_name = items[6].trimmed();
        if (items.size() >= 8) row.cnee = items[7].trimmed();
        if (items.size() >= 9) row.date = items[8].trimmed();
        return row;
    }

    QStringList toStringList() const {
        return { bill, invoice_no, container_no, type, seal_no, truck_no, driver_name, cnee, date };
    }
};

#endif // DATAROW_H
