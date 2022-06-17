//
// Created by kj16609 on 6/16/22.
//

#ifndef MAIN_FILEVIEW_HPP
#define MAIN_FILEVIEW_HPP

#include <QWidget>


QT_BEGIN_NAMESPACE
namespace Ui { class FileView; }
QT_END_NAMESPACE

class FileView : public QWidget
{
	Q_OBJECT

public:
	explicit FileView( QWidget* parent = nullptr );
	
	~FileView() override;

private:
	Ui::FileView* ui;
	
	double leftPercent {0.25};
	
};


#endif //MAIN_FILEVIEW_HPP
