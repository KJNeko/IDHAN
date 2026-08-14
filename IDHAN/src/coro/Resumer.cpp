#include "coro/Resumer.hpp"

namespace idhan::coro
{

ResumerProvider g_provider { nullptr };

void setResumerProvider( const ResumerProvider provider ) noexcept
{
	g_provider = provider;
}

Resumer* currentResumer() noexcept
{
	return g_provider ? g_provider() : nullptr;
}

} // namespace idhan::coro
