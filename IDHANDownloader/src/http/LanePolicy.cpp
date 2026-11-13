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

//! Leaves room to add a window to a time point without overflowing.
static constexpr LanePolicy::SteadyClock::duration longest_window { (LanePolicy::SteadyClock::duration::max)() / 4 };

//! Even Retry-After: 0 waits one second to avoid retrying in a tight loop.
static constexpr LanePolicy::SteadyClock::duration minimum_limit_pause { std::chrono::seconds { 1 } };

static LanePolicy::SteadyClock::duration windowFor( const RequestRate rate )
{
	if ( rate.requests == 0 || rate.seconds == 0 )
		throw std::invalid_argument( "Downloader request rates require positive requests and seconds" );

	if ( rate.concurrency == 0 ) throw std::invalid_argument( "Downloader request rates require positive concurrency" );

	const auto longest { std::chrono::duration_cast< std::chrono::seconds >( longest_window ) };
	const auto seconds { std::min( rate.seconds, static_cast< std::uint64_t >( longest.count() ) ) };

	return std::chrono::seconds { static_cast< std::chrono::seconds::rep >( seconds ) };
}

static std::size_t capacityFor( const RequestRate rate )
{
	constexpr auto largest { ( std::numeric_limits< LanePolicy::SteadyClock::duration::rep >::max )() };

	return static_cast< std::size_t >( std::min( rate.requests, static_cast< std::uint64_t >( largest ) ) );
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

LanePolicy::LanePolicy( std::string key, LaneSettings settings ) : m_key( std::move( key ) )
{
	configure( std::move( settings ) );
}

void LanePolicy::configure( LaneSettings settings )
{
	const std::scoped_lock lock { m_mutex };
	m_configured_settings = settings;
	configureLocked( std::move( settings ) );
}

void LanePolicy::configureLocked( LaneSettings settings )
{
	if ( m_burst_ceiling.has_value() )
	{
		if ( !settings.rate.has_value() ) settings.rate = RequestRate { m_burst_ceiling.value(), 1 };

		settings.rate->requests = std::min( settings.rate->requests, m_burst_ceiling.value() );
	}

	const auto window { settings.rate.has_value() ? windowFor( *settings.rate ) : SteadyClock::duration::zero() };
	const auto capacity { settings.rate.has_value() ? capacityFor( *settings.rate ) : std::size_t { 1 } };
	if ( capacity != m_capacity && !m_starts.empty() )
	{
		const auto count { std::min( m_starts.size(), capacity ) };
		const auto oldest { m_starts.size() == m_capacity ? m_oldest : std::size_t {} };
		std::vector< SteadyClock::time_point > starts;
		starts.reserve( count );
		for ( auto index { m_starts.size() - count }; index < m_starts.size(); ++index )
			starts.push_back( m_starts[ ( oldest + index ) % m_starts.size() ] );

		m_starts = std::move( starts );
		m_oldest = 0;
	}

	m_settings = std::move( settings );
	m_capacity = capacity;
	m_window = window;
	m_error_backoff = std::chrono::duration_cast< SteadyClock::duration >( m_settings.error_backoff );
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
	return readyAt();
}

bool LanePolicy::disabled() const
{
	const std::scoped_lock lock { m_mutex };
	return m_disabled;
}

void LanePolicy::unsuccessful()
{
	const std::scoped_lock lock { m_mutex };
	m_successes = 0;
}

void LanePolicy::succeeded( const SteadyClock::time_point now )
{
	const std::scoped_lock lock { m_mutex };
	if ( m_disabled ) return;

	if ( now < m_backoff_until ) return;

	if ( !m_burst_ceiling.has_value() ) return;

	if ( ++m_successes < 10 ) return;

	m_successes = 0;
	m_limit_backoff = std::chrono::seconds::zero();
	const auto ceiling { m_burst_ceiling.value() };
	if ( ceiling < ( std::numeric_limits< std::uint64_t >::max )() ) m_burst_ceiling = ceiling + 1;

	if ( m_configured_settings.rate.has_value() && m_burst_ceiling.value() >= m_configured_settings.rate->requests )
		m_burst_ceiling.reset();

	configureLocked( m_configured_settings );
}

LanePolicy::SteadyClock::time_point LanePolicy::readyAt() const
{
	if ( m_disabled ) return SteadyClock::time_point::max();

	auto ready { m_backoff_until };
	if ( m_starts.size() == m_capacity ) ready = std::max( ready, m_starts[ m_oldest ] + m_window );

	if ( !m_settings.rate.has_value() ) return ready;

	if ( !m_settings.rate->spread ) return ready;

	if ( !m_last_start.has_value() ) return ready;

	// Round up to the clock's resolution so spacing never admits an extra request.
	const auto divisor { static_cast< SteadyClock::duration::rep >( m_capacity ) };
	const SteadyClock::duration interval { m_window.count() / divisor + ( m_window.count() % divisor != 0 ) };
	return std::max( ready, m_last_start.value() + interval );
}

LaneSettings LanePolicy::settings() const
{
	const std::scoped_lock lock { m_mutex };
	return m_settings;
}

std::size_t LanePolicy::concurrency( const std::size_t default_concurrency ) const
{
	const std::scoped_lock lock { m_mutex };

	if ( m_settings.concurrency != 0 ) return m_settings.concurrency;

	if ( m_settings.rate.has_value() ) return m_settings.rate->concurrency;

	return default_concurrency;
}

std::optional< LanePolicy::SteadyClock::duration > LanePolicy::claim( const SteadyClock::time_point now )
{
	const std::scoped_lock lock { m_mutex };

	if ( m_disabled ) return SteadyClock::duration::max();

	const auto ready { readyAt() };

	if ( ready > now ) return ready - now;

	m_last_start = now;
	if ( m_starts.size() < m_capacity )
	{
		m_starts.push_back( now );
		return std::nullopt;
	}

	m_starts[ m_oldest ] = now;
	m_oldest = m_oldest + 1 == m_capacity ? 0 : m_oldest + 1;
	return std::nullopt;
}

void LanePolicy::pause( const SteadyClock::duration cooldown, const SteadyClock::time_point now )
{
	m_backoff_until = std::max( m_backoff_until, now + std::min( cooldown, SteadyClock::time_point::max() - now ) );
}

LanePolicy::SteadyClock::duration LanePolicy::limited(
	const std::optional< std::string_view > retry_after,
	const SteadyClock::time_point now,
	const SystemClock::time_point wall_time,
	const bool parser_error )
{
	const auto parsed { retry_after.has_value() ? parseRetryAfter( *retry_after, wall_time ) : std::nullopt };
	const std::scoped_lock lock { m_mutex };
	m_successes = 0;
	if ( m_disabled ) return SteadyClock::duration::zero();

	if ( parser_error )
		++m_parser_error_hits;
	else
	{
		++m_rate_limit_hits;
		const auto burst { m_settings.rate.has_value() ? m_settings.rate->requests : std::uint64_t { 1 } };
		m_burst_ceiling = burst > 1 ? burst - 1 : 1;
		if ( burst > 1 || !m_settings.rate.has_value() ) configureLocked( m_configured_settings );
	}

	m_rate_limited = !parser_error;
	m_parser_limited = parser_error;

	if ( !parser_error && now >= m_backoff_until && m_limit_backoff == std::chrono::seconds { 60 } )
	{
		m_disabled = true;
		++m_generation;
		return SteadyClock::duration::zero();
	}

	if ( parsed.has_value() )
	{
		const auto maximum { std::chrono::duration_cast< std::chrono::seconds >( SteadyClock::duration::max() ) };
		const auto bounded { std::min( *parsed, maximum ) };
		pause( std::max( minimum_limit_pause, std::chrono::duration_cast< SteadyClock::duration >( bounded ) ), now );
		return m_backoff_until - now;
	}

	if ( parser_error )
	{
		if ( now >= m_backoff_until ) pause( std::max( minimum_limit_pause, m_error_backoff ), now );

		return m_backoff_until - now;
	}

	if ( now >= m_backoff_until )
	{
		m_limit_backoff = m_limit_backoff == std::chrono::seconds::zero() ?
		                      std::chrono::seconds { 1 } :
		                      std::min( m_limit_backoff * 2, std::chrono::seconds { 60 } );
		pause( m_limit_backoff, now );
	}

	return m_backoff_until - now;
}

void LanePolicy::failed( const SteadyClock::time_point now )
{
	const std::scoped_lock lock { m_mutex };
	m_successes = 0;

	if ( now < m_backoff_until ) return;

	m_rate_limited = false;
	m_parser_limited = false;
	pause( m_error_backoff, now );
}

void LanePolicy::hold( const SteadyClock::duration wait, const SteadyClock::time_point now )
{
	const std::scoped_lock lock { m_mutex };

	if ( now < m_backoff_until ) return;

	m_rate_limited = false;
	m_parser_limited = false;
	pause( wait, now );
}

void LanePolicy::advertised( std::string headers )
{
	const std::scoped_lock lock { m_mutex };
	m_advertised = std::move( headers );
}

void LanePolicy::reset()
{
	const std::scoped_lock lock { m_mutex };
	m_starts.clear();
	m_last_start.reset();
	m_oldest = 0;
	m_backoff_until = {};
	m_disabled = false;
	m_limit_backoff = {};
	m_successes = 0;
	m_rate_limited = false;
	m_parser_limited = false;
	++m_generation;
}

void LanePolicy::fill( LaneSnapshot& snapshot, const SteadyClock::time_point now ) const
{
	const std::scoped_lock lock { m_mutex };
	snapshot.key = m_key;
	snapshot.group = m_settings.group.value_or( "" );
	snapshot.rate = m_settings.rate.value_or( RequestRate { 0, 0 } );
	snapshot.disabled = m_disabled;
	snapshot.remaining = m_disabled ? SteadyClock::duration::zero() :
	                                  std::max( SteadyClock::duration::zero(), readyAt() - now );
	snapshot.backed_off = m_disabled || now < m_backoff_until;
	snapshot.advertised_limits = m_advertised;
	snapshot.rate_limit_hits = m_rate_limit_hits;
	snapshot.rate_limited = snapshot.backed_off && m_rate_limited;
	snapshot.parser_error_hits = m_parser_error_hits;
	snapshot.parser_limited = snapshot.backed_off && m_parser_limited;
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
