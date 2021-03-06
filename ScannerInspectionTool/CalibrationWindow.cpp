#include "CalibrationWindow.h"
#include <QFileDialog>
#include "Lib/json.hpp"
#include "JsonTypes.h"
#include <QGraphicsPixmapItem>
#include "CalibrationImageValidityTask.h"
#include "CameraCalibrationThread.h"


CalibrationWindow::CalibrationWindow(QWidget *parent) : QWidget(parent)
{
	ui.setupUi(this);

	leftCamView = findChild<QGraphicsView*>("LeftCamera");
	rightCamView = findChild<QGraphicsView*>("RightCamera");
	pairLayout = findChild<QLayout*>("CameraPairSelection");
	imageSets = findChild<QListView*>("imageSets");

	model = new CalibrationListModel();
	imageSets->setModel(model);
	connect(imageSets, &QListView::activated, this, &CalibrationWindow::selctionChanged);

	spacer = new QSpacerItem(40, 20, QSizePolicy::Expanding);
	pairLayout->addItem(spacer);

	leftCam = new QGraphicsScene();
	leftCamView->setScene(leftCam);

	rightCam = new QGraphicsScene();
	rightCamView->setScene(rightCam);

	pairSummary = findChild<QTableView*>("pairSummary");
	pairModel = new QStandardItemModel();
	pairSummary->setModel(pairModel);

	//setup auto scaling
	QSplitter* imageSplitter = findChild<QSplitter*>("imageSplitter");
	connect(imageSplitter, &QSplitter::splitterMoved, this, &CalibrationWindow::splitterChanged);
	QSplitter* main = findChild<QSplitter*>("mainSplitter");
	connect(main, &QSplitter::splitterMoved, this, &CalibrationWindow::splitterChanged);

	summarySplitter = findChild<QSplitter*>("keySplitter");

	configureButton = findChild<QPushButton*>("genButton");
	connect(configureButton, &QPushButton::pressed, this, &CalibrationWindow::startConfigGeneration);
}


CalibrationWindow::~CalibrationWindow()
{
	delete model;
	delete spacer;
	delete leftCam;
	delete rightCam;

	for (int i = 0; i < buttons->size(); ++i)
		delete buttons->at(i);
	buttons->clear();
	delete buttons;

	cameras->clear();
	delete cameras;

	finishThread->quit();
	finishThread->quit();
}

//clear the existing imagesets and load a new project
void CalibrationWindow::projectSelected(QString project)
{
	projectPath = project;
	QString data = getProjectJsonString();
	configureButton->setEnabled(false);
	confBtnEnable = false;

	if (model->rowCount() > 0) model->clearData();
	for (int i = 0; i < cameras->size(); ++i)
	{
		cameras->at(i).workingCount = 0;

		QStandardItem* qty = new QStandardItem(QString::number(0));
		pairModel->setItem(i, 1, qty);
	}

	json json = json::parse(data.toStdString().c_str());
	for (int i = 0; i < json["ImageSets"].size(); ++i) {
		CalibrationSet* newSet = generateCalibrationSet(json["ImageSets"][i]);

		if (QDir().exists(projectPath + newSet->name) + "/") generateCalibrationTasks(newSet);
		model->addItem(newSet);
	}
}

//add imagesets to the existing sets
void CalibrationWindow::updateProject()
{
	QString data = getProjectJsonString();
	json json = json::parse(data.toStdString().c_str());

	if (json["ImageSets"].size() <= model->rowCount()) return;

	for (int i = 0; i < json["ImageSets"].size(); ++i) {
		nlohmann::json imageSetJson = json["ImageSets"][i];

		int setId = imageSetJson["id"];
		if (model->containsSet(setId)) continue;
		//todo check if the amount of images match and add any new ones if they do (can happen if scanner is queried while taking a new image)

		CalibrationSet* newSet = generateCalibrationSet(json["ImageSets"][i]);

		generateCalibrationTasks(newSet);
		model->addItem(newSet);
	}
}

void CalibrationWindow::scannerConnected()
{
	emit connection->requestScanner(ScannerCommands::CameraPairs, "", this);
}

