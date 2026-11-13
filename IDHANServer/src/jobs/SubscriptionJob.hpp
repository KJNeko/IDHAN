#pragma once

#include <memory>
#include <optional>
#include <string>

#include "db/subscriptions.hpp"

struct JobTask;

namespace idhan::jobs
{
// The scheduler owns the run; this suspended job exposes its progress and final response.
class SubscriptionJob
{
	class Progress;
	std::shared_ptr< Progress > m_progress;
	static JobTask waitForRun( std::shared_ptr< Progress > progress );

  public:

	explicit SubscriptionJob( db::SubscriptionRunResult result, std::string label );
	~SubscriptionJob();
	SubscriptionJob( const SubscriptionJob& ) = delete;
	SubscriptionJob& operator=( const SubscriptionJob& ) = delete;
	void update( db::SubscriptionRunResult result );
	void finish( std::optional< std::string > error = {} );
};
} // namespace idhan::jobs
