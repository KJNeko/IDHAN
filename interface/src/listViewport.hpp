//
// Created by kj16609 on 7/7/22.
//

#ifndef IDHAN_LISTVIEWPORT_H
#define IDHAN_LISTVIEWPORT_H


#include <QWidget>
#include <QTimer>

#include <queue>

#include "ImageDelegate.hpp"

#include "FileData.hpp"


QT_BEGIN_NAMESPACE
namespace Ui
{
	class ListViewport;
}
QT_END_NAMESPACE

class ListViewport : public QWidget
{
Q_OBJECT

public:
	explicit ListViewport( QWidget* parent = nullptr );

	~ListViewport() override;

private:
	Ui::ListViewport* ui;

	//std::vector< uint64_t > files;

	ImageModel* model { new ImageModel( this ) };
	ImageDelegate* delegate { new ImageDelegate( this ) };

	QTimer updateTimer;
	std::vector< uint64_t > addQueue;
	std::mutex queue_lock;

public slots:

	void addFile( uint64_t );

	void resetFiles();

	void setFiles( const std::vector< uint64_t >& files );

	void processValues();

	void updateTimermsec_slot( uint64_t );

signals:

	void updateTimermsec( uint64_t );

	void setupTimer();

	std::vector< uint64_t > selection();

};


#endif //IDHAN_LISTVIEWPORT_H
