//
// Created by kj16609 on 7/23/24.
//

#pragma once

#include <QNetworkAccessManager>
#include <QObject>

#include <cstdint>
#include <string>

namespace idhan
{

	struct IDHANClientConfig
	{
		std::string hostname;
		std::uint16_t port;
	};

	class IDHANClient : public QObject
	{
		Q_OBJECT

		IDHANClientConfig m_config;
		QNetworkAccessManager m_network;
		std::size_t connection_attempts { 0 };

		//! Queries the server version, Returns true if successful
		void attemptQueryVersion();

	  public:

		Q_DISABLE_COPY_MOVE( IDHANClient );

		IDHANClient() = delete;
		IDHANClient( const IDHANClientConfig& config );

	  public slots:
		void recieveVersionData();

	  public:

		~IDHANClient() = default;
	};

} // namespace idhan
