#ifndef GEMINICLIENT_H
#define GEMINICLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "DataRow.h"

class GeminiClient : public QObject {
    Q_OBJECT
public:
    explicit GeminiClient(const QString& apiKey, const QString& modelName, QObject *parent = nullptr);
    void setApiKey(const QString& apiKey) { m_apiKey = apiKey; }
    void setModelName(const QString& modelName) { m_modelName = modelName; }
    void processImage(const QString& imagePath, const QStringList& clientIds);

signals:
    void finished(const QList<DataRow>& rows);
    void error(const QString& message);
    void statusUpdate(const QString& status);

private slots:
    void onUploadFinished();
    void onGenerateFinished();

private:
    QString m_apiKey;
    QString m_modelName;
    QNetworkAccessManager *m_networkManager;
    QStringList m_clientIds;
    QString m_fileUri;
    QString m_fileName;
    QString m_mimeType;

    void generateContent();
};

#endif // GEMINICLIENT_H
