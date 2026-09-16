#include "idhan/http/SSEBodySource.hpp"

#include <folly/ScopeGuard.h>
#include <folly/io/IOBuf.h>

#include <format>
#include <string>

namespace idhan::http
{

std::string encodeSSEFrame( const std::string_view event, const std::int64_t timestamp, const std::string_view data )
{
	return std::format( "event: {}\ntimestamp: {}\ndata: {}\n\n", event, timestamp, data );
}

[[nodiscard]] static std::string encodeEvent( const SSEEvent& event )
{
	// A comment, so it holds the connection open without any client seeing an event for it.
	if ( event.kind == SSEEvent::Kind::keepalive ) return ": keepalive\n\n";

	if ( event.kind == SSEEvent::Kind::shutdown ) return encodeSSEFrame( "shutdown", event.timestamp, "" );

	const std::string_view name { event.kind == SSEEvent::Kind::snapshot ? "snapshot" : "update" };
	return encodeSSEFrame( name, event.timestamp, event.data ? event.data->text() : "" );
}

SSEBodySource::SSEBodySource( proxygen::HTTPMessage message, SSESubscription subscription ) :
  m_message( std::move( message ) ),
  m_subscription( std::move( subscription ) )
{
	setHeapAllocated();
}

folly::coro::Task< proxygen::coro::HTTPHeaderEvent > SSEBodySource::readHeaderEvent()
{
	if ( !m_message ) throw std::logic_error( "SSE response headers were already read" );

	m_message->setStatusCode( 200 );
	m_message->setStatusMessage( proxygen::HTTPMessage::getDefaultReason( 200 ) );
	m_message->getHeaders().set( "Content-Type", "text/event-stream; charset=utf-8" );
	m_message->getHeaders().set( "Cache-Control", "no-cache" );
	m_message->getHeaders().set( "X-Accel-Buffering", "no" );

	auto message { std::make_unique< proxygen::HTTPMessage >( std::move( *m_message ) ) };
	m_message.reset();
	co_return proxygen::coro::HTTPHeaderEvent { std::move( message ), false };
}

folly::coro::Task< proxygen::coro::HTTPBodyEvent > SSEBodySource::readBodyEvent(
	[[maybe_unused]] const std::uint32_t max )
{
	if ( m_stopped.load( std::memory_order_acquire ) || m_finished )
	{
		proxygen::coro::HTTPBodyEvent event { nullptr, true };
		auto guard { folly::makeGuard( lifetime( event ) ) };
		co_return event;
	}

	// The retry preamble rides in the same chunk as the first event. Returning it on its own would
	// spend the first body event, leaving a subscriber's opening snapshot to wait for proxygen to
	// write that chunk and read from us again.
	std::string payload {};
	if ( m_initial )
	{
		m_initial = false;
		payload = "retry: 3000\n\n";
	}

	const auto event { co_await m_subscription.next() };
	if ( !event )
	{
		m_finished = true;
		proxygen::coro::HTTPBodyEvent eom { payload.empty() ? nullptr : folly::IOBuf::copyBuffer( payload ), true };
		auto guard { folly::makeGuard( lifetime( eom ) ) };
		co_return eom;
	}

	payload += encodeEvent( *event );

	const bool eom { event->kind == SSEEvent::Kind::shutdown };
	m_finished = eom;
	proxygen::coro::HTTPBodyEvent body { folly::IOBuf::copyBuffer( payload ), eom };
	auto guard { folly::makeGuard( lifetime( body ) ) };
	co_return body;
}

void SSEBodySource::stopReading( folly::Optional< const proxygen::coro::HTTPErrorCode > ) noexcept
{
	if ( m_stopped.exchange( true, std::memory_order_acq_rel ) ) return;
	m_subscription.close();
	if ( heapAllocated_ ) delete this;
}

} // namespace idhan::http
