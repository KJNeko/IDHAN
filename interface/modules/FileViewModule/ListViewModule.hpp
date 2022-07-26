//
// Created by kj16609 on 7/7/22.
//

#ifndef IDHAN_LISTVIEWPORT_H
#define IDHAN_LISTVIEWPORT_H


#include <QWidget>
#include <QTimer>

#include <queue>
#include <QItemSelection>

#include "../modelDelegates/ImageViewer/ImageDelegate.hpp"

#include "filedata/FileData.hpp"


QT_BEGIN_NAMESPACE
namespace Ui
{
	class ListViewModule;
}
QT_END_NAMESPACE

class ListViewModule : public QWidget
{
Q_OBJECT

public:
	explicit ListViewModule( QWidget* parent = nullptr );

	~ListViewModule() override;

	ListViewModule( const ListViewModule& ) = delete;

	ListViewModule operator=( const ListViewModule& ) = delete;

private:
	Ui::ListViewModule* ui;

	//std::vector< uint64_t > files;

	ImageModel* model { new ImageModel( this ) };
	ImageDelegate* delegate { new ImageDelegate( this ) };

	std::vector< uint64_t > addQueue {};
	std::mutex queue_lock {};

public slots:

	void addFile( const uint64_t );

	void addFiles( const std::vector< uint64_t >& file_id );

	void resetFiles();

	void setFiles( const std::vector< uint64_t >& files );

	void itemActivated( const QItemSelection& selected, const QItemSelection& deselected );

signals:

	void updateTimermsec( uint64_t );

	void setupTimer();

	void selection( const std::vector< uint64_t >& );
};


#endif //IDHAN_LISTVIEWPORT_H
