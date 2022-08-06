//
// Created by kj16609 on 6/28/22.
//

#pragma once
#ifndef MAIN_MAPPINGS_HPP
#define MAIN_MAPPINGS_HPP


#include <cstdint>
#include <string>

// idhan
#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "DatabaseModule/files/files.hpp"
#include "templates/pipeline/PipelineTemplate.hpp"
#include "objects/tag.hpp"

#include <QFuture>


namespace mappings
{

	namespace raw
	{
		void addMapping( pqxx::work& work, const uint64_t hash_id, const uint64_t tag_id );

		void deleteMapping( pqxx::work& work, const uint64_t hash_id, const uint64_t tag_id );

		[[nodiscard]] std::vector< Tag > getMappings( pqxx::work& work, const uint64_t hash_id );

		[[nodiscard]] std::vector< std::pair< uint64_t, Tag>>
		getMappingsGroup( pqxx::work& work, const std::vector< uint64_t >& hash_ids );
	}

	namespace async
	{
		[[nodiscard]] QFuture< void > addMapping( const uint64_t hash_id, const uint64_t tag_id );

		[[nodiscard]] QFuture< void >
		deleteMapping( const uint64_t, const std::string& group, const std::string& subtag );

		[[nodiscard]] QFuture< void > deleteMapping( const uint64_t hash_id, const uint64_t tag_id );

		[[nodiscard]] QFuture< std::vector< Tag >> getMappings( const uint64_t hash_id );

		[[nodiscard]] QFuture< std::vector< std::pair< uint64_t, Tag>> >
		getMappings( const std::vector< uint64_t >& hash_ids );
	}

}
#endif // MAIN_MAPPINGS_HPP
