#include "GoogleSheetsClient.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QUrlQuery>
#include <QDebug>
#include <QCryptographicHash>

#ifdef Q_OS_WIN
#include <windows.h>
#include <bcrypt.h>
#include <ncrypt.h>
#endif

GoogleSheetsClient::GoogleSheetsClient(const QString& serviceAccountData, const QString& spreadsheetId, QObject *parent)
    : QObject(parent), m_serviceAccountData(serviceAccountData), m_spreadsheetId(spreadsheetId) {
    m_networkManager = new QNetworkAccessManager(this);
}

void GoogleSheetsClient::appendRows(const QList<DataRow>& rows) {
    m_pendingRows = rows;
    m_pendingAction = PendingAction::Append;
    if (m_accessToken.isEmpty()) {
        emit statusUpdate("Authenticating with Google...");
        requestAccessToken();
    } else {
        executePendingAction();
    }
}

void GoogleSheetsClient::fetchSheetData(const QString& sheetName, const QString& range) {
    m_pendingFetchSheetName = sheetName;
    m_pendingFetchRange = range;
    m_pendingAction = PendingAction::Fetch;
    if (m_accessToken.isEmpty()) {
        emit statusUpdate("Authenticating with Google...");
        requestAccessToken();
    } else {
        executePendingAction();
    }
}

void GoogleSheetsClient::fetchSheetData(const QString& range) {
    QString sheetName = range;
    if (sheetName.contains("!")) {
        sheetName = sheetName.left(sheetName.indexOf("!"));
        sheetName.remove("'");
    }
    fetchSheetData(sheetName, range);
}

void GoogleSheetsClient::updateCell(const QString& range, const QString& value) {
    m_pendingUpdateRange = range;
    m_pendingUpdateValue = value;
    m_pendingAction = PendingAction::UpdateCell;
    if (m_accessToken.isEmpty()) {
        emit statusUpdate("Authenticating with Google...");
        requestAccessToken();
    } else {
        executePendingAction();
    }
}

void GoogleSheetsClient::requestAccessToken() {
    QString jwt = createJwt();
    if (jwt.isEmpty()) return;

    QUrl url("https://oauth2.googleapis.com/token");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QUrlQuery params;
    params.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:jwt-bearer");
    params.addQueryItem("assertion", jwt);

    QNetworkReply *reply = m_networkManager->post(request, params.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, &GoogleSheetsClient::onTokenReceived);
}

