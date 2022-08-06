//
// Created by kj16609 on 6/28/22.
//
#pragma once
#ifndef MAIN_GROUPS_HPP
#define MAIN_GROUPS_HPP


#include <cstdint>
#include <string>

#include "templates/pipeline/PipelineTemplate.hpp"
#include "DatabaseModule/DatabaseObjects/database.hpp"
#include "objects/tag.hpp"


#include <QFuture>


namespace groups
{

	namespace raw
	{
		[[nodiscard]] uint64_t createGroup( pqxx::work& work, const Group& group );

		[[nodiscard]] Group getGroup( pqxx::work& work, const uint64_t group_id );

		[[nodiscard]] uint64_t getGroupID( pqxx::work& work, const Group& group );

		void removeGroup( pqxx::work& work, const uint64_t group_id );
	}

	namespace async
	{
		[[nodiscard]] QFuture< uint64_t > createGroup( const Group& group );

		[[nodiscard]] QFuture< Group > getGroup( const uint64_t group_id );

		[[nodiscard]] QFuture< uint64_t > getGroupID( const Group& group );

		[[nodiscard]] QFuture< void > removeGroup( const uint64_t group_id );
	}
}

#endif // MAIN_GROUPS_HPP
