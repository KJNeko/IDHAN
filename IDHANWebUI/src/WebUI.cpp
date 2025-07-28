//
// Created by kj16609 on 7/27/25.
//
// You may need to build the project (run Qt uic code generator) to get "ui_WebUI.h" resolved

#include "WebUI.hpp"

#include "ui_WebUI.h"

namespace idhan
{
WebUI::WebUI( QWidget* parent ) : QMainWindow( parent ), ui( new Ui::WebUI )
{
	ui->setupUi( this );
}

WebUI::~WebUI()
{
	delete ui;
}
} // namespace idhan
