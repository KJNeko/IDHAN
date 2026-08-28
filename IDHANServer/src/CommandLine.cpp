#include "CommandLine.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace idhan::cli
{

//! Strips the leading dashes. Returns an empty view for anything that is not an option.
static std::string_view stripDashes( const std::string_view argument )
{
	if ( argument.starts_with( "--" ) ) return argument.substr( 2 );
	if ( argument.starts_with( '-' ) && argument.size() > 1 ) return argument.substr( 1 );
	return {};
}

Parser::Parser( const std::span< const Option > options, std::string version ) :
  m_options( options ),
  m_version( std::move( version ) ),
  m_values()
{}

//! Column the descriptions start at. A name that reaches it wraps instead of colliding.
static constexpr std::size_t description_column { 26 };

static void printOptionLine( const std::string& name, const std::string_view description )
{
	std::cout << name;
	if ( name.size() < description_column )
		std::cout << std::string( description_column - name.size(), ' ' );
	else
		std::cout << '\n' << std::string( description_column, ' ' );

	std::cout << description;
}

void Parser::printUsage() const
{
	std::cout << "Usage: IDHANServer [options]\n\nOptions:\n";

	printOptionLine( "  --help", "Displays this help" );
	std::cout << '\n';
	printOptionLine( "  --version", "Displays version information" );
	std::cout << '\n';

	for ( const Option& option : m_options )
	{
		std::string name { "  --" };
		name += option.name;
		// An optional value is shown in brackets, the way the flag may actually be written.
		name += option.implicit_value.empty() ? " <" : " [<";
		name += option.value_name;
		name += option.implicit_value.empty() ? ">" : ">]";

		printOptionLine( name, option.description );
		if ( !option.default_value.empty() ) std::cout << " [default: " << option.default_value << ']';
		std::cout << '\n';
	}
}

void Parser::process( const int argc, char** argv )
{
	for ( int i = 1; i < argc; ++i )
	{
		const std::string_view argument { argv[ i ] };
		const std::string_view stripped { stripDashes( argument ) };

		if ( stripped.empty() )
		{
			std::cerr << "Unknown argument '" << argument << "'.\n\n";
			printUsage();
			std::exit( EXIT_FAILURE );
		}

		std::string_view name { stripped };
		std::string_view inline_value {};
		bool has_inline_value { false };

		if ( const auto equals { stripped.find( '=' ) }; equals != std::string_view::npos )
		{
			name = stripped.substr( 0, equals );
			inline_value = stripped.substr( equals + 1 );
			has_inline_value = true;
		}

		if ( name == "help" || name == "h" )
		{
			printUsage();
			std::exit( EXIT_SUCCESS );
		}

		if ( name == "version" || name == "v" )
		{
			std::cout << m_version << '\n';
			std::exit( EXIT_SUCCESS );
		}

		const auto option { std::ranges::find( m_options, name, &Option::name ) };

		if ( option == m_options.end() )
		{
			std::cerr << "Unknown option '" << name << "'.\n\n";
			printUsage();
			std::exit( EXIT_FAILURE );
		}

		if ( has_inline_value )
		{
			m_values[ std::string { name } ] = std::string { inline_value };
			continue;
		}

		// Only a following non-option token can be this option's value; `--flag --other` is two options.
		const bool value_follows { i + 1 < argc && stripDashes( argv[ i + 1 ] ).empty() };

		if ( !option->implicit_value.empty() && !value_follows )
		{
			m_values[ std::string { name } ] = std::string { option->implicit_value };
			continue;
		}

		if ( !value_follows )
		{
			std::cerr << "Missing value after '--" << name << "'.\n\n";
			printUsage();
			std::exit( EXIT_FAILURE );
		}

		++i;
		m_values[ std::string { name } ] = std::string { argv[ i ] };
	}
}

bool Parser::isSet( const Option& option ) const
{
	return m_values.contains( option.name );
}

std::string Parser::value( const Option& option ) const
{
	if ( const auto itter { m_values.find( option.name ) }; itter != m_values.end() ) return itter->second;
	return std::string { option.default_value };
}

} // namespace idhan::cli
