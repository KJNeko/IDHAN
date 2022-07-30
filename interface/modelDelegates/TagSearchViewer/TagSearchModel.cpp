//
// Created by kj16609 on 7/13/22.
//

#include "TagSearchModel.hpp"


#include "TracyBox.hpp"


void TagSearchModel::setTags( const std::vector< uint64_t >& tag_list_ )
{
	std::lock_guard< std::mutex > lock( this->mtx );

	ZoneScoped;


	beginResetModel();
	tag_list = tag_list_;
	endResetModel();
}


int TagSearchModel::rowCount( [[maybe_unused]] const QModelIndex& parent ) const
{
	return tag_list.size();
}


QVariant TagSearchModel::data( const QModelIndex& index, [[maybe_unused]] int role ) const
{
	return QVariant::fromValue( tag_list[ index.row() ] );
}