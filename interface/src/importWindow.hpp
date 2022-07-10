//
// Created by kj16609 on 6/16/22.
//

#pragma once
#ifndef MAIN_IMPORTWINDOW_HPP
#define MAIN_IMPORTWINDOW_HPP


#include <QDialog>
#include <QPair>
#include <QVector>

#include <filesystem>


QT_BEGIN_NAMESPACE
namespace Ui
{
	class ImportWindow;
}
QT_END_NAMESPACE

class ImportWindow : public QDialog
{
Q_OBJECT

public:
	explicit ImportWindow( QWidget* parent = nullptr );

	~ImportWindow() override;


	ImportWindow( const ImportWindow& ) = delete;

	ImportWindow operator=( const ImportWindow& ) = delete;


private:
	Ui::ImportWindow* ui;

	std::vector< std::filesystem::path > fileList {};

private slots:

	// Click on "addFolder" button
	void on_addFolder_clicked();

	void on_importNow_clicked();
};


#endif // MAIN_IMPORTWINDOW_HPP
