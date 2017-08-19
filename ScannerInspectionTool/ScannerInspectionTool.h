#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_ScannerInspectionTool.h"

class ScannerInspectionTool : public QMainWindow
{
	Q_OBJECT

public:
	ScannerInspectionTool(QWidget *parent = Q_NULLPTR);

private:
	Ui::ScannerInspectionToolClass ui;
};
