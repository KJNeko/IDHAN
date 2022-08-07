//
// Created by kj16609 on 7/13/22.
//

#include "TagSearchModel.hpp"


#include "TracyBox.hpp"


//Should only be called during invalidate
void TagSearchModel::reset()
{
	std::lock_guard< std::mutex > lock( this->mtx );


	beginResetModel();
	tag_list = std::vector< uint64_t >();
	endResetModel();
}


void TagSearchModel::setTags( const std::vector< uint64_t >& tag_list_ )
{
	std::lock_guard< std::mutex > lock( this->mtx );


	beginResetModel();
	tag_list = tag_list_;
	endResetModel();
}


TagSearchModel::TagSearchModel( [[maybe_unused]]QWidget* parent )
{

}


int TagSearchModel::rowCount( [[maybe_unused]] const QModelIndex& parent ) const
{
	return tag_list.size();
}


int TagSearchModel::columnCount( [[maybe_unused]] const QModelIndex& parent ) const
{
	return 1;
}


QVariant TagSearchModel::data( const QModelIndex& index, [[maybe_unused]] int role ) const
{
	return QVariant::fromValue( tag_list[ index.row() ] );
}