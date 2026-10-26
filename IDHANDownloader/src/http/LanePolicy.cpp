#include "http/LanePolicy.hpp"

#include <algorithm>
#include <charconv>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace idhan::downloader
{

static LanePolicy::SteadyClock::duration intervalFor( const RequestRate rate )
{
	if ( rate.requests == 0 || rate.seconds == 0 )
		throw std::invalid_argument( "Downloader request rates require positive requests and seconds" );

	const auto interval {
		std::chrono::duration_cast< LanePolicy::SteadyClock::duration >( std::chrono::duration< long double > {
			static_cast< long double >( rate.seconds ) / static_cast< long double >( rate.requests ) } )
	};

	return std::max( interval, LanePolicy::SteadyClock::duration { 1 } );
}

static std::string_view trim( const std::string_view value )
{
	const auto first { value.find_first_not_of( " \t" ) };

	if ( first == std::string_view::npos ) return {};

	const auto last { value.find_last_not_of( " \t" ) };
	return value.substr( first, last - first + 1 );
}

static std::time_t utcTime( std::tm& value )
{
#ifdef _WIN32
	return _mkgmtime( &value );
#else
	return timegm( &value );
#endif
}

LanePolicy::LanePolicy( std::string key, LaneSettings settings, const SteadyClock::duration maximum_interval ) :
  m_key( std::move( key ) ),
  m_maximum_interval( maximum_interval )
{
	if ( maximum_interval <= SteadyClock::duration::zero() )
		throw std::invalid_argument( "Downloader maximum request backoff must be positive" );

	configure( std::move( settings ) );
}

void LanePolicy::configure( LaneSettings settings )
{
	const auto interval { settings.rate.has_value() ? intervalFor( *settings.rate ) : SteadyClock::duration::zero() };
	const std::scoped_lock lock { m_mutex };
	m_settings = std::move( settings );
	m_base_interval = interval;
	m_interval = interval;
	m_error_backoff = std::chrono::duration_cast< SteadyClock::duration >( m_settings.error_backoff );
	m_next_request = {};
	m_backoff_until = {};
	m_consecutive_failures = 0;
	++m_generation;
}

std::uint64_t LanePolicy::generation() const
{
	const std::scoped_lock lock { m_mutex };
	return m_generation;
}

LanePolicy::SteadyClock::time_point LanePolicy::nextSlot() const
{
	const std::scoped_lock lock { m_mutex };
	return m_next_request;
}

LaneSettings LanePolicy::settings() const
{
	const std::scoped_lock lock { m_mutex };
	return m_settings;
}

bool LanePolicy::throttled() const
{
	const std::scoped_lock lock { m_mutex };
	return m_settings.rate.has_value();
}

std::size_t LanePolicy::concurrency( const std::size_t unthrottled_default, const std::size_t throttled_default ) const
{
	const std::scoped_lock lock { m_mutex };

	if ( m_settings.concurrency != 0 ) return m_settings.concurrency;

	return m_settings.rate.has_value() ? throttled_default : unthrottled_default;
}

std::optional< LanePolicy::SteadyClock::duration > LanePolicy::claim( const SteadyClock::time_point now )
{
	const std::scoped_lock lock { m_mutex };

	if ( m_next_request > now ) return m_next_request - now;

	// Preserve cadence without banking slots after an idle period.
	const auto following { m_next_request + m_interval };
	m_next_request = following > now ? following : now + m_interval;
	return std::nullopt;
}

// Unthrottled lanes back off from a one-second floor.
void LanePolicy::widen()
{
	constexpr SteadyClock::duration floor { std::chrono::seconds { 1 } };
	const auto widened { m_interval == SteadyClock::duration::zero() ? floor : m_interval * 2 };
	m_interval = std::min( m_maximum_interval, std::max( m_base_interval, widened ) );

	if ( m_consecutive_failures != ( std::numeric_limits< std::uint32_t >::max )() ) ++m_consecutive_failures;
}

// Called with m_mutex held.
LanePolicy::SteadyClock::duration LanePolicy::openPause( const SteadyClock::time_point now )
{
	if ( now < m_backoff_until ) return SteadyClock::duration::zero();

	widen();
	return std::max( m_interval, m_error_backoff );
}

// Called with m_mutex held.
void LanePolicy::pause( const SteadyClock::duration cooldown, const SteadyClock::time_point now )
{
	m_backoff_until = std::max( m_backoff_until, now + std::min( cooldown, SteadyClock::time_point::max() - now ) );
	m_next_request = std::max( m_next_request, m_backoff_until );
}

LanePolicy::SteadyClock::duration LanePolicy::limited(
	const std::optional< std::string_view > retry_after,
	const SteadyClock::time_point now,
	const SystemClock::time_point wall_time )
{
	const auto parsed { retry_after.has_value() ? parseRetryAfter( *retry_after, wall_time ) : std::nullopt };
	const std::scoped_lock lock { m_mutex };

	auto cooldown { openPause( now ) };

	// The host named its own deadline, so it replaces the widened pause instead of joining it.
	if ( parsed.has_value() )
	{
		const auto maximum { std::chrono::duration_cast< std::chrono::seconds >( SteadyClock::duration::max() ) };
		const auto bounded { std::min( *parsed, maximum ) };
		cooldown = std::max( m_base_interval, std::chrono::duration_cast< SteadyClock::duration >( bounded ) );
	}

	pause( cooldown, now );
	return m_backoff_until - now;
}

void LanePolicy::failed( const SteadyClock::time_point now )
{
	const std::scoped_lock lock { m_mutex };
	pause( openPause( now ), now );
}

void LanePolicy::advertised( std::string headers )
{
	const std::scoped_lock lock { m_mutex };
	m_advertised = std::move( headers );
}

void LanePolicy::reset()
{
	const std::scoped_lock lock { m_mutex };
	m_interval = m_base_interval;
	m_next_request = {};
	m_backoff_until = {};
	m_consecutive_failures = 0;
	++m_generation;
}

void LanePolicy::fill( LaneSnapshot& snapshot, const SteadyClock::time_point now ) const
{
	const std::scoped_lock lock { m_mutex };
	snapshot.key = m_key;
	snapshot.group = m_settings.group;
	snapshot.rate_requests = m_settings.rate.has_value() ? m_settings.rate->requests : 0;
	snapshot.rate_seconds = m_settings.rate.has_value() ? m_settings.rate->seconds : 0;
	snapshot.throttled = m_settings.rate.has_value();
	snapshot.effective_interval = m_interval;
	snapshot.remaining = std::max( SteadyClock::duration::zero(), m_next_request - now );
	snapshot.consecutive_failures = m_consecutive_failures;
	snapshot.backed_off = m_interval > m_base_interval || m_consecutive_failures > 0;
	snapshot.bytes_per_second = m_settings.bytes_per_second;
	snapshot.advertised_limits = m_advertised;
}

std::optional< std::chrono::seconds > LanePolicy::parseRetryAfter(
	std::string_view value,
	const SystemClock::time_point now )
{
	value = trim( value );
	std::uint64_t seconds {};
	const auto [ end, error ] { std::from_chars( value.data(), value.data() + value.size(), seconds ) };

	if ( error == std::errc {} && end == value.data() + value.size() )
	{
		const auto maximum { static_cast< std::uint64_t >( (std::chrono::seconds::max)().count() ) };
		return std::chrono::seconds { static_cast< std::chrono::seconds::rep >( std::min( seconds, maximum ) ) };
	}

	std::tm date {};
	std::istringstream input { std::string { value } };
	input.imbue( std::locale::classic() );
	input >> std::get_time( &date, "%a, %d %b %Y %H:%M:%S GMT" );

	if ( input.fail() ) return std::nullopt;

	input >> std::ws;

	if ( !input.eof() ) return std::nullopt;

	const std::time_t timestamp { utcTime( date ) };

	if ( timestamp == static_cast< std::time_t >( -1 ) ) return std::nullopt;

	const auto retry_time { SystemClock::from_time_t( timestamp ) };

	if ( retry_time <= now ) return std::chrono::seconds::zero();

	return std::chrono::ceil< std::chrono::seconds >( retry_time - now );
}

} // namespace idhan::downloader