void CalibrationWindow::scannerDisconnected()
{
	cameras->clear();
	model->clearData();

	activeSet = nullptr;
	activePair = -1;

	for (int i = 0; i < buttons->size(); ++i)
	{
		pairLayout->removeWidget(buttons->at(i));
		delete buttons->at(i);
	}
	buttons->clear();
}

void CalibrationWindow::selctionChanged(QModelIndex index)
{
	activeSet = model->getRow(index.row());

	calculateButtonStates();

	//try to find an image pair to display
	activePair = -1;
	for (int i = 0; i < buttons->size(); ++i)
	{
		if (buttons->at(i)->isEnabled())
		{
			activePair = i;
			break;
		}
	}

	updateCameraImages();
}

void CalibrationWindow::pairChange(const int& id)
{
	activePair = id;
	updateCameraImages();
}

void CalibrationWindow::splitterChanged(int pos, int index)
{
	resizePreviews();
}

void CalibrationWindow::startConfigGeneration()
{
	if (finishThread->isRunning()) return;

	CameraCalibrationThread* work = new CameraCalibrationThread(projectPath, rawPairs, connection);
	work->moveToThread(finishThread);

	connect(work, &CameraCalibrationThread::complete, this, &CalibrationWindow::configGenComplete);
	connect(finishThread, &QThread::started, work, &CameraCalibrationThread::start);

	finishThread->start();
	//emit work->start();

	configureButton->setText("Working");
	configureButton->setEnabled(false);
}

void CalibrationWindow::configGenComplete()
{
	configureButton->setText("Configure");
	configureButton->setEnabled(confBtnEnable);
	finishThread->quit();
}

void CalibrationWindow::newImageTransfered(int setId, int imageId)
{
	CalibrationSet* set = model->getSet(setId);
	if (set == nullptr) return;

	for (int i = 0; i < set->images->size(); ++i)
	{
		if (set->images->at(i)->cameraId == imageId)
		{
			if (QFile().exists(projectPath + "/" + set->name + "/" + set->images->at(i)->fileName))
				set->images->at(i)->valid = Pending;
			break;
		}
	}

	//check if new images complete a pair
	checkImagePairs(set);
	generateCalibrationTasks(set);

	//rescan the buttons and enable any new pairs that now (potentially) exist
	if (activeSet != nullptr && activeSet->setId == setId)
		calculateButtonStates();
}

void CalibrationWindow::windowOpened()
{
	enabled = true;
	if(cameras->size() > 0) resizeControlSplitter();

	if (model->rowCount() > 0)
	{
		for (int i = 0; i < model->rowCount(); ++i)
		{
			CalibrationSet* set = model->getRow(i);
			generateCalibrationTasks(set);
		}
	}
}

void CalibrationWindow::respondToScanner(ScannerCommands command, QByteArray data)
{
	switch (command)
	{
	case ScannerCommands::CameraPairs:
		processCameraPairs(data);
		rawPairs = data;
		break;
	}
}

