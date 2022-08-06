//
// Created by kj16609 on 7/8/22.
//


#pragma once
#ifndef IDHAN_TAGS_HPP
#define IDHAN_TAGS_HPP


#include <string>

#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "templates/pipeline/PipelineTemplate.hpp"

#include "objects/tag.hpp"


namespace tags
{

	namespace raw
	{
		Tag getTag( pqxx::work& work, const uint64_t tag_id );


		uint64_t createTag( pqxx::work& work, const Group& group, const Subtag& subtag );


		uint64_t getTagID( pqxx::work& work, const Group& group, const Subtag& subtag );


		void deleteTagFromID( pqxx::work& work, const uint64_t tag_id );
	}

	namespace async
	{
		QFuture< Tag > getTag( const uint64_t tag_id );


		QFuture< uint64_t > createTag( const Group& group, const Subtag& subtag );


		QFuture< uint64_t > getTagID( const Group& group, const Subtag& subtag );


		QFuture< void > deleteTagFromID( const uint64_t tag_id );
	}

}


#endif //IDHAN_TAGS_HPP
