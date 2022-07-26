//
// Created by kj16609 on 7/9/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_TagView.h" resolved

#include "TagViewModule.hpp"
#include "ui_TagViewModule.h"

#include <QTreeWidgetItem>
#include <QList>
#include <QStandardItemModel>
#include <QPainter>

#include "filedata/FileData.hpp"


TagViewModule::TagViewModule( QWidget* parent ) : QWidget( parent ), ui( new Ui::TagViewModule )
{
	ui->setupUi( this );

	ui->tagView->setModel( model );
	ui->tagView->setItemDelegate( delegate );

	//Set alignment left
	ui->tagView->setItemAlignment( Qt::AlignLeft );
}


TagViewModule::~TagViewModule()
{
	delete ui;
}


void TagViewModule::selectionChanged( const std::vector< uint64_t >& hash_ids )
{
	ZoneScoped;

	//Clear the model
	//model->reset();

	model->setFiles( hash_ids );
}





