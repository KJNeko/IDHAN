//
// Created by kj16609 on 6/16/22.
//

#ifndef MAIN_MAINVIEW_HPP
#define MAIN_MAINVIEW_HPP

#include <QMainWindow>


QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow( QWidget* parent = nullptr );
	
	~MainWindow() override;

private:
	Ui::MainWindow* ui;
};


#endif //MAIN_MAINVIEW_HPP
