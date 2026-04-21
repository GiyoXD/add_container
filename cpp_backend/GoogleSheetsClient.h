#ifndef GOOGLESHEETSCLIENT_H
#define GOOGLESHEETSCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStringList>
#include <QColor>
#include "DataRow.h"

struct CellData {
    QString value;
    QColor bgColor;
};

class GoogleSheetsClient : public QObject {
    Q_OBJECT
public:
    enum class PendingAction { None, Append, Fetch };

    explicit GoogleSheetsClient(const QString& serviceAccountPath, const QString& spreadsheetId, QObject *parent = nullptr);
    void appendRows(const QList<DataRow>& rows);
    void fetchSheetData(const QString& range);

signals:
    void finished();
    void dataFetched(const QList<QList<CellData>>& rows);
    void error(const QString& message);
    void statusUpdate(const QString& status);

private slots:
    void onTokenReceived();
    void onAppendFinished();
    void onFetchFinished();

private:
    QString m_serviceAccountPath;
    QString m_spreadsheetId;
    QString m_accessToken;
    QNetworkAccessManager *m_networkManager;
    QList<DataRow> m_pendingRows;
    QString m_pendingFetchRange;
    PendingAction m_pendingAction = PendingAction::None;

    void requestAccessToken();
    QString createJwt();
    QString base64UrlEncode(const QByteArray& data);
    void executePendingAction();
};

#endif // GOOGLESHEETSCLIENT_H