QString GoogleSheetsClient::createJwt() {
    QByteArray jsonData;
    if (m_serviceAccountData.trimmed().startsWith("{")) {
        jsonData = m_serviceAccountData.toUtf8();
    } else {
        QFile file(m_serviceAccountData);
        if (!file.open(QIODevice::ReadOnly)) {
            emit error("Service account file not found: " + m_serviceAccountData);
            return "";
        }
        jsonData = file.readAll();
    }

    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (doc.isNull()) {
        emit error("Invalid Service Account JSON.");
        return "";
    }
    QJsonObject obj = doc.object();
    QString privateKeyStr = obj["private_key"].toString();
    QString clientEmail = obj["client_email"].toString();

    if (privateKeyStr.isEmpty() || clientEmail.isEmpty()) {
        emit error("Missing private_key or client_email in Service Account JSON.");
        return "";
    }

    // Remove PEM headers and footers, and whitespace
    privateKeyStr.replace("-----BEGIN PRIVATE KEY-----", "").replace("-----END PRIVATE KEY-----", "").replace("\n", "").replace("\r", "").trimmed();
    QByteArray derKey = QByteArray::fromBase64(privateKeyStr.toUtf8());

    QJsonObject header;
    header["alg"] = "RS256";
    header["typ"] = "JWT";

    qint64 now = QDateTime::currentSecsSinceEpoch();
    QJsonObject payload;
    payload["iss"] = clientEmail;
    payload["scope"] = "https://www.googleapis.com/auth/spreadsheets";
    payload["aud"] = "https://oauth2.googleapis.com/token";
    payload["exp"] = now + 3600;
    payload["iat"] = now;

    QString headerB64 = base64UrlEncode(QJsonDocument(header).toJson(QJsonDocument::Compact));
    QString payloadB64 = base64UrlEncode(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QString unsignedJwt = headerB64 + "." + payloadB64;

    QByteArray signature;
#ifdef Q_OS_WIN
    NCRYPT_PROV_HANDLE hProv = NULL;
    NCRYPT_KEY_HANDLE hNKey = NULL;
    if (NCryptOpenStorageProvider(&hProv, MS_KEY_STORAGE_PROVIDER, 0) != 0) {
        emit error("NCryptOpenStorageProvider failed.");
        return "";
    }
    
    if (NCryptImportKey(hProv, NULL, NCRYPT_PKCS8_PRIVATE_KEY_BLOB, NULL, &hNKey, (PBYTE)derKey.data(), derKey.size(), 0) != 0) {
        NCryptFreeObject(hProv);
        emit error("NCryptImportKey failed.");
        return "";
    }

    BCRYPT_PKCS1_PADDING_INFO padInfo;
    padInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;

    QByteArray hash = QCryptographicHash::hash(unsignedJwt.toUtf8(), QCryptographicHash::Sha256);
    
    DWORD cbSig = 0;
    SECURITY_STATUS status = NCryptSignHash(hNKey, &padInfo, (PBYTE)hash.data(), hash.size(), NULL, 0, &cbSig, BCRYPT_PAD_PKCS1);
    if (status == 0 && cbSig > 0) {
        signature.resize(cbSig);
        NCryptSignHash(hNKey, &padInfo, (PBYTE)hash.data(), hash.size(), (PBYTE)signature.data(), signature.size(), &cbSig, BCRYPT_PAD_PKCS1);
    }

    NCryptFreeObject(hNKey);
    NCryptFreeObject(hProv);
#endif

    if (signature.isEmpty()) {
        emit error("Failed to sign JWT.");
        return "";
    }

    QString signatureB64 = base64UrlEncode(signature);
    return unsignedJwt + "." + signatureB64;
}

QString GoogleSheetsClient::base64UrlEncode(const QByteArray& data) {
    return QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

void GoogleSheetsClient::onTokenReceived() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply) {
        if (reply->error() != QNetworkReply::NoError) {
            emit error("Authentication failed: " + reply->errorString());
            reply->deleteLater();
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        m_accessToken = doc.object()["access_token"].toString();
        reply->deleteLater();
    }

    executePendingAction();
}

void GoogleSheetsClient::executePendingAction() {
    if (m_pendingAction == PendingAction::Append) {
        emit statusUpdate("Pushing data to Google Sheets...");

        QUrl url(QString("https://sheets.googleapis.com/v4/spreadsheets/%1/values/%2!A1:append?valueInputOption=RAW")
                 .arg(m_spreadsheetId).arg("CONTAINER"));
        
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject body;
        QJsonArray values;
        for (const auto& row : m_pendingRows) {
            QJsonArray rowVals;
            rowVals.append(row.bill);
            rowVals.append(row.invoice_no);
            rowVals.append(row.container_no);
            rowVals.append(row.type);
            rowVals.append(row.seal_no);
            rowVals.append(row.truck_no);
            rowVals.append(row.driver_name);
            rowVals.append(row.cnee);
            rowVals.append(row.date);
            rowVals.append(""); // Column J
            rowVals.append(""); // Column K
            rowVals.append(row.pallet_gross); // Column L
            values.append(rowVals);
        }
        body["values"] = values;

        QNetworkReply *appendReply = m_networkManager->post(request, QJsonDocument(body).toJson());
        appendReply->setProperty("actionType", static_cast<int>(PendingAction::Append));
        connect(appendReply, &QNetworkReply::finished, this, &GoogleSheetsClient::onAppendFinished);
    } else if (m_pendingAction == PendingAction::Fetch) {
        emit statusUpdate(QString("Fetching data from Google Sheets (%1)...").arg(m_pendingFetchSheetName.isEmpty() ? m_pendingFetchRange : m_pendingFetchSheetName));

        QUrl url(QString("https://sheets.googleapis.com/v4/spreadsheets/%1")
                 .arg(m_spreadsheetId));
        
        QUrlQuery query;
        query.addQueryItem("ranges", m_pendingFetchRange);
        query.addQueryItem("fields", "sheets(properties(title),data(rowData(values(effectiveValue,effectiveFormat(backgroundColor)))))");
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());

        QNetworkReply *fetchReply = m_networkManager->get(request);
        fetchReply->setProperty("sheetName", m_pendingFetchSheetName);
        fetchReply->setProperty("range", m_pendingFetchRange);
        connect(fetchReply, &QNetworkReply::finished, this, &GoogleSheetsClient::onFetchFinished);
    } else if (m_pendingAction == PendingAction::UpdateCell) {
        emit statusUpdate(QString("Updating cell %1 to %2...").arg(m_pendingUpdateRange).arg(m_pendingUpdateValue));

        QString encodedRange = QString::fromUtf8(QUrl::toPercentEncoding(m_pendingUpdateRange));
        QUrl url(QString("https://sheets.googleapis.com/v4/spreadsheets/%1/values/%2")
                 .arg(m_spreadsheetId).arg(encodedRange));
        
        QUrlQuery query;
        query.addQueryItem("valueInputOption", "USER_ENTERED");
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject body;
        QJsonArray values;
        QJsonArray rowVals;
        rowVals.append(m_pendingUpdateValue);
        values.append(rowVals);
        body["values"] = values;

        QNetworkReply *updateReply = m_networkManager->put(request, QJsonDocument(body).toJson());
        updateReply->setProperty("actionType", static_cast<int>(PendingAction::UpdateCell));
        connect(updateReply, &QNetworkReply::finished, this, &GoogleSheetsClient::onAppendFinished);
    } else if (m_pendingAction == PendingAction::DeleteContainerRow) {
        emit statusUpdate("Fetching spreadsheet metadata for GID...");
        QUrl url(QString("https://sheets.googleapis.com/v4/spreadsheets/%1?fields=sheets.properties")
                 .arg(m_spreadsheetId));
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
        
        QNetworkReply *reply = m_networkManager->get(request);
        connect(reply, &QNetworkReply::finished, this, &GoogleSheetsClient::onDeleteMetadataReceived);
    }
    m_pendingAction = PendingAction::None;
}

