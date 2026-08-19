#pragma once

#include <string>

namespace idhan::test
{

//! The name of the test case currently running, as Catch2 reported it when the case started.
[[nodiscard]] const std::string& currentTestName();

} // namespace idhan::test
