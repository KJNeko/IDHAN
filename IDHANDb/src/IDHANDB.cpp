#include "IDHANDB.hpp"

#include <libpq-fe.h>

namespace idhan::db
{

std::runtime_error DBTask::connectionError( const std::string_view operation ) const
{
	return std::runtime_error { std::string { operation } + ": " + PQerrorMessage( m_conn ) };
}

int DBTask::socket() const
{
	if ( !m_conn ) throw std::logic_error( "Database task has no connection" );

	const int descriptor { PQsocket( m_conn ) };
	if ( descriptor < 0 ) throw connectionError( "Failed to get database socket" );

	return descriptor;
}

DBTaskState DBTask::poll()
{
	if ( !m_conn ) throw std::logic_error( "Database task has no connection" );

	const int flush_status { PQflush( m_conn ) };
	if ( flush_status < 0 ) throw connectionError( "Failed to flush database query" );

	if ( flush_status > 0 ) return DBTaskState::NEEDS_WRITE;

	if ( PQconsumeInput( m_conn ) != 1 ) throw connectionError( "Failed to consume database input" );

	if ( PQisBusy( m_conn ) == 1 ) return DBTaskState::NEEDS_READ;

	return DBTaskState::RESULT_READY;
}

DBResult DBTask::nextResult()
{
	if ( !m_conn ) throw std::logic_error( "Database task has no connection" );

	return DBResult { PQgetResult( m_conn ) };
}

IDHANPQContext::IDHANPQContext( const std::string_view host, std::uint16_t port, std::string_view db_name )
{
	m_mgmt_conn =
		PQsetdbLogin( host.data(), std::to_string( port ).data(), nullptr, nullptr, db_name.data(), "idhan", "idhan" );

	if ( !m_mgmt_conn ) throw std::runtime_error( "Failed to allocate database connection" );

	if ( PQstatus( m_mgmt_conn ) != CONNECTION_OK )
	{
		PQfinish( m_mgmt_conn );
		m_mgmt_conn = nullptr;
		throw std::runtime_error( "Failed to connect to database" );
	}

	if ( PQsetnonblocking( m_mgmt_conn, 1 ) != 0 )
	{
		PQfinish( m_mgmt_conn );
		m_mgmt_conn = nullptr;
		throw std::runtime_error( "Failed to put database connection into nonblocking mode" );
	}
}

IDHANPQContext::~IDHANPQContext()
{
	if ( m_mgmt_conn ) PQfinish( m_mgmt_conn );
}

} // namespace idhan::db