void GoogleSheetsClient::onFetchFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::AuthenticationRequiredError || httpStatus == 401) {
        emit statusUpdate("Access token expired. Re-authenticating with Google...");
        m_accessToken.clear();
        m_pendingFetchSheetName = reply->property("sheetName").toString();
        m_pendingFetchRange = reply->property("range").toString();
        m_pendingAction = PendingAction::Fetch;
        reply->deleteLater();
        requestAccessToken();
        return;
    }

    QString sheetName = reply->property("sheetName").toString();

    if (reply->error() != QNetworkReply::NoError) {
        emit error("Fetch failed: " + reply->errorString());
    } else {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        QJsonArray sheets = root["sheets"].toArray();
        
        if (sheetName.isEmpty() && !sheets.isEmpty()) {
            sheetName = sheets[0].toObject()["properties"].toObject()["title"].toString();
        }
        if (sheetName.isEmpty()) sheetName = "2026";

        QList<QList<CellData>> result;
        if (!sheets.isEmpty()) {
            QJsonArray data = sheets[0].toObject()["data"].toArray();
            if (!data.isEmpty()) {
                QJsonArray rowData = data[0].toObject()["rowData"].toArray();
                for (int i = 0; i < rowData.size(); ++i) {
                    QJsonArray values = rowData[i].toObject()["values"].toArray();
                    QList<CellData> row;
                    for (int j = 0; j < values.size(); ++j) {
                        QJsonObject cellObj = values[j].toObject();
                        CellData cell;
                        
                        // Parse value
                        QJsonObject effValue = cellObj["effectiveValue"].toObject();
                        if (effValue.contains("stringValue")) cell.value = effValue["stringValue"].toString();
                        else if (effValue.contains("numberValue")) cell.value = QString::number(effValue["numberValue"].toDouble());
                        else if (effValue.contains("boolValue")) cell.value = effValue["boolValue"].toBool() ? "TRUE" : "FALSE";
                        
                        // Parse background color
                        QJsonObject bgColor = cellObj["effectiveFormat"].toObject()["backgroundColor"].toObject();
                        if (!bgColor.isEmpty()) {
                            double r = bgColor["red"].toDouble(0.0);
                            double g = bgColor["green"].toDouble(0.0);
                            double b = bgColor["blue"].toDouble(0.0);
                            cell.bgColor = QColor::fromRgbF(r, g, b);
                        } else {
                            cell.bgColor = Qt::white;
                        }
                        row.append(cell);
                    }
                    result.append(row);
                }
            }
        }

        emit statusUpdate(QString("Successfully fetched data for '%1' from Google Sheets.").arg(sheetName));
        emit dataFetched(sheetName, result);
        emit dataFetched(result);
    }
    reply->deleteLater();
}

void GoogleSheetsClient::onAppendFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::AuthenticationRequiredError || httpStatus == 401) {
        emit statusUpdate("Access token expired. Re-authenticating with Google...");
        m_accessToken.clear();
        int actionType = reply->property("actionType").toInt();
        if (actionType == static_cast<int>(PendingAction::UpdateCell)) {
            m_pendingAction = PendingAction::UpdateCell;
        } else {
            m_pendingAction = PendingAction::Append;
        }
        reply->deleteLater();
        requestAccessToken();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        emit error("Operation failed: " + reply->errorString());
    } else {
        emit statusUpdate("Successfully pushed to Google Sheets.");
        emit finished();
    }
    reply->deleteLater();
}

void GoogleSheetsClient::deleteContainerRow(const QString& invoiceId) {
    m_pendingDeleteInvoiceId = invoiceId;
    m_pendingAction = PendingAction::DeleteContainerRow;
    if (m_accessToken.isEmpty()) {
        emit statusUpdate("Authenticating with Google...");
        requestAccessToken();
    } else {
        executePendingAction();
    }
}

