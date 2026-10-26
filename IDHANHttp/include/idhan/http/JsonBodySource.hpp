#pragma once
#include <proxygen/lib/http/coro/HTTPSource.h>

#include <atomic>
#include <optional>

#include "ResponseChannel.hpp"

namespace idhan::http
{
class JsonBodySource final : public proxygen::coro::HTTPSource
{
	std::optional< proxygen::HTTPMessage > m_message;
	std::shared_ptr< ResponseChannel > m_channel;
	std::atomic_bool m_stopped { false };

  public:

	JsonBodySource( proxygen::HTTPMessage, std::shared_ptr< ResponseChannel > channel );
	~JsonBodySource() override = default;
	[[nodiscard]] folly::coro::Task< proxygen::coro::HTTPHeaderEvent > readHeaderEvent() override;
	[[nodiscard]] folly::coro::Task< proxygen::coro::HTTPBodyEvent > readBodyEvent( std::uint32_t max ) override;
	void stopReading( folly::Optional< const proxygen::coro::HTTPErrorCode > = folly::none ) noexcept override;
};
} // namespace idhan::http
