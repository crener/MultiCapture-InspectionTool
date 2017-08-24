#include "StereoCalibrationTask.h"
#include <map>
#include "Lib/json.hpp"
#include <QFileDialog>
#include <opencv2/core/persistence.hpp>
#include <opencv2/calib3d/calib3d_c.h>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgcodecs.hpp>

using namespace nlohmann;

StereoCalibrationTask::StereoCalibrationTask(QString leftConfigPath, vector<QString> leftImageConfigLocations, QString rightConfigPath, vector<QString> rightImageConfigLocations, QString sampleImagePath, QString savePath)
{
	leftPath = leftConfigPath;
	rightPath = rightConfigPath;

	leftConfigImages = leftImageConfigLocations;
	rightConfigImages = leftImageConfigLocations;

	imageSamplePath = sampleImagePath;
	this->savePath = savePath;
}

StereoCalibrationTask::~StereoCalibrationTask()
{
}

void StereoCalibrationTask::run()
{
	generatePointData();

	Mat K1, K2, D1, D2, R, F, E;
	Vec3d T;
	int flags = CV_CALIB_FIX_INTRINSIC;
	Size imageSize = imread(imageSamplePath.toStdString()).size();
	{
		FileStorage leftCam(leftPath.toStdString(), FileStorage::READ);
		leftCam["K"] >> K1;
		leftCam["D"] >> D1;

		FileStorage rightCam(rightPath.toStdString(), FileStorage::READ);
		rightCam["K"] >> K2;
		rightCam["D"] >> D2;
	}


	stereoCalibrate(objectPoints, leftPoints, rightPoints, K1, D1, K2, D2, imageSize, R, T, E, F, flags);

	Mat R1, R2, P1, P2, Q;
	stereoRectify(K1, D1, K2, D2, imageSize, R, T, R1, R2, P1, P2, Q);

	//saveResult
	FileStorage stereoSave(savePath.toStdString(), FileStorage::WRITE);
	stereoSave << "K1" << K1;
	stereoSave << "K2" << K2;
	stereoSave << "D1" << D1;
	stereoSave << "D2" << D2;
	stereoSave << "R1" << R1;
	stereoSave << "R2" << R2;
	stereoSave << "P1" << P1;
	stereoSave << "P2" << P2;
	stereoSave << "R" << R;
	stereoSave << "T" << T;
	stereoSave << "E" << E;
	stereoSave << "F" << F;
	stereoSave << "Q" << Q;
}

void StereoCalibrationTask::generatePointData()
{
	leftPoints = vector<vector<Point2f>>();
	rightPoints = vector<vector<Point2f>>();
	objectPoints = vector<vector<Point3f>>();

	//build maps so that you can get paths in pairs knowning that they match
	//if an image is missing without this the pairs would be missaligned and lead to bad results
	map<QString, QString> tempLeftSet = map<QString, QString>();
	map<QString, QString> tempRightSet = map<QString, QString>();
	{
		for (int i = 0; i < leftConfigImages.size(); ++i)
		{
			QString path = leftConfigImages.at(i);
			int qty = path.count('/') - 2;
			QString set = path.section('/', qty, qty);

			tempLeftSet.emplace(set, path);
		}

		for (int i = 0; i < rightConfigImages.size(); ++i)
		{
			QString path = rightConfigImages.at(i);
			int qty = path.count('/') - 2;
			QString set = path.section('/', qty, qty);

			tempRightSet.emplace(set, path);
		}
	}

	//calculate greatest set number to avoid skipping a valid image pair
	int totalSize = leftConfigImages.size();
	{
		QString setId = leftConfigImages.back();
		int slash = setId.count('/') - 2;
		int parsedSize = setId.section('/', slash, slash).section('-', 1, 1).toInt();
		if (totalSize < parsedSize) totalSize = parsedSize;
	}
	{
		QString setId = rightConfigImages.back();
		int slash = setId.count('/') - 2;
		int parsedSize = setId.section('/', slash, slash).section('-', 1, 1).toInt();
		if (totalSize < parsedSize) totalSize = parsedSize;
	}

	//get the point data from file and load it to the relevent vectors
	for (int i = 1; i <= totalSize; ++i)
	{
		QString requestSet = "set-" + QString::number(i);

		bool valid = tempLeftSet.count(requestSet) && tempRightSet.count(requestSet);
		if (!valid) continue;

		vector<Point2f> left = loadPointData(tempLeftSet[requestSet]);
		if (left.size() == 0) continue;

		vector<Point2f> right = loadPointData(tempRightSet[requestSet]);
		if (right.size() == 0) continue;
		if (left.size() != right.size()) continue;

		leftPoints.push_back(left);
		rightPoints.push_back(right);
	}


	//generate objectPoints
	for (int h = 0; h < leftPoints.size(); ++h)
	{
		vector< Point3f > obj;
		for (int i = 0; i < boardHeight; i++)
			for (int j = 0; j < boardWidth; j++)
				obj.push_back(Point3f(j * squareSize, i * squareSize, 0));
		objectPoints.push_back(obj);
	}
}

vector<Point2f> StereoCalibrationTask::loadPointData(QString path)
{
	QFile text(path);
	QString data = "";

	try {
		if (text.open(QIODevice::ReadOnly))
		{
			data = text.readAll();
			text.close();
		}
	}
	catch (std::exception) {}
	if (text.isOpen()) text.close();

	vector<Point2f> points = vector<Point2f>();
	if (data.isEmpty()) return points;

	json jsonFile = json::parse(data.toStdString().c_str());

	for (int j = 0; j < jsonFile["points"].size(); ++j)
	{
		Point2f newPoint = Point2f(jsonFile["points"][j]["x"], jsonFile["points"][j]["y"]);
		points.push_back(newPoint);
	}

	return points;
}
