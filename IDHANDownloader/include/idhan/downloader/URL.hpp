#pragma once

class URL
{
	std::string m_url;

  public:

	URL( const std::string_view url ) : m_url( url )
	{
		// TODO: Ada checks
	}
};
