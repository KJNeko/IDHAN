//
// Created by kj16609 on 8/24/25.
//
#pragma once
#include <json/value.h>

#include <string>

#include "drogon/HttpAppFramework.h"
#include "drogon/orm/DbClient.h"
#include "drogon/utils/coroutine.h"
#include "fgl/defines.hpp"

namespace idhan::jobs
{

enum class JobStatus
{
	//! Job is pending a thread to run on
	PENDING,
	//! Job starting
	STARTED,
	//! Job has failed
	FAILED,
	//! Job has finished with no failure
	COMPLETED,

	//! Job has been serialized to the DB and is pending destruction
	SERIALIZED
};

class [[nodiscard]] JobContext
{
	std::string m_job_id;
	JobStatus m_job_status { JobStatus::STARTED };
	Json::Value m_response;

  protected:

	std::vector< std::string > m_dependencies {};

  public:

	FGL_DELETE_ALL_RO5( JobContext );

	explicit JobContext( std::string job_id );

	//! Overwrites the job_data every time it is called
	virtual Json::Value serialize() = 0;

	//! Executed after creation of a job,
	virtual drogon::Task< void > prepare( drogon::orm::DbClientPtr db ) = 0;

	virtual drogon::Task< void > run() = 0;

	drogon::Task<> addDependency( const std::shared_ptr< JobContext >& shared, drogon::orm::DbClientPtr db );

	drogon::Task<> markJobPending( drogon::orm::DbClientPtr db );
	drogon::Task<> markJobFailed( drogon::orm::DbClientPtr db );
	drogon::Task<> markJobCompleted( drogon::orm::DbClientPtr db );
	drogon::Task<> syncJobStatus( drogon::orm::DbClientPtr db );

	virtual ~JobContext();
};

template < typename T >
concept is_job = requires( T t, const Json::Value& json ) {
	{ t.serialize() } -> std::same_as< Json::Value >;
	{ t.prepare( std::declval< drogon::orm::DbClientPtr >() ) } -> std::same_as< drogon::Task< void > >;
	{ t.run() } -> std::same_as< drogon::Task< void > >;
	requires std::derived_from< T, JobContext >;
	requires std::constructible_from< T, const Json::Value& >;
	{ T::m_job_name } -> std::same_as< const std::string_view& >;
};

template < typename T >
drogon::Task< std::shared_ptr< jobs::JobContext > >
	createJob( Json::Value json, drogon::orm::DbClientPtr db = drogon::app().getDbClient() )
{
	static_assert( jobs::is_job< T >, "T must satisfy is_job concept" );

	auto job_ptr { std::make_shared< T >( "temp", json ) };

	co_await job_ptr->prepare( db );

	co_return job_ptr;
}

} // namespace idhan::jobs