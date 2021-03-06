#include "ScannerInspectionTool.h"
#include "ScannerResponseListener.h"
#include <QMessageBox>
#include <QLabel>
#include <QGraphicsItem>
#include "ScannerInteraction.h"
#include "parameterBuilder.h"
#include "Lib/json.hpp"
#include "ProjectView.h"


ScannerInspectionTool::ScannerInspectionTool(QWidget *parent)
	: QMainWindow(parent)
{
	ui.setupUi(this);

	deviceList = findChild<QListView*>("deviceList");
	deviceList->setSelectionMode(QAbstractItemView::SingleSelection);
	deviceScanBtn = findChild<QPushButton*>("deviceScanBtn");
	deviceConnectBtn = findChild<QPushButton*>("deviceConnectBtn");
	currentLbl = findChild<QLabel*>("currentLbl");
	imgPreview = findChild<QGraphicsView*>("deviceImagePreview");
	broadcastSocket = new QUdpSocket(this);

	nameText = findChild<QLineEdit*>("nameText");
	nameBtn = findChild<QPushButton*>("nameUpdateBtn");

	scannerItems = new QStringList();
	deviceList->setModel(new QStringListModel(*scannerItems));

	logRefresh = findChild<QPushButton*>("deviceLogsBtn");
	logData = new QStringList();
	logView = findChild<QListView*>("deviceLogs");
	logView->setSelectionMode(QAbstractItemView::NoSelection);
	logView->setModel(new QStringListModel(*logData));

	scene = new QGraphicsScene;
	scene->addText("No Image Selected");
	imgPreview->setScene(scene);
	refreshImagePreview();

	timer = new QTimer(this);
	timer->start(30000);

	connect(timer, &QTimer::timeout, this, &ScannerInspectionTool::refreshDevices);
	connect(deviceList, &QListView::clicked, this, &ScannerInspectionTool::selectionChanged);
	connect(deviceList, &QListView::doubleClicked, this, &ScannerInspectionTool::handleConnectionBtn);
	connect(deviceConnectBtn, &QPushButton::released, this, &ScannerInspectionTool::handleConnectionBtn);
	connect(deviceScanBtn, &QPushButton::released, this, &ScannerInspectionTool::refreshDevices);
	connect(nameBtn, &QPushButton::released, this, &ScannerInspectionTool::changeScannerName);
	connect(logRefresh, SIGNAL(released()), this, SLOT(refreshLogs()));

	QSplitter* top = findChild<QSplitter*>("topSplitter");
	connect(top, &QSplitter::splitterMoved, this, &ScannerInspectionTool::splitterChanged);
	QSplitter* left = findChild<QSplitter*>("leftSplitter");
	connect(left, &QSplitter::splitterMoved, this, &ScannerInspectionTool::splitterChanged);

	setupBroadcastListener();
	setupProjectView();
	setupProjectTransfer();

	//setup the direct interaction window and hide it in the background
	directWn = new DirectInteractionWindow();
	directWn->setConnection(connector);
	DirectInteractionBtn = findChild<QAction*>("actionDirect_Interaction_Window");
	connect(DirectInteractionBtn, &QAction::triggered, this, &ScannerInspectionTool::openDirectInteraction);

	//setup the calibration window
	calibWn = new CalibrationWindow();
	calibWn->setConnection(connector);
	CalibrationBtn = findChild<QAction*>("actionCalibration_Tool");
	connect(CalibrationBtn, &QAction::triggered, this, &ScannerInspectionTool::openCalibration);
	connect(transfer, &projectTransfer::projectUpdated, calibWn, &CalibrationWindow::updateProject);
	connect(transfer, &projectTransfer::projectChanged, calibWn, &CalibrationWindow::projectSelected);
	connect(transfer, &projectTransfer::imageTransfered, calibWn, &CalibrationWindow::newImageTransfered);
	connect(connector, &ScannerInteraction::scannerConnected, calibWn, &CalibrationWindow::scannerConnected);
	connect(connector, &ScannerInteraction::scannerConnectionLost, calibWn, &CalibrationWindow::scannerDisconnected);

	emit refreshDevices();
}

