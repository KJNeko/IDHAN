#include "RequestRate.hpp"

#include "JsonDecoding.hpp"
#include <spdlog/spdlog.h>

using namespace idhan::downloader::manifest_json;

RequestRate::RequestRate( element json ) :
  m_requests { read< std::uint16_t >( json, "requests" ) },
  m_seconds { read< std::uint16_t >( json, "seconds" ) },
  m_shared_subdomains { optional< bool >( json, "shareSubdomains" ).value_or( false ) },
  m_spread { optional< bool >( json, "spread" ).value_or( false ) },
  m_concurrency { optional< std::uint32_t >( json, "concurrency" ) }
{
	if ( !m_requests || !m_seconds || m_concurrency == 0 ) invalid( "requestRate", "values must be positive" );
}

RequestRateDisabled::RequestRateDisabled( element json )
{
	if ( decode< bool >( json ) ) invalid( "requestRate", "only false disables rate limiting" );
}
