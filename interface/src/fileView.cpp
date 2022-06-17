//
// Created by kj16609 on 6/16/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_FileView.h" resolved

#include "fileView.hpp"
#include "../ui/ui_fileViewer.h"


FileView::FileView( QWidget* parent )
		:
		QWidget( parent ), ui( new Ui::FileView )
{
	ui->setupUi( this );
	
	
	//Get width of the window
	auto width = ui->topSplitter->width();
	
	//Set the left side to be 20% of the window
	ui->topSplitter->setSizes({static_cast<int>(width * leftPercent), static_cast<int>(width * (1.0 - leftPercent))});
}

FileView::~FileView()
{
	delete ui;
}