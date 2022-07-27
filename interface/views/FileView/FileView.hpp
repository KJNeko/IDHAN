//
// Created by kj16609 on 7/26/22.
//

#ifndef IDHAN_FILEVIEW_HPP
#define IDHAN_FILEVIEW_HPP


#include <QWidget>

#include "modules/ListViewModule/ListViewModule.hpp"
#include "modules/TagViewModule/TagViewModule.hpp"
#include "modules/TagSearchModule/TagSearchModule.hpp"


QT_BEGIN_NAMESPACE
namespace Ui
{
	class FileView;
}
QT_END_NAMESPACE

class FileView : public QWidget
{
Q_OBJECT

public:
	explicit FileView( QWidget* parent = nullptr );

	~FileView() override;

private:
	Ui::FileView* ui;

	ListViewModule* viewport { nullptr };
	TagViewModule* tagport { nullptr };
	TagSearchModule* tagsearch { nullptr };
};


#endif //IDHAN_FILEVIEW_HPP
