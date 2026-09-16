#pragma once
#include <folly/File.h>
#include <proxygen/lib/http/coro/HTTPSource.h>

#include <atomic>
#include <cstdint>
#include <optional>

namespace idhan::http
{

class FileBodySource final : public proxygen::coro::HTTPSource
{
	std::optional< proxygen::HTTPMessage > m_message;
	folly::File m_file;
	std::uint64_t m_offset, m_remaining;
	std::atomic_bool m_stopped { false };

  public:

	FileBodySource( proxygen::HTTPMessage, folly::File file, std::uint64_t offset, std::uint64_t length );
	~FileBodySource() override = default;
	[[nodiscard]] folly::coro::Task< proxygen::coro::HTTPHeaderEvent > readHeaderEvent() override;
	[[nodiscard]] folly::coro::Task< proxygen::coro::HTTPBodyEvent > readBodyEvent( std::uint32_t max ) override;
	void stopReading( folly::Optional< const proxygen::coro::HTTPErrorCode > = folly::none ) noexcept override;
};

} // namespace idhan::http
