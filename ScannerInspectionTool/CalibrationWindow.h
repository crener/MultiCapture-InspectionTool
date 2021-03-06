#pragma once
#include "IDeviceResponder.h"
#include "ui_CalibrationWindow.h"
#include "ScannerInteraction.h"
#include "CalibrationListModel.h"
#include "TagPushButton.h"
#include <qthreadpool.h>
#include "Lib/json.hpp"
#include <QTableView>

QT_BEGIN_NAMESPACE
class QGraphicsView;
class QListView;
class QTableView;
class QLayout;
QT_END_NAMESPACE

class CalibrationWindow : public QWidget, public IDeviceResponder
{
	Q_OBJECT

public:
	CalibrationWindow(QWidget *parent = Q_NULLPTR);
	~CalibrationWindow();

	void setConnection(ScannerInteraction* scanner) { connection = scanner; }

	public slots:
	void projectSelected(QString project);
	void updateProject();
	void scannerConnected();
	void scannerDisconnected();
	void newImageTransfered(int setId, int imageId);
	void windowOpened();

	private slots:
	void selctionChanged(QModelIndex index);
	void pairChange(const int &id);
	void splitterChanged(int pos, int index);
	void startConfigGeneration();
	void configGenComplete();
	void respondToScanner(ScannerCommands, QByteArray) override;

private:
	QString getProjectJsonString();
	void processCameraPairs(QByteArray data);
	void updateCameraImages();
	void calculateButtonStates();
	void resizePreviews() const;
	void resizeEvent(QResizeEvent *event) override;
	QString getImageName(CalibrationSet* search, int camId) const;
	void resizeControlSplitter();

	CalibrationSet* generateCalibrationSet(nlohmann::json json) const;
	void checkImagePairs(CalibrationSet* set) const;
	void generateCalibrationTasks(CalibrationSet* set) const;

	void imageTaskComplete(int, int);
	void imageTaskFailed(int, int);

	CalibrationListModel* model;
	QStandardItemModel* pairModel;
	QSpacerItem* spacer;
	bool enabled = false;
	bool confBtnEnable = false;
	const QPalette calibrationValid = QPalette(QColor(24, 185, 119));
	const QPalette calibrationInvalid = QPalette(QColor(253, 91, 93));
	const QPalette calibrationPending = QPalette();
	QGraphicsScene *leftCam, *rightCam;

	QString projectPath = "";
	std::vector<CameraPair>* cameras = new std::vector<CameraPair>;
	std::vector<TagPushButton*>* buttons = new std::vector<TagPushButton*>;

	CalibrationSet* activeSet = nullptr;
	int activePair = -1;
	QThreadPool* workQueue = new QThreadPool(this);
	QString rawPairs = "";
	QThread* finishThread = new QThread(this);

	Ui::CalibrationWindow ui;
	ScannerInteraction* connection;
	QGraphicsView *leftCamView, *rightCamView;
	QPushButton* configureButton;
	QListView* imageSets;
	QLayout* pairLayout;
	QTableView* pairSummary;
	QSplitter* summarySplitter;
};

