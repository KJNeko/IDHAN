#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#include <pqxx/pqxx>
#pragma GCC diagnostic pop

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace idhan::test
{

//! Creates a domain, along with the tag_mappings, tag_aliases and tag_parents partitions it needs.
inline int insertDomain( pqxx::transaction_base& tx, const std::string_view name )
{
	return tx.exec( "INSERT INTO tag_domains (domain_name) VALUES ($1) RETURNING tag_domain_id", pqxx::params { name } )
	    .one_row()[ 0 ]
	    .as< int >();
}

//! The 32 byte hash a record needs, filled from a seed so each record in a test gets a distinct one.
inline std::string hashFor( const int seed )
{
	constexpr std::string_view DIGITS { "0123456789abcdef" };

	std::string hex( 64, '0' );
	for ( std::size_t i = 0; i < 8; ++i )
		hex[ hex.size() - 1 - i ] = DIGITS[ static_cast< unsigned >( seed >> ( i * 4 ) ) & 0xFu ];

	return hex;
}

//! Creates a record and the file_info row without which its mappings never become active.
inline int insertRecord( pqxx::transaction_base& tx, const int seed )
{
	const auto record_id {
		tx.exec(
			  "INSERT INTO records (sha256) VALUES (decode($1, 'hex')) RETURNING record_id",
			  pqxx::params { hashFor( seed ) } )
			.one_row()[ 0 ]
			.as< int >()
	};

	tx.exec(
		"INSERT INTO file_info (record_id, size, mime_id, cluster_delete_time) "
		"VALUES ($1, 0, (SELECT mime_id FROM mime WHERE name = 'image/png'), now())",
		pqxx::params { record_id } );

	return record_id;
}

//! A record with no file_info row, which the mapping triggers are meant to skip.
inline int insertRecordWithoutFile( pqxx::transaction_base& tx, const int seed )
{
	return tx
	    .exec(
			"INSERT INTO records (sha256) VALUES (decode($1, 'hex')) RETURNING record_id",
			pqxx::params { hashFor( seed ) } )
	    .one_row()[ 0 ]
	    .as< int >();
}

inline void insertMapping( pqxx::transaction_base& tx, const int record_id, const int tag_id, const int tag_domain_id )
{
	tx.exec(
		"INSERT INTO tag_mappings (record_id, tag_id, tag_domain_id) VALUES ($1, $2, $3)",
		pqxx::params { record_id, tag_id, tag_domain_id } );
}

inline void removeMapping( pqxx::transaction_base& tx, const int record_id, const int tag_id, const int tag_domain_id )
{
	tx.exec(
		"DELETE FROM tag_mappings WHERE record_id = $1 AND tag_id = $2 AND tag_domain_id = $3",
		pqxx::params { record_id, tag_id, tag_domain_id } );
}

//! Points aliased_id at alias_id, so anything holding the aliased tag should resolve to the alias.
inline void insertAlias( pqxx::transaction_base& tx, const int tag_domain_id, const int aliased_id, const int alias_id )
{
	tx.exec(
		"INSERT INTO tag_aliases (aliased_id, alias_id, tag_domain_id) VALUES ($1, $2, $3)",
		pqxx::params { aliased_id, alias_id, tag_domain_id } );
}

inline void removeAlias( pqxx::transaction_base& tx, const int tag_domain_id, const int aliased_id )
{
	tx.exec(
		"DELETE FROM tag_aliases WHERE aliased_id = $1 AND tag_domain_id = $2",
		pqxx::params { aliased_id, tag_domain_id } );
}

inline void insertParent( pqxx::transaction_base& tx, const int tag_domain_id, const int parent_id, const int child_id )
{
	tx.exec(
		"INSERT INTO tag_parents (parent_id, child_id, tag_domain_id) VALUES ($1, $2, $3)",
		pqxx::params { parent_id, child_id, tag_domain_id } );
}

inline void removeParent( pqxx::transaction_base& tx, const int tag_domain_id, const int parent_id, const int child_id )
{
	tx.exec(
		"DELETE FROM tag_parents WHERE parent_id = $1 AND child_id = $2 AND tag_domain_id = $3",
		pqxx::params { parent_id, child_id, tag_domain_id } );
}

//! Every row the final view holds for the record, duplicates included, since the view is a UNION ALL.
inline std::vector< int > finalTagRows( pqxx::transaction_base& tx, const int record_id, const int tag_domain_id )
{
	std::vector< int > tags {};

	for ( const auto& row : tx.exec(
			  "SELECT tag_id FROM active_tag_mappings_final WHERE record_id = $1 AND tag_domain_id = $2 "
			  "ORDER BY tag_id",
			  pqxx::params { record_id, tag_domain_id } ) )
		tags.emplace_back( row[ 0 ].as< int >() );

	return tags;
}

//! What a reader of the final view sees the record tagged with.
inline std::vector< int > finalTags( pqxx::transaction_base& tx, const int record_id, const int tag_domain_id )
{
	auto tags { finalTagRows( tx, record_id, tag_domain_id ) };
	tags.erase( std::unique( tags.begin(), tags.end() ), tags.end() );
	return tags;
}

//! The ideal the storage layer resolved a raw mapping onto, or nothing when the mapping is not active.
inline std::optional< int > effectiveTag(
	pqxx::transaction_base& tx,
	const int record_id,
	const int tag_id,
	const int tag_domain_id )
{
	const auto result { tx.exec(
		"SELECT COALESCE(ideal_tag_id, tag_id) FROM active_tag_mappings "
		"WHERE record_id = $1 AND tag_id = $2 AND tag_domain_id = $3",
		pqxx::params { record_id, tag_id, tag_domain_id } ) };

	if ( result.empty() ) return std::nullopt;

	return result[ 0 ][ 0 ].as< int >();
}

//! One materialised parent row: the tag it added, the mapping that caused it, and its refcount.
struct ParentRow
{
	int tag_id;
	int origin_id;
	int internal_count;

	bool operator==( const ParentRow& ) const = default;
};

inline std::vector< ParentRow > parentRows( pqxx::transaction_base& tx, const int record_id, const int tag_domain_id )
{
	std::vector< ParentRow > rows {};

	for ( const auto& row : tx.exec(
			  "SELECT tag_id, origin_id, internal_count FROM active_tag_mappings_parents "
			  "WHERE record_id = $1 AND tag_domain_id = $2 ORDER BY tag_id, origin_id",
			  pqxx::params { record_id, tag_domain_id } ) )
		rows.emplace_back( row[ 0 ].as< int >(), row[ 1 ].as< int >(), row[ 2 ].as< int >() );

	return rows;
}

inline std::optional< int > internalCount(
	pqxx::transaction_base& tx,
	const int record_id,
	const int tag_id,
	const int origin_id,
	const int tag_domain_id )
{
	const auto result { tx.exec(
		"SELECT internal_count FROM active_tag_mappings_parents "
		"WHERE record_id = $1 AND tag_id = $2 AND origin_id = $3 AND tag_domain_id = $4",
		pqxx::params { record_id, tag_id, origin_id, tag_domain_id } ) };

	if ( result.empty() ) return std::nullopt;

	return result[ 0 ][ 0 ].as< int >();
}

//! Sorted so an expectation can be written in whatever order reads best.
inline std::vector< int > sorted( std::vector< int > tags )
{
	std::sort( tags.begin(), tags.end() );
	return tags;
}

} // namespace idhan::test
