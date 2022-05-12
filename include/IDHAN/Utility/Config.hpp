//
// Created by kj16609 on 5/6/22.
//

#ifndef IDHAN_CONFIG_HPP
#define IDHAN_CONFIG_HPP

#include <string>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>

class IDHANConfig
{
public:

	template < typename T >
	static inline std::optional<T> getValue( const std::string& valueName )
	{
		try
		{
			//read config file
			std::ifstream i( "config.json" );
			nlohmann::json j;
			i >> j;

			return j[valueName].get<T>();
		}
		catch ( ... )
		{
			return std::nullopt;
		}
	}

	template < typename T >
	static inline void setValue( const std::string& valueName, T value )
	{
		//read config file
		std::ifstream i( "config.json" );
		nlohmann::json j;

		if ( i.is_open())
		{
			i >> j;
		}


		j[valueName] = value;

		//write config file
		std::ofstream o( "config.json" );
		o << std::setw( 4 ) << j << std::endl;
	}

	template < typename T >
	static inline T getSet( const std::string& valueName, T defaultValue )
	{
		auto value = getValue<T>( valueName );
		if ( value.has_value())
		{
			return value.value();
		}
		else
		{
			setValue( valueName, defaultValue );
		}
	}

};

std::string getDBString()
{
	std::string dbString;
	dbString += "dbname=" + IDHANConfig::getSet<std::string>( "dbname", "idhanDB" );
	dbString += " user=" + IDHANConfig::getSet<std::string>( "user", "idhan" );
	dbString += " password=" + IDHANConfig::getSet<std::string>( "password", "idhan" );
	dbString += " host=" + IDHANConfig::getSet<std::string>( "host", "localhost" );
	dbString += " port=" + IDHANConfig::getSet<std::string>( "port", "5432" );
	return dbString;
}


#endif //IDHAN_CONFIG_HPP