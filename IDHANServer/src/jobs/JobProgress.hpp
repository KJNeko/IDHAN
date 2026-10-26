#pragma once

#include <json/value.h>

#include <functional>
#include <mutex>

class JobProgress
{
	mutable std::mutex m_callback_mutex {};
	mutable std::function< void() > m_on_change {};

  protected:

	// Only queue notifications here: a producer may still hold its progress mutex.
	void changed() const
	{
		std::lock_guard lock { m_callback_mutex };
		if ( m_on_change ) m_on_change();
	}

  public:

	virtual ~JobProgress() = default;

	void setChangeCallback( std::function< void() > callback ) const
	{
		std::lock_guard lock { m_callback_mutex };
		m_on_change = std::move( callback );
	}

	// Implementations provide a consistent snapshot while the job's workers update their state.
	[[nodiscard]] virtual Json::Value snapshot() const = 0;
};
