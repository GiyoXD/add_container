#include "GeminiClient.h"
#include <QFile>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QFileInfo>

GeminiClient::GeminiClient(const QString& apiKey, QObject *parent)
    : QObject(parent), m_apiKey(apiKey) {
    m_networkManager = new QNetworkAccessManager(this);
}

void GeminiClient::processImage(const QString& imagePath, const QStringList& clientIds) {
    m_clientIds = clientIds;
    emit statusUpdate(QString("Uploading %1 to Gemini...").arg(imagePath));

    QFile *file = new QFile(imagePath);
    if (!file->open(QIODevice::ReadOnly)) {
        emit error("Could not open image file.");
        delete file;
        return;
    }

    m_mimeType = "image/jpeg";
    if (imagePath.endsWith(".png", Qt::CaseInsensitive)) m_mimeType = "image/png";

    QUrl url("https://generativelanguage.googleapis.com/upload/v1beta/files?key=" + m_apiKey);
    
    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    
    // Metadata part
    QHttpPart metadataPart;
    metadataPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/json"));
    QJsonObject metadata;
    metadata["file"] = QJsonObject{{"display_name", QFileInfo(imagePath).fileName()}};
    metadataPart.setBody(QJsonDocument(metadata).toJson());
    
    // File part
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(m_mimeType));
    filePart.setBodyDevice(file);
    file->setParent(multiPart);
    
    multiPart->append(metadataPart);
    multiPart->append(filePart);
    
    QNetworkRequest request(url);
    request.setRawHeader("X-Goog-Upload-Protocol", "multipart");
    QNetworkReply *reply = m_networkManager->post(request, multiPart);
    multiPart->setParent(reply);
    
    connect(reply, &QNetworkReply::finished, this, &GeminiClient::onUploadFinished);
}

void GeminiClient::onUploadFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply->error() != QNetworkReply::NoError) {
        QString responseBody = reply->readAll();
        emit error(QString("Upload failed (%1): %2").arg(reply->errorString()).arg(responseBody));
        reply->deleteLater();
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    m_fileUri = doc.object()["file"].toObject()["uri"].toString();
    m_fileName = doc.object()["file"].toObject()["name"].toString();
    
    reply->deleteLater();
    
    emit statusUpdate("Asking Gemini to analyze the image...");
    generateContent();
}

void GeminiClient::generateContent() {
    QUrl url("https://generativelanguage.googleapis.com/v1beta/models/gemini-3.1-flash-lite-preview:generateContent?key=" + m_apiKey);
    
    QJsonObject requestBody;
    QJsonArray contents;
    QJsonObject content;
    QJsonArray parts;
    
    QJsonObject textPart;
    textPart["text"] = R"(
    Analyze this image of a shipping spreadsheet.
    Extract all data rows. For each row, extract the following 9 columns exactly in this order:
    1. TBL NO (This is the BILL)
    2. SHIPPER (The invoice ID)
    3. CONTAINER NO.
    4. TYPE
    5. SEAL NO.
    6. TRUCK NO. (If the content here is just a truck size, replace it with the actual truck plate no. Look carefully).
    7. DRIVER NAME
    8. CNEE
    9. DATE

    Return ONLY a CSV format with one row per line. Do not include headers, labels, or any other text.
    Use a comma as the separator. If a value contains a comma, omit it or replace with a space.
    )";
    
    QJsonObject filePart;
    QJsonObject fileData;
    fileData["mime_type"] = m_mimeType;
    fileData["file_uri"] = m_fileUri;
    filePart["file_data"] = fileData;
    
    parts.append(textPart);
    parts.append(filePart);
    content["parts"] = parts;
    contents.append(content);
    requestBody["contents"] = contents;
    
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply *reply = m_networkManager->post(request, QJsonDocument(requestBody).toJson());
    connect(reply, &QNetworkReply::finished, this, &GeminiClient::onGenerateFinished);
}

void GeminiClient::onGenerateFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
    if (reply->error() != QNetworkReply::NoError) {
        QString responseBody = reply->readAll();
        emit error(QString("Generation failed (%1): %2").arg(reply->errorString()).arg(responseBody));
        reply->deleteLater();
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    QString text = doc.object()["candidates"].toArray()[0].toObject()["content"].toObject()["parts"].toArray()[0].toObject()["text"].toString();
    
    reply->deleteLater();
    
    // Cleanup the file from Gemini
    QUrl deleteUrl("https://generativelanguage.googleapis.com/v1beta/" + m_fileName + "?key=" + m_apiKey);
    m_networkManager->deleteResource(QNetworkRequest(deleteUrl));

    QStringList lines = text.trimmed().split('\n');
    QList<DataRow> extractedRows;
    
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();
        if (line.isEmpty()) continue;
        
        QStringList items = line.split(',');
        DataRow row = DataRow::fromCsvRow(items);
        
        // Replace invoice_no with provided client ID if available
        if (i < m_clientIds.size()) {
            row.invoice_no = m_clientIds[i];
        }
        
        extractedRows.append(row);
    }
    
    emit finished(extractedRows);
}
