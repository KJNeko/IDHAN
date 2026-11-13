#pragma once

#include <coroutine>

#include "fgl/defines.hpp"

namespace idhan::coro
{

class Resumer
{
  public:

	Resumer() = default;
	virtual ~Resumer() = default;

	FGL_DELETE_COPY( Resumer );
	FGL_DELETE_MOVE( Resumer );

	virtual void resume( std::coroutine_handle<> handle ) noexcept = 0;
};

using ResumerProvider = Resumer* (*)() noexcept;

void setResumerProvider( ResumerProvider provider ) noexcept;

[[nodiscard]] Resumer* currentResumer() noexcept;

} // namespace idhan::coro
