//
// Created by kj16609 on 7/9/22.
//

#ifndef IDHAN_TAGVIEWMODULE_HPP
#define IDHAN_TAGVIEWMODULE_HPP


#include <QWidget>
#include <QStandardItem>
#include <QItemDelegate>

#include "DatabaseModule/tags/tags.hpp"
#include "filedata/FileData.hpp"

#include "modelDelegates/TagViewer/TagModel.hpp"
#include "modelDelegates/TagViewer/TagDelegate.hpp"


QT_BEGIN_NAMESPACE
namespace Ui
{
	class TagViewModule;
}
QT_END_NAMESPACE

class TagViewModule : public QWidget
{
Q_OBJECT


public:
	explicit TagViewModule( QWidget* parent = nullptr );

	~TagViewModule() override;

	TagViewModule( const TagViewModule& ) = delete;

	TagViewModule operator=( const TagViewModule& ) = delete;

private:
	Ui::TagViewModule* ui;

	TagModel* model { new TagModel( this ) };
	TagDelegate* delegate { new TagDelegate( this ) };

public slots:

	//void addFile( const uint64_t hash_id );

	void selectionChanged( const std::vector< uint64_t >& hash_ids );
};


#endif //IDHAN_TAGVIEWMODULE_HPP