QString CalibrationWindow::getProjectJsonString()
{
	QFile text(projectPath + "/" + "project.scan");
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

void CalibrationWindow::processCameraPairs(QByteArray data)
{
	QString jsonText = data;
	if (jsonText.startsWith("Fail")) return;

	if (cameras->size() > 0) cameras->clear();
	if (pairModel->rowCount() > 0) pairModel->clear();

	json pairs = json::parse(jsonText.toStdString().c_str());
	for (int i = 0; i < pairs.size(); ++i)
	{
		CameraPair pair = CameraPair();
		pair.leftId = pairs.at(i)["LeftCamera"];
		pair.rightId = pairs.at(i)["RightCamera"];
		pair.id = pairs.at(i)["pairId"];

		cameras->push_back(pair);

		QStandardItem* item = new QStandardItem(QString::number(pair.leftId) + " - " + QString::number(pair.rightId));
		pairModel->setItem(i, 0, item);
		QStandardItem* qty = new QStandardItem(QString::number(0));
		pairModel->setItem(i, 1, qty);
	}

	resizeControlSplitter();

	//button setup
	pairLayout->removeItem(spacer);

	for (int i = 0; i < cameras->size(); ++i)
	{
		CameraPair pair = cameras->at(i);
		TagPushButton* newButton;
		if (buttons->size() < i) newButton = buttons->at(i);
		else {
			newButton = new TagPushButton(this);
			newButton->setMinimumWidth(30);
			newButton->setPalette(calibrationPending);
			if (activeSet == nullptr || activePair == -1) newButton->setEnabled(false);

			pairLayout->addWidget(newButton);
			buttons->push_back(newButton);
		}

		QString text = QString::number(pair.leftId) + " - " + QString::number(pair.rightId);
		newButton->setText(text);
		newButton->setTag(i);

		connect(newButton, &TagPushButton::triggered, this, &CalibrationWindow::pairChange);
	}

	//remove all unused buttons
	while (buttons->size() > cameras->size())
	{
		TagPushButton* remove = buttons->back();
		pairLayout->removeWidget(remove);
		delete remove;
		buttons->erase(buttons->end());
	}

	pairLayout->addItem(spacer);
}

void CalibrationWindow::updateCameraImages()
{
	if (activePair == -1 || activePair > cameras->size())
	{
		leftCam->clear();
		leftCam->addText("No image pair in set");

		rightCam->clear();
		rightCam->addText("No image pair in set");

		resizePreviews();
		return;
	}

	QString leftName = "", rightName = "";

	for (int j = 0; j < activeSet->images->size(); ++j)
	{
		if (activeSet->images->at(j)->cameraId == cameras->at(activePair).leftId)
			leftName = activeSet->images->at(j)->fileName;
		else if (activeSet->images->at(j)->cameraId == cameras->at(activePair).rightId)
			rightName = activeSet->images->at(j)->fileName;

		if (leftName != "" && rightName != "") break;
	}

	if (leftCam->items().size() > 0) leftCam->clear();
	if (QFile().exists(projectPath + "/" + activeSet->name + "/calibration/" + leftName))
		leftCam->addItem(new QGraphicsPixmapItem(QPixmap::fromImage(QImage(projectPath + "/" + activeSet->name + "/calibration/" + leftName))));
	else leftCam->addItem(new QGraphicsPixmapItem(QPixmap::fromImage(QImage(projectPath + "/" + activeSet->name + "/" + leftName))));

	if (rightCam->items().size() > 0) rightCam->clear();
	if (QFile().exists(projectPath + "/" + activeSet->name + "/calibration/" + rightName))
		rightCam->addItem(new QGraphicsPixmapItem(QPixmap::fromImage(QImage(projectPath + "/" + activeSet->name + "/calibration/" + rightName))));
	else rightCam->addItem(new QGraphicsPixmapItem(QPixmap::fromImage(QImage(projectPath + "/" + activeSet->name + "/" + rightName))));

	resizePreviews();
}

void CalibrationWindow::calculateButtonStates()
{
	for (int i = 0; i < activeSet->pairs->size(); ++i)
	{
		if (activeSet->pairs->at(i) == CalibrationValidity::Missing)
			buttons->at(i)->setEnabled(false);
		else if (activeSet->pairs->at(i) == CalibrationValidity::Uncaptured)
			buttons->at(i)->setEnabled(false);
		else buttons->at(i)->setEnabled(true);
	}
}

void CalibrationWindow::resizePreviews() const
{
	leftCamView->fitInView(leftCam->itemsBoundingRect(), Qt::KeepAspectRatio);
	leftCamView->show();

	rightCamView->fitInView(rightCam->itemsBoundingRect(), Qt::KeepAspectRatio);
	rightCamView->show();
}

void CalibrationWindow::resizeEvent(QResizeEvent* event)
{
	resizePreviews();
}

QString CalibrationWindow::getImageName(CalibrationSet* search, int camId) const
{
	for (int i = 0; i < search->images->size(); ++i)
	{
		if (search->images->at(i)->cameraId == camId)
			return search->images->at(i)->fileName;
	}

	return "";
}

void CalibrationWindow::resizeControlSplitter()
{
	int idealSize = pairModel->rowCount() * pairSummary->rowHeight(0);
	QList<int> size = summarySplitter->sizes();
	QList<int> newSize = QList<int>();

	int total = 0;
	for (int i = 0; i < size.size(); ++i)
		total += size.at(i);

	if (total > 0) {
		int oldProp = total - size.back();
		int newProp = total - idealSize;

		for (int i = 0; i < size.size() - 1; ++i)
		{
			float proportians = size.at(i) / oldProp;
			newSize.append(newProp * proportians);
		}

		newSize.append(idealSize);
		summarySplitter->setSizes(newSize);
	}
}

void CalibrationWindow::generateCalibrationTasks(CalibrationSet* set) const
{
	if (!enabled) return;
	QString basePath = projectPath + "/" + set->name + "/";
	QString savePath = basePath + "calibration/";

	if (!QDir().exists(savePath)) QDir().mkdir(savePath);

	for (int i = 0; i < set->images->size(); ++i)
	{
		if (set->images->at(i)->valid == Pending)
		{
			QString left = set->images->at(i)->fileName;

			CalibrationImageValidityTask* task = new CalibrationImageValidityTask(basePath + left, savePath, left, set->setId, set->images->at(i)->cameraId);
			connect(task, &CalibrationImageValidityTask::complete, this, &CalibrationWindow::imageTaskComplete);
			connect(task, &CalibrationImageValidityTask::failed, this, &CalibrationWindow::imageTaskFailed);
			workQueue->start(task);
		}
	}
}

void CalibrationWindow::imageTaskComplete(int set, int img)
{
	CalibrationSet* activeSet = model->getSet(set);
	for (int i = 0; i < activeSet->images->size(); ++i)
	{
		if (activeSet->images->at(i)->cameraId == img)
		{
			activeSet->images->at(i)->valid = Valid;
			break;
		}
	}

	checkImagePairs(activeSet);
	configureButton->setEnabled(true);
	confBtnEnable = true;
}

void CalibrationWindow::imageTaskFailed(int set, int img)
{
	CalibrationSet* activeSet = model->getSet(set);
	for (int i = 0; i < activeSet->images->size(); ++i)
	{
		if (activeSet->images->at(i)->cameraId == img)
		{
			activeSet->images->at(i)->valid = Invalid;
			break;
		}
	}

	checkImagePairs(activeSet);
}

CalibrationSet* CalibrationWindow::generateCalibrationSet(nlohmann::json json) const
{
	CalibrationSet* newSet = new CalibrationSet();
	newSet->setId = json["id"];
	newSet->name = QString::fromStdString(json["path"]);
	for (int i = 0; i < cameras->size(); ++i) newSet->pairs->push_back(Pending);

	QString path = projectPath + "/" + newSet->name + "/";

	for (int j = 0; j < json["images"].size(); ++j)
	{
		CalibrationImage* img = new CalibrationImage();
		img->fileName = QString::fromStdString(json["images"][j]["path"]);
		img->cameraId = json["images"][j]["id"];
		if (!QFile().exists(path + img->fileName)) img->valid = Missing;

		newSet->images->append(img);
	}

	checkImagePairs(newSet);

	return newSet;
}

void CalibrationWindow::checkImagePairs(CalibrationSet* set) const
{
	QString imagePath = projectPath + "/" + set->name + "/";
	for (int i = 0; i < cameras->size(); ++i)
	{
		CalibrationImage *leftName = nullptr, *rightName = nullptr;

		for (int j = 0; j < set->images->size(); ++j)
		{
			if (set->images->at(j)->cameraId == cameras->at(i).leftId)
				leftName = set->images->at(j);
			else if (set->images->at(j)->cameraId == cameras->at(i).rightId)
				rightName = set->images->at(j);

			if (leftName != nullptr && rightName != nullptr) break;
		}

		if (leftName == nullptr || rightName == nullptr)
		{
			set->pairs->at(i) = CalibrationValidity::Uncaptured;
			return;
		}

		if (!QFile::exists(imagePath + leftName->fileName))
			set->pairs->at(i) = CalibrationValidity::Missing;
		else if (!QFile::exists(imagePath + rightName->fileName))
			set->pairs->at(i) = CalibrationValidity::Missing;
		else
		{
			if (leftName->valid == Invalid || rightName->valid == Invalid)
			{
				set->pairs->at(i) = Invalid;
				continue;
			}
			else if (leftName->valid == Valid && rightName->valid == Valid)
			{
				set->pairs->at(i) = Valid;
				cameras->at(i).workingCount++;

				QStandardItem* qty = new QStandardItem(QString::number(cameras->at(i).workingCount));
				pairModel->setItem(i, 1, qty);

				continue;
			}

			set->pairs->at(i) = CalibrationValidity::Pending;
		}
	}
}
