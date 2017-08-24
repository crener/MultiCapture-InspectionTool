#pragma once
#include <vector>
#include <QString>
#include <qrunnable.h>
#include <qobjectdefs.h>
#include <opencv2/core/mat.hpp>

using namespace std;
using namespace cv;

class StereoCalibrationTask : public QRunnable
{
public:
	StereoCalibrationTask(QString leftConfigPath, vector<QString> leftImageConfigLocations, QString rightConfigPath, vector<QString> rightImageConfigLocations, QString sampleImagePath, QString savePath);
	~StereoCalibrationTask();

	void run() override;

private:
	void generatePointData();
	vector<Point2f> loadPointData(QString path);

	QString leftPath, rightPath;
	QString imageSamplePath;
	QString savePath;
	vector<QString> leftConfigImages, rightConfigImages;

	const float squareSize = 24.23; // in mm
	const int boardWidth = 9;
	const int boardHeight = 6;

	vector<vector<Point2f>> leftPoints, rightPoints;
	vector<vector<Point3f>> objectPoints;
};

