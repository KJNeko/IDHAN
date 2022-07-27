//
// Created by kj16609 on 7/26/22.
//

#ifndef IDHAN_NEWVIEWDIALOG_HPP
#define IDHAN_NEWVIEWDIALOG_HPP


#include <QDialog>


QT_BEGIN_NAMESPACE
namespace Ui
{
	class NewViewDialog;
}
QT_END_NAMESPACE

class NewViewDialog : public QDialog
{
Q_OBJECT

public:
	explicit NewViewDialog( QWidget* parent = nullptr );

	~NewViewDialog() override;

private:
	Ui::NewViewDialog* ui;

private slots:

	void on_buttonBox_clicked();

	void on_buttonBox_rejected();
};


#endif //IDHAN_NEWVIEWDIALOG_HPP
