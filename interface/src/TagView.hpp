//
// Created by kj16609 on 7/9/22.
//

#ifndef IDHAN_TAGVIEW_HPP
#define IDHAN_TAGVIEW_HPP


#include <QWidget>

#include "database/tags.hpp"


QT_BEGIN_NAMESPACE
namespace Ui
{
	class TagView;
}
QT_END_NAMESPACE

class TagView : public QWidget
{
Q_OBJECT


public:
	explicit TagView( QWidget* parent = nullptr );

	~TagView() override;

	void setTags( const std::vector< Tag >& tags );

private:
	Ui::TagView* ui;

public slots:

	void selectionChanged( const std::vector< uint64_t >& hash_ids );
};


#endif //IDHAN_TAGVIEW_HPP
