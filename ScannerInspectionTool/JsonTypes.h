#pragma once
#include <QStandardItemModel.h>

struct Set;
struct Image
{
	int cameraId;
	QString fileName;

	QStandardItem* item;
};

struct Set
{
	int setId;
	QString name;
	QList<Image*>* images = new QList<Image*>();

	QStandardItem* item;
};

enum CalibrationValidity
{
	Pending,
	Valid,
	Invalid,
	Uncaptured,
	Missing
};

struct CalibrationImage
{
	int cameraId;
	QString fileName;
	CalibrationValidity valid = Pending;
};

struct CameraPair
{
	int leftId, rightId, id, workingCount;
	CalibrationValidity valid = Pending;
};

struct CalibrationSet
{
	int setId;
	QString name;
	QList<CalibrationImage*>* images = new QList<CalibrationImage*>();
	std::vector<CalibrationValidity>* pairs = new std::vector<CalibrationValidity>();
};