ScannerInspectionTool::~ScannerInspectionTool()
{
	delete broadcastSocket;
	delete timer;
	delete logData;

	clearScanners();
	delete scannerItems;

	//remove threaded items
	listenerThread->quit();
	listenerThread->wait();
	delete listenerThread;
	//delete listenSocket;
	//delete connector;
}

void ScannerInspectionTool::refreshDevices()
{
	//clear out the existing data
	clearScanners();
	scannerItems->clear();
	deviceList->model()->removeRows(0, deviceList->model()->rowCount());

	broadcastSocket->writeDatagram(datagram.data(), datagram.size(), QHostAddress::Broadcast, brdPort);
}

void ScannerInspectionTool::selectionChanged() const
{
	if (!connected)
	{
		deviceConnectBtn->setDisabled(false);
	}
}

void ScannerInspectionTool::setImagePreview(QString path) const
{
	scene->clear();
	scene->addItem(new QGraphicsPixmapItem(QPixmap::fromImage(QImage(path))));

	refreshImagePreview();
}

void ScannerInspectionTool::handleConnectionBtn()
{
	if (connected)
	{
		//disconnect, show confirmation message
		QMessageBox msgBox;
		msgBox.setWindowTitle("Are you sure?");
		msgBox.setIcon(QMessageBox::Question);
		msgBox.setText("Are you sure you want to disconnect?");
		msgBox.setStandardButtons(QMessageBox::No | QMessageBox::Yes);
		msgBox.setDefaultButton(QMessageBox::Yes);
		int ret = msgBox.exec();

		switch (ret) {
		case QMessageBox::No:
			return;
		case QMessageBox::Yes:
			disconnectFromScanner();
			break;
		default:
			//do nothing 
			break;
		}
	}
	else
	{
		connectToScanner();
	}
}

void ScannerInspectionTool::connectToScanner()
{
	QModelIndex current_index = deviceList->selectionModel()->currentIndex();
	QString scannerName = current_index.data(Qt::DisplayRole).toString();

	//find the network address for this device
	ScannerDeviceInformation* deviceInformation = nullptr;
	std::list<ScannerDeviceInformation*>::iterator it;
	for (it = scanners.begin(); it != scanners.end(); ++it) {
		if ((*it)->name == scannerName) {
			deviceInformation = *it;
			break;
		}
	}

	if (deviceInformation == nullptr) return;

	emit connector->connectToScanner(deviceInformation);
	nameText->setText(deviceInformation->name);
}

void ScannerInspectionTool::disconnectFromScanner()
{
	emit connector->disconnect();
}

void ScannerInspectionTool::scannerConnected()
{
	deviceConnectBtn->setText("Disconnect");
	currentLbl->setText("Connected");
	currentLbl->setStyleSheet("color: rgb(0, 128, 0);");

	connected = true;
	refreshLogs(false);
}

void ScannerInspectionTool::scannerDisconnected()
{
	deviceConnectBtn->setText("Connect");
	currentLbl->setText("Disconnected");
	currentLbl->setStyleSheet("color: rgb(237, 20, 61);");

	connected = false;
}

void ScannerInspectionTool::changeScannerName()
{
	if (!connected) return;

	emit connector->requestScanner(ScannerCommands::setName,
		parameterBuilder().addParam("name", nameText->text())->toString(), this);

	clearScanners();
	emit refreshDevices();
}

void ScannerInspectionTool::refreshLogs()
{
	if (!connected) return;

	refreshLogs(true);
}

void ScannerInspectionTool::refreshLogs(bool partial)
{
	connector->requestScanner(partial ? ScannerCommands::getRecentLogDiff : ScannerCommands::getRecentLogFile, "", this);
}

void ScannerInspectionTool::refreshImagePreview() const
{
	imgPreview->fitInView(scene->itemsBoundingRect(), Qt::KeepAspectRatio);
	imgPreview->show();
}

