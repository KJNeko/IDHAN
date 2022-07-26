//
// Created by kj16609 on 6/16/22.
//

#pragma once
#ifndef MAIN_MAINVIEW_HPP
#define MAIN_MAINVIEW_HPP


#include <QMainWindow>

#include <filesystem>


QT_BEGIN_NAMESPACE
namespace Ui
{
	class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
Q_OBJECT

public:
	explicit MainWindow( QWidget* parent = nullptr );

	~MainWindow() override;

	MainWindow( const MainWindow& ) = delete;

	MainWindow operator=( const MainWindow& ) = delete;

private:
	Ui::MainWindow* ui;

public:
	void addTab( QWidget* widget );

	void importFiles( const std::vector< std::filesystem::path >& files );

private slots:

	void on_actionImport_triggered();
};


#endif // MAIN_MAINVIEW_HPP
