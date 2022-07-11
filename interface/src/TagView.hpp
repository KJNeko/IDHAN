//
// Created by kj16609 on 7/9/22.
//

#ifndef IDHAN_TAGVIEW_HPP
#define IDHAN_TAGVIEW_HPP


#include <QWidget>
#include <QStandardItem>
#include <QItemDelegate>

#include "database/tags.hpp"
#include "FileData.hpp"


class TagDelegate : public QItemDelegate
{
Q_OBJECT

public:
	TagDelegate( QObject* parent = nullptr );

	void paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const override;

	QSize sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const override;
};

struct TagCounter
{
	uint64_t counter { 0 };
	Tag tag;

	TagCounter() = default;


	TagCounter( Tag tag_, uint64_t counter_ ) : counter( counter_ ), tag( tag_ ) {}


	TagCounter( const TagCounter& other ) : counter( other.counter ), tag( other.tag ) {}


	TagCounter& operator=( const TagCounter& other )
	{
		counter = other.counter;
		tag = other.tag;
		return *this;
	}
};

Q_DECLARE_METATYPE( TagCounter )

class TagModel : public QAbstractListModel
{
Q_OBJECT

public:
	TagModel( QWidget* parent = nullptr );

	int rowCount( const QModelIndex& parent = QModelIndex() ) const override;

	int columnCount( const QModelIndex& parent = QModelIndex() ) const override;

	QVariant data( const QModelIndex& index, int role = Qt::DisplayRole ) const override;
	
	void reset();

	//TODO implement these two if needed
	//void addTag( const uint64_t tag_id );

	//void removeTag( const uint64_t tag_id );

	void setFiles( const std::vector< uint64_t >& hash_ids );

private:

	pqxx::result database_ret;

	std::mutex mtx;
};


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
