#include "CameraCalibrationTask.h"
#include <opencv2/core/mat.hpp>
#include <QFileDialog>
#include "Lib/json.hpp"
#include <opencv2/calib3d/calib3d_c.h>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>


using namespace cv;

CameraCalibrationTask::CameraCalibrationTask(QString savePath, vector<QString> imageLocations)
{
	save = savePath;
	locations = imageLocations;
}

CameraCalibrationTask::~CameraCalibrationTask()
{
}

void CameraCalibrationTask::run()
{
	//load all the point values from files
	vector<vector<Point2f>> pointdata = vector<vector<Point2f>>();
	for (int i = 0; i < locations.size(); ++i)
	{
		QString data = loadTextfile(locations.at(i));
		nlohmann::json jsonFile = nlohmann::json::parse(data.toStdString().c_str());

		vector<Point2f> points = vector<Point2f>();
		for (int j = 0; j < jsonFile["points"].size(); ++j)
		{
			Point2f newPoint = Point2f(jsonFile["points"][j]["x"], jsonFile["points"][j]["y"]);
			points.push_back(newPoint);
		}
		pointdata.push_back(points);
	}

	//generate object points
	vector<vector<Point3f>> objectPoints = vector<vector<Point3f>>();
	for (int h = 0; h < pointdata.size(); ++h)
	{
		vector< Point3f > obj;
		for (int i = 0; i < boardHeight; i++)
			for (int j = 0; j < boardWidth; j++)
				obj.push_back(Point3f(j * squareSize, i * squareSize, 0));
		objectPoints.push_back(obj);
	}

	Size imgSize;
	{
		QString location = locations[0];
		int dotQty = location.count('.') - 1;
		if (dotQty < 0) dotQty = 0;
		location = location.section('.', dotQty, dotQty) + ".jpg";

		Mat img = imread(location.toStdString());
		imgSize = img.size();
	}

	//calibrate the camera
	Mat K;
	Mat D;
	vector< Mat > rvecs, tvecs;
	int flag = 0;
	flag |= CV_CALIB_FIX_K4;
	flag |= CV_CALIB_FIX_K5;
	calibrateCamera(objectPoints, pointdata, imgSize, K, D, rvecs, tvecs, flag);

	const string path = save.toStdString();
	FileStorage fs(path, FileStorage::WRITE);
	fs << "K" << K;
	fs << "D" << D;
}

QString CameraCalibrationTask::loadTextfile(QString path)
{
	QFile text(path);
	QString jsonText = "";

	try {
		if (text.open(QIODevice::ReadOnly))
		{
			jsonText = text.readAll();
			text.close();
		}
	}
	catch (std::exception) {}
	if (text.isOpen()) text.close();

	return jsonText;
}
