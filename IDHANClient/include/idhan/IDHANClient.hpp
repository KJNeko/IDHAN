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
	//! Name of this application.
	std::string self_name;
	bool use_ssl { false };
};

class IDHANClient : public QObject
{
	Q_OBJECT

	IDHANClientConfig m_config;
	QNetworkAccessManager m_network;
	std::size_t connection_attempts { 0 };

	//! Queries the server version, Returns true if successful
	void attemptQueryVersion();

	inline static IDHANClient* m_instance { nullptr };

  public:

	static IDHANClient& instance();

	Q_DISABLE_COPY_MOVE( IDHANClient );

	IDHANClient() = delete;

	/**
		 * @brief Upon construction the class will attempt to get the version info from the IDHAN server target.
		 * @note Qt must be initalized before construction of this class. Either a QGuiApplication or an QApplication instance
		 * @param config
		 */
	IDHANClient( const IDHANClientConfig& config );

  public slots:
	void handleVersionInfo( QNetworkReply* reply );

  public:

	~IDHANClient() = default;
};

} // namespace idhan
