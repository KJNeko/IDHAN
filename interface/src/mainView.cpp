//
// Created by kj16609 on 6/16/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_mainView.h" resolved

#include "mainView.hpp"
#include "../ui/ui_mainView.h"


MainWindow::MainWindow( QWidget* parent )
		:
		QMainWindow( parent ), ui( new Ui::MainWindow )
{
	ui->setupUi( this );
}

MainWindow::~MainWindow()
{
	delete ui;
}
