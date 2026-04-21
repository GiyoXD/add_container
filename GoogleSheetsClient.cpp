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

GoogleSheetsClient::GoogleSheetsClient(const QString& serviceAccountPath, const QString& spreadsheetId, QObject *parent)
    : QObject(parent), m_serviceAccountPath(serviceAccountPath), m_spreadsheetId(spreadsheetId) {
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

void GoogleSheetsClient::fetchSheetData(const QString& range) {
    m_pendingFetchRange = range;
    m_pendingAction = PendingAction::Fetch;
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
    QFile file(m_serviceAccountPath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit error("Service account file not found.");
        return "";
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject obj = doc.object();
    QString privateKeyStr = obj["private_key"].toString();
    QString clientEmail = obj["client_email"].toString();

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
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_KEY_HANDLE hKey = NULL;
    DWORD cbKeyBlob = 0, cbSignature = 0;
    PUCHAR pbKeyBlob = NULL, pbSignature = NULL;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_RSA_ALGORITHM, NULL, 0) != 0) return "";
    
    // Importing PKCS#8 is complex in BCrypt, simpler to use NCrypt for PEM/DER
    NCRYPT_PROV_HANDLE hProv = NULL;
    NCRYPT_KEY_HANDLE hNKey = NULL;
    if (NCryptOpenStorageProvider(&hProv, MS_KEY_STORAGE_PROVIDER, 0) != 0) return "";
    
    if (NCryptImportKey(hProv, NULL, NCRYPT_PKCS8_PRIVATE_KEY_BLOB, NULL, &hNKey, (PBYTE)derKey.data(), derKey.size(), 0) != 0) {
        NCryptFreeObject(hProv);
        return "";
    }

    BCRYPT_PKCS1_PADDING_INFO padInfo;
    padInfo.pszAlgId = BCRYPT_SHA256_ALGORITHM;

    QByteArray hash = QCryptographicHash::hash(unsignedJwt.toUtf8(), QCryptographicHash::Sha256);
    
    DWORD cbSig = 0;
    NCryptSignHash(hNKey, &padInfo, (PBYTE)hash.data(), hash.size(), NULL, 0, &cbSig, BCRYPT_PAD_PKCS1);
    signature.resize(cbSig);
    NCryptSignHash(hNKey, &padInfo, (PBYTE)hash.data(), hash.size(), (PBYTE)signature.data(), signature.size(), &cbSig, BCRYPT_PAD_PKCS1);

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
            values.append(rowVals);
        }
        body["values"] = values;

        QNetworkReply *appendReply = m_networkManager->post(request, QJsonDocument(body).toJson());
        connect(appendReply, &QNetworkReply::finished, this, &GoogleSheetsClient::onAppendFinished);
    } else if (m_pendingAction == PendingAction::Fetch) {
        emit statusUpdate("Fetching data from Google Sheets...");

        QUrl url(QString("https://sheets.googleapis.com/v4/spreadsheets/%1")
                 .arg(m_spreadsheetId));
        
        QUrlQuery query;
        query.addQueryItem("ranges", m_pendingFetchRange);
        query.addQueryItem("fields", "sheets(data(rowData(values(effectiveValue,effectiveFormat(backgroundColor)))))");
        url.setQuery(query);

        QNetworkRequest request(url);
        request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());

        QNetworkReply *fetchReply = m_networkManager->get(request);
        connect(fetchReply, &QNetworkReply::finished, this, &GoogleSheetsClient::onFetchFinished);
    }
    m_pendingAction = PendingAction::None;
}

void GoogleSheetsClient::onFetchFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply->error() != QNetworkReply::NoError) {
        emit error("Fetch failed: " + reply->errorString());
    } else {
        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject root = doc.object();
        QJsonArray sheets = root["sheets"].toArray();
        
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

        emit statusUpdate("Successfully fetched data from Google Sheets.");
        emit dataFetched(result);
    }
    reply->deleteLater();
}

void GoogleSheetsClient::onAppendFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply->error() != QNetworkReply::NoError) {
        emit error("Append failed: " + reply->errorString());
    } else {
        emit statusUpdate("Successfully pushed to Google Sheets.");
        emit finished();
    }
    reply->deleteLater();
}