void GoogleSheetsClient::onDeleteMetadataReceived() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::AuthenticationRequiredError || httpStatus == 401) {
        emit statusUpdate("Access token expired. Re-authenticating with Google...");
        m_accessToken.clear();
        m_pendingAction = PendingAction::DeleteContainerRow;
        reply->deleteLater();
        requestAccessToken();
        return;
    }
    
    if (reply->error() != QNetworkReply::NoError) {
        emit error("Failed to fetch sheet properties: " + reply->errorString());
        reply->deleteLater();
        return;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();
    
    int containerSheetId = -1;
    QJsonArray sheets = doc.object()["sheets"].toArray();
    for (const auto& sheetVal : sheets) {
        QJsonObject props = sheetVal.toObject()["properties"].toObject();
        if (props["title"].toString() == "CONTAINER") {
            containerSheetId = props["sheetId"].toInt();
            break;
        }
    }
    
    if (containerSheetId == -1) {
        emit error("CONTAINER sheet not found in spreadsheet.");
        return;
    }
    
    m_deleteContainerSheetId = containerSheetId;
    
    emit statusUpdate("Scanning CONTAINER sheet for matching invoices...");
    QUrl url(QString("https://sheets.googleapis.com/v4/spreadsheets/%1/values/CONTAINER!B:B")
             .arg(m_spreadsheetId));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    
    QNetworkReply *fetchReply = m_networkManager->get(request);
    connect(fetchReply, &QNetworkReply::finished, this, &GoogleSheetsClient::onDeleteRowsFetched);
}

void GoogleSheetsClient::onDeleteRowsFetched() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::AuthenticationRequiredError || httpStatus == 401) {
        emit statusUpdate("Access token expired. Re-authenticating with Google...");
        m_accessToken.clear();
        m_pendingAction = PendingAction::DeleteContainerRow;
        reply->deleteLater();
        requestAccessToken();
        return;
    }
    
    if (reply->error() != QNetworkReply::NoError) {
        emit error("Failed to fetch CONTAINER rows: " + reply->errorString());
        reply->deleteLater();
        return;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();
    
    QJsonArray values = doc.object()["values"].toArray();
    QList<int> rowsToDelete;
    
    for (int i = 0; i < values.size(); ++i) {
        QJsonArray rowVals = values[i].toArray();
        if (!rowVals.isEmpty()) {
            QString inv = rowVals[0].toString().trimmed();
            if (inv.compare(m_pendingDeleteInvoiceId.trimmed(), Qt::CaseInsensitive) == 0) {
                rowsToDelete.append(i);
            }
        }
    }
    
    if (rowsToDelete.isEmpty()) {
        emit statusUpdate(QString("No matching rows for Invoice '%1' found in CONTAINER sheet.").arg(m_pendingDeleteInvoiceId));
        emit finished();
        return;
    }
    
    // Sort descending so indices don't shift during deletion
    std::sort(rowsToDelete.begin(), rowsToDelete.end(), std::greater<int>());
    
    emit statusUpdate(QString("Deleting %1 matching rows from CONTAINER sheet...").arg(rowsToDelete.size()));
    QUrl url(QString("https://sheets.googleapis.com/v4/spreadsheets/%1:batchUpdate")
             .arg(m_spreadsheetId));
             
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QJsonObject body;
    QJsonArray requests;
    for (int rowIndex : rowsToDelete) {
        QJsonObject deleteReq;
        QJsonObject deleteDimension;
        QJsonObject range;
        range["sheetId"] = m_deleteContainerSheetId;
        range["dimension"] = "ROWS";
        range["startIndex"] = rowIndex;
        range["endIndex"] = rowIndex + 1;
        
        deleteDimension["range"] = range;
        deleteReq["deleteDimension"] = deleteDimension;
        requests.append(deleteReq);
    }
    body["requests"] = requests;
    
    QNetworkReply *updateReply = m_networkManager->post(request, QJsonDocument(body).toJson());
    connect(updateReply, &QNetworkReply::finished, this, &GoogleSheetsClient::onDeleteFinished);
}

void GoogleSheetsClient::onDeleteFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;

    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::AuthenticationRequiredError || httpStatus == 401) {
        emit statusUpdate("Access token expired. Re-authenticating with Google...");
        m_accessToken.clear();
        m_pendingAction = PendingAction::DeleteContainerRow;
        reply->deleteLater();
        requestAccessToken();
        return;
    }
    
    if (reply->error() != QNetworkReply::NoError) {
        emit error("Delete failed: " + reply->errorString());
    } else {
        emit statusUpdate("Successfully deleted matching rows from CONTAINER sheet.");
        emit finished();
    }
    reply->deleteLater();
}
