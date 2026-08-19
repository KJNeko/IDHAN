#include "CurrentTestName.hpp"

#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace idhan::test
{

static std::string current_test_name {};

const std::string& currentTestName()
{
	return current_test_name;
}

class TestNameListener final : public Catch::EventListenerBase
{
  public:

	using Catch::EventListenerBase::EventListenerBase;

	void testCaseStarting( const Catch::TestCaseInfo& info ) override { current_test_name = info.name; }
};

} // namespace idhan::test

CATCH_REGISTER_LISTENER( idhan::test::TestNameListener )
