#pragma once

#include <map>
#include <span>
#include <string>
#include <string_view>

namespace idhan::cli
{

struct Option
{
	std::string_view name;
	std::string_view description;
	std::string_view value_name;
	std::string_view default_value {};
};

//! Minimal QCommandLineParser stand-in; accepts both `--name value` and `--name=value`.
class Parser
{
  public:

	Parser( std::span< const Option > options, std::string version );

	void process( int argc, char** argv );

	[[nodiscard]] bool isSet( const Option& option ) const;

	[[nodiscard]] std::string value( const Option& option ) const;

  private:

	void printUsage() const;

	std::span< const Option > m_options;
	std::string m_version;
	std::map< std::string, std::string, std::less<> > m_values;
};

} // namespace idhan::cli
