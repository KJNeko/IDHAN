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
	std::optional<T> getValue( const std::string& valueName )
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
	void setValue( const std::string& valueName, T value )
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
	T getSet( const std::string& valueName, T defaultValue )
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


#endif //IDHAN_CONFIG_HPP