void ScannerInspectionTool::respondToScanner(ScannerCommands command, QByteArray data)
{
	switch (command)
	{
	case ScannerCommands::getRecentLogFile:
	case ScannerCommands::getRecentLogDiff:
		updateLogView(data);
		break;
	default:
		return;
	}
}

void ScannerInspectionTool::openDirectInteraction()
{
	if (!directWn->isVisible()) directWn->show();
}

void ScannerInspectionTool::openCalibration()
{
	if (!calibWn->isVisible()) {
		calibWn->show();
		emit calibWn->windowOpened();
	}
}

void ScannerInspectionTool::splitterChanged(int pos, int index)
{
	refreshImagePreview();
}

void ScannerInspectionTool::resizeEvent(QResizeEvent* event)
{
	QMainWindow::resizeEvent(event);

	refreshImagePreview();
}

void ScannerInspectionTool::addNewScanner(ScannerDeviceInformation* scanner)
{
	//check if the scanner is already tracked in the list
	bool exists = false;

	std::list<ScannerDeviceInformation*>::iterator it;
	for (it = scanners.begin(); it != scanners.end(); ++it) {
		if ((*it)->address.isEqual(scanner->address)) {
			exists = true;
			break;
		}
	}

	if (exists)
	{
		//scanner already exists
		delete scanner;
		return;
	}

	scanners.push_back(scanner);
	scannerItems->append(scanner->name);
	static_cast<QStringListModel*>(deviceList->model())->setStringList(*scannerItems);
}

void ScannerInspectionTool::setupBroadcastListener()
{
	listenerThread = new QThread(this);
	listener = new ScannerResponseListener();
	listenerThread->setObjectName("Listener Thread");
	listener->moveToThread(listenerThread);
	connector = new ScannerInteraction();
	connector->moveToThread(listenerThread);

	connect(listenerThread, &QThread::started, listener, &ScannerResponseListener::startProcessing);
	connect(listenerThread, &QThread::finished, listener, &QObject::deleteLater);
	connect(listener, &ScannerResponseListener::newScannerFound, this, &ScannerInspectionTool::addNewScanner);
	connect(connector, &ScannerInteraction::scannerConnected, this, &ScannerInspectionTool::scannerConnected);
	connect(connector, &ScannerInteraction::scannerConnectionLost, this, &ScannerInspectionTool::scannerDisconnected);
	//connect(connector, &ScannerInteraction::scannerResult, this, &ScannerInspectionTool::respondToScanner);

	listenerThread->start();
}

void ScannerInspectionTool::setupProjectView()
{
	QPushButton* trans = findChild<QPushButton*>("transferBtn");
	QPushButton* refresh = findChild<QPushButton*>("projectRefresh");
	QTableView* table = findChild<QTableView*>("projects");

	projects = new ProjectView(refresh, trans, table, connector);
}

void ScannerInspectionTool::setupProjectTransfer()
{
	QLineEdit* path = findChild<QLineEdit*>("savePath");
	QPushButton* pause = findChild<QPushButton*>("pausePlay");
	QTreeView* progress = findChild<QTreeView*>("progress");

	transfer = new projectTransfer(path, pause, progress, connector);

	connect(projects, &ProjectView::transferProject, transfer, &projectTransfer::changeTargetProject);
	connect(transfer, &projectTransfer::triggerImagePreview, this, &ScannerInspectionTool::setImagePreview);
}

void ScannerInspectionTool::clearScanners()
{
	for (int i = scanners.size() - 1; i >= 0; --i) {
		ScannerDeviceInformation* remove = scanners.back();
		scanners.remove(remove);
		delete remove;
	}

	scannerItems->clear();
}

void ScannerInspectionTool::updateLogView(QByteArray data)
{
	nlohmann::json j = nlohmann::json::parse(data.toStdString().c_str());

	for (int i = 0; i < j.size(); ++i)
	{
		std::string normal = j[i];
		QString* logString = new QString(normal.c_str());
		logData->append(*logString);
	}

	static_cast<QStringListModel*>(logView->model())->setStringList(*logData);
	logView->scrollToBottom();
}
