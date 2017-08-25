#pragma once
#include <QObject>
#include "JsonTypes.h"
#include <qthreadpool.h>
#include "ScannerInteraction.h";

using namespace std;

class CameraCalibrationThread : public QObject
{
	Q_OBJECT
public:
	CameraCalibrationThread(QString projectPath, const QString pairJson, ScannerInteraction *connector);
	~CameraCalibrationThread();

	signals:
	void complete();

	public slots:
	void start();

private:
	void extractImages(QString projectFile);
	void extractPairs(const QString pairJson);

	QString readText(const QString path);

	QString path;
	ScannerInteraction* connection;
	vector<CameraPair*>* pairs = new vector<CameraPair*>();
	map<int, vector<QString>> imageLocations = map<int, vector<QString>>();
	map<int, QString> camNames = map<int, QString>();
	QThreadPool* pool = new QThreadPool(this);
};

