#pragma once
#include <qrunnable.h>
#include <vector>
#include <qstring.h>

using namespace std;

class CameraCalibrationTask : public QRunnable
{
public:
	CameraCalibrationTask(QString savePath, std::vector<QString> imageLocations);
	~CameraCalibrationTask();

	void run() override;

private:
	QString loadTextfile(QString path);

	const float squareSize = 24.23; // in mm
	const int boardWidth = 9;
	const int boardHeight = 6;

	QString save;
	vector<QString> locations;
};

