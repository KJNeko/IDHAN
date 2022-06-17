//
// Created by kj16609 on 6/16/22.
//

#ifndef MAIN_IMPORTWINDOW_HPP
#define MAIN_IMPORTWINDOW_HPP

#include <QDialog>


QT_BEGIN_NAMESPACE
namespace Ui { class ImportWindow; }
QT_END_NAMESPACE

class ImportWindow : public QDialog
{
	Q_OBJECT

public:
	explicit ImportWindow( QWidget* parent = nullptr );
	
	~ImportWindow() override;

private:
	Ui::ImportWindow* ui;
	
private slots:
	//Click on "addFolder" button
	void on_addFolder_clicked();
};


#endif //MAIN_IMPORTWINDOW_HPP