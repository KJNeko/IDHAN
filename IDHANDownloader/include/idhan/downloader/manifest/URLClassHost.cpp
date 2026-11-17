#include "URLClassHost.hpp"

#include "JsonDecoding.hpp"
#include <spdlog/spdlog.h>

using namespace idhan::downloader::manifest_json;

URLClassLane::URLClassLane( element json ) :
  m_concurrency { optional< std::uint32_t >( json, "concurrency" ) },
  m_backoff_statuses { list< std::uint16_t >( json, "backoffStatuses" ) },
  m_keep_alive { optional< double >( json, "keepAlive" ) },
  m_error_backoff { optional< double >( json, "errorBackoff" ) },
  m_http_version { optional< std::string >( json, "httpVersion" ) },
  m_group { optional< std::string >( json, "group" ) }
{
	if ( m_concurrency == 0 ) invalid( "concurrency", "must be positive" );

	if ( m_keep_alive && m_keep_alive.value() < 0 ) invalid( "keepAlive", "must be nonnegative" );

	if ( m_error_backoff && m_error_backoff.value() < 0 ) invalid( "errorBackoff", "must be nonnegative" );

	if ( m_http_version && m_http_version != "1.1" && m_http_version != "2" && m_http_version != "3" )
	{
		invalid( "httpVersion", "expected 1.1, 2, or 3" );
	}
}

URLClassRetry::URLClassRetry( element json ) :
  m_code { read< std::uint16_t >( json, "code" ) },
  m_count { read< std::uint32_t >( json, "count" ) },
  m_wait { optional< double >( json, "wait" ) }
{
	if ( m_code < 100 || m_code > 599 ) invalid( "code", "invalid HTTP status" );

	if ( m_wait && m_wait.value() < 0 ) invalid( "wait", "must be nonnegative" );
}

URLClassHost::URLClassHost( element json )
{
	if ( json.is_string() )
	{
		name = decode< std::string >( json );
		return;
	}

	name = read< std::string >( json, "host" );
	m_request_rate = rate( json );
	m_lane = optional< URLClassLane >( json, "lane" );
	m_retry = list< URLClassRetry >( json, "retry" );
	m_points_to = optional< std::string >( json, "pointsTo" );
}
