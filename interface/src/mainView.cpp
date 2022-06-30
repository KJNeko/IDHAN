//
// Created by kj16609 on 6/16/22.
//

// You may need to build the project (run Qt uic code generator) to get
// "ui_mainView.h" resolved

#include "mainView.hpp"
#include "ui_mainView.h"

// ImportWindow include
#include "importViewer.hpp"
#include "importWindow.hpp"

MainWindow::MainWindow( QWidget* parent )
	: QMainWindow( parent ),
	  ui( new Ui::MainWindow )
{
	ui->setupUi( this );
}

MainWindow::~MainWindow()
{
	delete ui;
}

void MainWindow::addTab( QWidget* widget )
{
	ui->tabWidget->addTab( widget, widget->windowTitle() );
}

void MainWindow::on_actionImport_triggered()
{
	// Open import window
	ImportWindow window( this );
	window.exec();
}

void MainWindow::importFiles( const std::vector<std::pair<std::string, std::string>>& files )
{
	// Open import viewer in a new page
	ImportViewer* viewer = new ImportViewer( this );

	// Create a new tag and place the viewer into it
	ui->tabWidget->addTab( viewer, "Import" );
	viewer->addFiles( files );
}
