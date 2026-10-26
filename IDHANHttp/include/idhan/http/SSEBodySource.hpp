#pragma once
#include <proxygen/lib/http/coro/HTTPSource.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "SSEEventHub.hpp"

namespace idhan::http
{

//! One `event:`/`timestamp:`/`type:`/`data:` frame, terminated by the blank line that dispatches it.
//! Shared so sources that render their own payloads stay identical to SSEBodySource on the wire.
[[nodiscard]] std::string encodeSSEFrame(
	std::string_view event,
	std::int64_t timestamp,
	std::string_view type,
	std::string_view data );

class SSEBodySource final : public proxygen::coro::HTTPSource
{
	std::function< runtime::Task< ValidatedJson >() > m_render;
	std::optional< proxygen::HTTPMessage > m_message;
	SSESubscription m_subscription;
	bool m_initial { true };
	bool m_finished { false };
	std::atomic_bool m_stopped { false };

  public:

	// A renderer resolves coalesced invalidations on the consumer, keeping I/O off producers.
	SSEBodySource(
		proxygen::HTTPMessage,
		SSESubscription subscription,
		std::function< runtime::Task< ValidatedJson >() > render = {} );
	~SSEBodySource() override = default;
	[[nodiscard]] folly::coro::Task< proxygen::coro::HTTPHeaderEvent > readHeaderEvent() override;
	[[nodiscard]] folly::coro::Task< proxygen::coro::HTTPBodyEvent > readBodyEvent( std::uint32_t max ) override;
	void stopReading( folly::Optional< const proxygen::coro::HTTPErrorCode > = folly::none ) noexcept override;
};
} // namespace idhan::http
