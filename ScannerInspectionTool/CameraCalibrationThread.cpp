#include "CameraCalibrationThread.h"
#include "Lib/json.hpp"
#include <QFileDialog>
#include "CameraCalibrationTask.h"
#include "StereoCalibrationTask.h"
#include "parameterBuilder.h"


CameraCalibrationThread::CameraCalibrationThread(QString projectPath, const QString pairJson, ScannerInteraction *connector)
{
	path = projectPath;
	connection = connector;

	extractPairs(pairJson);
}

CameraCalibrationThread::~CameraCalibrationThread()
{
	for (int i = 0; i < pairs->size(); ++i)
		delete pairs->at(i);
	pairs->clear();
	delete pairs;
}

void CameraCalibrationThread::start()
{
	extractImages(path + "/project.scan");

	//generate the camera configurations
	for (map<int, vector<QString>>::iterator it = imageLocations.begin(); it != imageLocations.end(); ++it)
	{
		QString savePath = path + "/calibration/" + camNames[it->first] + "-calibration.json";

		CameraCalibrationTask* task = new CameraCalibrationTask(savePath, it->second);
		pool->start(task);
	}

	if (!QDir().exists(path + "/calibration")) QDir().mkdir(path + "/calibration");
	pool->waitForDone();

	//generate sterio calibration
	for (int i = 0; i < pairs->size(); ++i)
	{
		QString leftConfig = path + "/" + camNames[pairs->at(i)->leftId] + "-calibration.json";
		QString rightConfig = path + "/" + camNames[pairs->at(i)->rightId] + "-calibration.json";
		QString sampleImage = imageLocations.at(pairs->at(i)->leftId).front();
		QString savePath = path + "/calibration/" + QString::number(pairs->at(i)->id) + ".json";

		StereoCalibrationTask* task = new StereoCalibrationTask(leftConfig, imageLocations[pairs->at(i)->leftId],
			rightConfig, imageLocations[pairs->at(i)->rightId], sampleImage, savePath);

		pool->start(task);
	}
	pool->waitForDone();

	//update the scanner configuration data
	for (int i = 0; i < pairs->size(); ++i)
	{
		QString savePath = path + "/calibration/" + QString::number(pairs->at(i)->id) + ".json";
		QString configData = readText(savePath);

		emit connection->requestScanner(ScannerCommands::setCameraPairConfiguration, 
			parameterBuilder().addParam("id", QString::number(pairs->at(i)->id))->addParam("config", configData)->toString(), nullptr);
	}

	emit complete();
	deleteLater();
}

void CameraCalibrationThread::extractImages(QString projectFile)
{
	QString jsonText = readText(projectFile);
	if (jsonText == "") return

		imageLocations.clear();
	nlohmann::json json = nlohmann::json::parse(jsonText.toStdString().c_str());

	for (int i = 0; i < json["Cameras"].size(); ++i)
	{
		int camId = json["Cameras"][i]["id"];
		vector<QString> paths = vector<QString>();

		for (int j = 0; j < json["ImageSets"].size(); ++j)
		{
			QString set = QString::fromStdString(json["ImageSets"][j]["path"]);

			for (int k = 0; k < json["ImageSets"][j]["images"].size(); ++k)
			{
				if (json["ImageSets"][j]["images"][k]["id"] == camId)
				{
					QString image = QString::fromStdString(json["ImageSets"][j]["images"][k]["path"]);
					image = image.section('.', 0, 0) + ".conf";

					paths.push_back(path + "/" + set + "/calibration/" + image);
					break;
				}
			}
		}

		imageLocations.emplace(camId, paths);
		QString name = QString::fromStdString(json["Cameras"][i]["name"]);
		camNames.emplace(camId, name);
	}
}

void CameraCalibrationThread::extractPairs(const QString pairJson)
{
	nlohmann::json pairData = nlohmann::json::parse(pairJson.toStdString().c_str());

	for (int i = 0; i < pairData.size(); ++i)
	{
		CameraPair* pair = new CameraPair();
		pair->id = pairData.at(i)["pairId"];
		pair->leftId = pairData.at(i)["LeftCamera"];
		pair->rightId = pairData.at(i)["RightCamera"];

		pairs->push_back(pair);
	}
}

QString CameraCalibrationThread::readText(const QString path)
{
	QString readtext = "";
	QFile text(path);

	try {
		if (text.open(QIODevice::ReadOnly))
		{
			readtext = text.readAll();
			text.close();
		}
	}
	catch (std::exception) {}
	if (text.isOpen()) text.close();

	return readtext;
}
