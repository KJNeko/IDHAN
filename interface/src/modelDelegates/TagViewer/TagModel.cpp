//
// Created by kj16609 on 7/13/22.
//

#include "TagModel.hpp"
#include "DataPack.hpp"


#include "TracyBox.hpp"


//Should only be called during invalidate
void TagModel::reset()
{
	std::lock_guard< std::mutex > lock( this->mtx );

	ZoneScoped;

	beginResetModel();
	database_ret = pqxx::result();
	endResetModel();
}


void TagModel::setFiles( const std::vector< uint64_t >& hash_ids )
{
	std::lock_guard< std::mutex > lock( this->mtx );

	ZoneScoped;


	beginResetModel();

	Connection conn;
	auto work { conn.getWork() };

	constexpr pqxx::zview query_raw {
		"SELECT tag_id, count(tag_id) AS counter FROM mappings WHERE hash_id = ANY($1::bigint[]) GROUP BY tag_id" };

	//Sorting additions
	std::string query_str { query_raw.c_str() };

	constexpr pqxx::zview order_count { " ORDER BY count(tag_id)" };
	constexpr pqxx::zview order_group { " ORDER BY group_name ORDER BY count(tag_id)" };
	constexpr pqxx::zview order_alphabetical { " ORDER BY group_name, subtag" };


	switch ( 0 )
	{
	case 0: //sort by count
		query_str += order_count.c_str();
		break;
	case 1: //Sort by group (alphabetical) then by count for subtags
		query_str += order_group.c_str();
		break;
	}

	//Asc desc?
	constexpr pqxx::zview order_asc { " ASC" };
	constexpr pqxx::zview order_desc( " DESC" );

	switch ( 1 )
	{
	case 0:
		query_str += order_asc.c_str();
		break;
	case 1:
		query_str += order_desc.c_str();
		break;
	}


	database_ret = { work->exec_params( query_str, hash_ids ) };

	endResetModel();
}


TagModel::TagModel( QWidget* parent )
{

}


int TagModel::rowCount( const QModelIndex& parent ) const
{
	return database_ret.size();
}


int TagModel::columnCount( const QModelIndex& parent ) const
{
	return 1;
}


QVariant TagModel::data( const QModelIndex& index, int role ) const
{
	DataPack pack;
	pack.tag_id = database_ret[ index.row() ][ "tag_id" ].as< uint64_t >();
	pack.counter = database_ret[ index.row() ][ "counter" ].as< uint64_t >();

	return QVariant::fromValue( pack );
}