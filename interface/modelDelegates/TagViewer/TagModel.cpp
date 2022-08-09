//
// Created by kj16609 on 7/13/22.
//

#include "TagModel.hpp"
#include "TagData.hpp"


#include "TracyBox.hpp"


//Should only be called during invalidate
void TagModel::reset()
{
	std::lock_guard< std::mutex > lock( this->mtx );


	beginResetModel();
	tags.clear();
	endResetModel();
}


void TagModel::setTags( const std::vector< TagData >& tag_ids )
{
	ZoneScoped;

	std::lock_guard< std::mutex > lock( this->mtx );

	beginResetModel();

	tags = tag_ids;

	endResetModel();

}


TagModel::TagModel( [[maybe_unused]]QWidget* parent )
{
}


int TagModel::rowCount( [[maybe_unused]] const QModelIndex& parent ) const
{
	return tags.size();
}


int TagModel::columnCount( [[maybe_unused]] const QModelIndex& parent ) const
{
	return 1;
}


QVariant TagModel::data( const QModelIndex& index, [[maybe_unused]] int role ) const
{
	return QVariant::fromValue( tags.at( index.row() ) );
}