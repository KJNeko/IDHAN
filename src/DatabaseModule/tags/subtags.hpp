//
// Created by kj16609 on 6/28/22.
//

#pragma once
#ifndef MAIN_SUBTAGS_HPP
#define MAIN_SUBTAGS_HPP


#include <string>

#include "templates/pipeline/PipelineTemplate.hpp"
#include "DatabaseModule/DatabaseObjects/database.hpp"

#include "objects/tag.hpp"


namespace subtags
{

	namespace raw
	{
		[[nodiscard]] uint64_t createSubtag( pqxx::work& work, const Subtag& subtag );

		[[nodiscard]] Subtag getSubtag( pqxx::work& work, const uint64_t subtag_id );

		[[nodiscard]]uint64_t getSubtagID( pqxx::work& work, const Subtag& subtag );

		void deleteSubtag( pqxx::work& work, const uint64_t subtag_id );
	}

	namespace async
	{
		QFuture< uint64_t > createSubtag( const Subtag& subtag );

		QFuture< Subtag > getSubtag( const uint64_t subtag_id );

		QFuture< uint64_t > getSubtagID( const Subtag& subtag );

		QFuture< void > deleteSubtag( const uint64_t subtag_id );
	}
}

#endif // MAIN_SUBTAGS_HPP
