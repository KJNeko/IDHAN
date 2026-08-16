#pragma once

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wswitch-enum"
#include <pqxx/pqxx>
#pragma GCC diagnostic pop

#include <string_view>

namespace idhan::test
{

//! Inserts the namespace, or returns the id of the row it folded onto.
inline int insertNamespace( pqxx::transaction_base& tx, const std::string_view text )
{
	return tx
	    .exec(
			"INSERT INTO tag_namespaces (namespace_text) VALUES ($1) "
			"ON CONFLICT (namespace_text) DO UPDATE SET namespace_text = EXCLUDED.namespace_text "
			"RETURNING namespace_id",
			pqxx::params { text } )
	    .one_row()[ 0 ]
	    .as< int >();
}

//! Creates a tag along with the namespace row it needs, or returns the id of the tag it folded onto.
inline int insertTag(
	pqxx::transaction_base& tx,
	const std::string_view namespace_text,
	const std::string_view subtag_text )
{
	const auto namespace_id { insertNamespace( tx, namespace_text ) };

	return tx
	    .exec(
			"INSERT INTO tags (namespace_id, subtag_text) VALUES ($1, $2) "
			"ON CONFLICT (namespace_id, subtag_text) DO UPDATE SET subtag_text = EXCLUDED.subtag_text "
			"RETURNING tag_id",
			pqxx::params { namespace_id, subtag_text } )
	    .one_row()[ 0 ]
	    .as< int >();
}

} // namespace idhan::test
