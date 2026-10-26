#pragma once

#include <json/value.h>

#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#include "IDHANTypes.hpp"
#include "idhan/http/SSEEventHub.hpp"

struct JobTaskStatus;

namespace idhan::jobs
{
// Producers only mark IDs dirty. A single publisher snapshots outside the notification lock
// and coalesces changes over 100 ms, keeping serialization off file-processing threads.
class JobEventHub : public std::enable_shared_from_this< JobEventHub >
{
	std::mutex m_mutex {};
	std::condition_variable_any m_wakeup {};
	std::map< JobID, std::weak_ptr< JobTaskStatus > > m_jobs {};
	std::set< JobID > m_dirty {};
	std::jthread m_publisher;

	void run( std::stop_token stop );
	void changed( JobID id );

  public:

	JobEventHub();
	~JobEventHub();
	void add( const std::shared_ptr< JobTaskStatus >& status );
	void remove( JobID id );
};
} // namespace idhan::jobs
