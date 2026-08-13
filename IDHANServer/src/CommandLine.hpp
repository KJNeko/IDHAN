#pragma once

#include <map>
#include <span>
#include <string>
#include <string_view>

namespace idhan::cli
{

//! A single recognised command line option, spelled either `--name value` or `--name=value`.
struct Option
{
	std::string_view name;
	std::string_view description;
	//! Placeholder shown for the value in --help output.
	std::string_view value_name;
	//! What Parser::value() returns when the option was not given.
	std::string_view default_value {};
};

/**
 * @brief Minimal stand-in for QCommandLineParser, covering exactly what IDHANServer asks of it.
 *
 * Both `--name value` and `--name=value` are accepted: the container ENTRYPOINT and the test
 * harness both use the second form. A repeated option keeps the last value given, and --help,
 * --version, an unknown option and a missing value all terminate the process, all matching what
 * QCommandLineParser::process() did.
 */
class Parser
{
  public:

	//! @param version Printed verbatim for --version.
	Parser( std::span< const Option > options, std::string version );

	//! Terminates the process on --help, --version, an unknown option, or a missing value.
	void process( int argc, char** argv );

	//! Whether the option was given on the command line. A default value does not count as set.
	[[nodiscard]] bool isSet( const Option& option ) const;

	//! The value given on the command line, or the option's default if it was not given.
	[[nodiscard]] std::string value( const Option& option ) const;

  private:

	void printUsage() const;

	std::span< const Option > m_options;
	std::string m_version;
	std::map< std::string, std::string, std::less<> > m_values;
};

} // namespace idhan::cli
