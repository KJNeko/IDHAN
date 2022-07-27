//
// Created by kj16609 on 7/26/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_FileView.h" resolved

#include "FileView.hpp"
#include "ui_FileView.h"


FileView::FileView( QWidget* parent ) : QWidget( parent ), ui( new Ui::FileView )
{
	ui->setupUi( this );

	viewport = new ListViewModule( this );
	tagport = new TagViewModule( this );
	tagsearch = new TagSearchModule( this );

	ui->viewFrame->layout()->addWidget( viewport );
	ui->tagFrame->layout()->addWidget( tagport );
	ui->searchFrame->layout()->addWidget( tagsearch );

	connect( viewport, &ListViewModule::selection, tagport, &TagViewModule::selectionChanged );
}


FileView::~FileView()
{
	delete ui;

	delete viewport;
	delete tagport;
	delete tagsearch;
}
