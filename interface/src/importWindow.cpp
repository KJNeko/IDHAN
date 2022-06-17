//
// Created by kj16609 on 6/16/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_ImportWindow.h" resolved

#include "importWindow.hpp"
#include "../ui/ui_importWindow.h"

#include <iostream>
#include <QFileDialog>
#include <QStandardItemModel>

#include "utility/fileutils.hpp"
#include "MrMime/mister_mime.hpp"


ImportWindow::ImportWindow( QWidget* parent )
		:
		QDialog( parent ), ui( new Ui::ImportWindow )
{
	ui->setupUi( this );
	
	//Create a new model
	
	
}

ImportWindow::~ImportWindow()
{
	delete ui;
}

void ImportWindow::on_addFolder_clicked()
{
	//Open a file dialog
	QString path = QFileDialog::getExistingDirectory( this, "Select a folder" );
	
	//Get all files in that path
	QDir dir( path );
	QStringList files = dir.entryList( QDir::Files );
	//Set max progress bar
	ui->progressBar->setMaximum( files.size() );
	
	//Create a new model
	QStandardItemModel* model = new QStandardItemModel();
	
	model->setColumnCount(3);
	model->setHorizontalHeaderItem(0, new QStandardItem("Name"));
	model->setHorizontalHeaderItem(1, new QStandardItem("File Type"));
	model->setHorizontalHeaderItem(2, new QStandardItem("Size"));
	
	//Set the model
	ui->fileList->setModel( model );
	
	//Get our current locale
	QLocale locale = this->locale();
	
	//Parse each file and see if it is a compatable mime type
	for( auto& file : files )
	{
		auto [type, size] = idhan::utils::get_mime( QString(path + "/" + file).toStdString() );
		
		//Convert size to human readable format
		auto sizeStr = locale.formattedDataSize(size);
		
		switch(type)
		{
			case MrMime::FileType::IMAGE_JPEG:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "JPEG" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::IMAGE_PNG:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "PNG" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::IMAGE_GIF:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "GIF" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::IMAGE_BMP:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "BMP" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::IMAGE_TIFF:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "TIFF" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::IMAGE_ICON:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "ICON" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::APPLICATION_FLASH:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "FLASH" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::APPLICATION_PDF:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "PDF" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::APPLICATION_ZIP:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "ZIP" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::APPLICATION_RAR:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "RAR" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::APPLICATION_7Z:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "7Z" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::VIDEO_AVI:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "AVI" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::VIDEO_MP4:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "MP4" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::VIDEO_MOV:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "MOV" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::VIDEO_FLV:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "FLV" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::AUDIO_FLAC:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "FLAC" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::AUDIO_WAVE:
				model->appendRow( {new QStandardItem( file ), new QStandardItem( "WAVE" ), new QStandardItem( sizeStr )} );
				break;
			case MrMime::FileType::APPLICATION_UNKNOWN:
				break;
		}
		
		//Set the progress bar
		ui->progressBar->setValue( ui->progressBar->value() + 1 );
	}
	
	
}