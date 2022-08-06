//
// Created by kj16609 on 7/26/22.
//

// You may need to build the project (run Qt uic code generator) to get "ui_NewViewDialog.h" resolved

#include "NewViewDialog.hpp"
#include "ui_NewViewDialog.h"

#include "../IDHANView.hpp"

#include "TracyBox.hpp"

#include "views/FileView/FileView.hpp"


NewViewDialog::NewViewDialog( QWidget* parent ) : QDialog( parent ), ui( new Ui::NewViewDialog )
{
	ui->setupUi( this );
}


NewViewDialog::~NewViewDialog()
{
	delete ui;
}


void NewViewDialog::on_buttonBox_clicked()
{
	auto view_ptr = static_cast<MainWindow*>(this->parent());

	switch ( ui->comboBox->currentIndex() )
	{
	case 0:
		//My Files
		view_ptr->addTab( new FileView( view_ptr ) );
		break;
	default:
		spdlog::error( "Undefined case in NewViewDialog" );
	}

	this->close();
}


void NewViewDialog::on_buttonBox_rejected()
{
	this->close();
}
