//
// Created by kj16609 on 7/9/22.
//

#ifndef IDHAN_TAGVIEW_HPP
#define IDHAN_TAGVIEW_HPP


#include <QWidget>
#include <QStandardItem>
#include <QItemDelegate>

#include "database/tags/tags.hpp"
#include "filedata/FileData.hpp"

#include "modelDelegates/TagViewer/TagModel.hpp"
#include "modelDelegates/TagViewer/TagDelegate.hpp"


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

	TagView( const TagView& ) = delete;

	TagView operator=( const TagView& ) = delete;

private:
	Ui::TagView* ui;

	TagModel* model { new TagModel( this ) };
	TagDelegate* delegate { new TagDelegate( this ) };

public slots:

	//void addFile( const uint64_t hash_id );

	void selectionChanged( const std::vector< uint64_t >& hash_ids );
};


#endif //IDHAN_TAGVIEW_HPP
