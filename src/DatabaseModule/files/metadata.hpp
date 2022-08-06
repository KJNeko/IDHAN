//
// Created by kj16609 on 7/1/22.
//

#pragma once
#ifndef MAIN_METADATA_HPP
#define MAIN_METADATA_HPP


#include <string>

#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "templates/pipeline/PipelineTemplate.hpp"

#include <QFuture>


namespace metadata
{

	namespace raw
	{
		uint64_t getMimeID( pqxx::work& work, const std::string& mime );

		void populateMime( pqxx::work& work, const uint64_t hash_id, const std::string& mime );

		std::string getMime( pqxx::work& work, const uint64_t hash_id );


	}

	namespace async
	{
		QFuture< uint64_t > getMimeID( const std::string& mime );

		QFuture< void > populateMime( const uint64_t hash_id, const std::string& mime );

		QFuture< std::string > getMime( const uint64_t hash_id );
	}

	std::string getFileExtentionFromMime( const std::string mimeType );

	std::string getFileExtention( const uint64_t hash_id );

}

#endif // MAIN_METADATA_HPP
