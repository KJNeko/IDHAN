//
// Created by kj16609 on 7/27/26.
//

#include "coro/Resumer.hpp"

namespace idhan::coro
{

namespace
{
// IDHAN is an OBJECT library, so this global exists once per consumer binary. That is harmless
// because it is only ever read on the io suspend path, and io is only ever submitted by a process
// that called IOUring::init() -- the server today, the Monitor and Worker later. The premade
// modules link IDHAN but never init io, so their copy stays null and unused.
ResumerProvider g_provider { nullptr };
} // namespace

void setResumerProvider( const ResumerProvider provider ) noexcept
{
	g_provider = provider;
}

Resumer* currentResumer() noexcept
{
	return g_provider ? g_provider() : nullptr;
}

} // namespace idhan::coro
