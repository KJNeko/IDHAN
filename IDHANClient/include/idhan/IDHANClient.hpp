//
// Created by kj16609 on 7/23/24.
//

#pragma once

#include <QFuture>
#include <QNetworkReply>
#include <QUrlQuery>

#include <cstdint>
#include <filesystem>
#include <queue>
#include <string>

#include "IDHANTypes.hpp"
#include "Network.hpp"
#include "logging/logger.hpp"

namespace spdlog
{
class logger;
}

namespace idhan
{
class SHA256;

struct IDHANClientConfig
{
	std::string hostname;
	std::uint16_t port;
	//! Name of this application.
	std::string self_name;
	bool use_ssl { false };
};

struct VersionInfo
{
	struct ServerVersion
	{
		std::string str;
	} server;

	struct ApiVersion
	{
		std::string str;
	} api;
};

class IDHANClient
{
	std::shared_ptr< spdlog::logger > logger { nullptr };
	IDHANClientConfig m_config;
	std::size_t connection_attempts { 0 };

	inline static IDHANClient* m_instance { nullptr };
	QUrl m_url_template {};

	Network network;

	using UrlVariant = std::variant< QString, QUrl >;

	void addKeyHeader( QNetworkRequest& request );

  public:

	void setUrlInfo( QUrl& url );

	static IDHANClient& instance();

	Q_DISABLE_COPY_MOVE( IDHANClient );

	IDHANClient() = delete;

	/**
	* @brief Upon construction the class will attempt to get the version info from the IDHAN server target.
	* @note Qt must be initalized before construction of this class. Either a QGuiApplication or an QApplication instance
	* @param config
	*/
	IDHANClient( const IDHANClientConfig& config );

	~IDHANClient();

	QFuture< std::vector< RecordID > > createRecords( std::vector< std::array< std::byte, 32 > >& hashes );

	/**
	 * @brief
	 * @param hashes Hex representations of hashes
	 * @param network
	 * @return
	 */
	QFuture< std::vector< RecordID > > createRecords( const std::vector< std::string >& hashes );

	QFuture< std::optional< RecordID > > getRecordID( const std::string& sha256 );

	QFuture< VersionInfo > queryVersion();

	QFuture< std::vector< TagID > > createTags( const std::vector< std::string >& tags );
	QFuture< std::vector< TagID > > createTags( const std::vector< std::pair< std::string, std::string > >& tags );

	QFuture< TagID > createTag( const std::string&& namespace_text, const std::string&& subtag_text );

	QFuture< TagID > createTag( const std::string& tag_text );

	QFuture< std::vector< TagID > > getRecordTags( RecordID record_id, TagDomainID domain_id );

	QFuture< std::vector< std::string > > getTagText( std::vector< TagID >& tag_ids );

	QFuture< std::string > getTagText( const TagID tag_id );

	QFuture< std::vector< std::pair< TagID, std::string > > > autocompleteTag( const QString& text );

	QFuture< void > addTags(
		RecordID record_id, TagDomainID domain_id, std::vector< std::pair< std::string, std::string > >&& tags );

	QFuture< void > addTags(
		std::vector< RecordID >&& record_ids,
		TagDomainID domain_id,
		const std::vector< std::pair< std::string, std::string > >& tags );
	QFuture< void > addTags(
		std::vector< RecordID >&& record_ids, TagDomainID domain_id, std::vector< std::vector< TagID > >&& tag_sets );

	QFuture< void > addTags(
		std::vector< RecordID >&& record_ids,
		TagDomainID domain_id,
		std::vector< std::vector< std::pair< std::string, std::string > > >&& tag_sets );

	/**
	 * @brief Creates a parent/child relationship between two tags
	 * @param parent_id
	 * @param child_id
	 * @param tag_domain_id
	 * @return
	 */
	QFuture< void > createParentRelationship( TagDomainID tag_domian_id, TagID parent_id, TagID child_id );
	QFuture< void >
		createParentRelationship( TagDomainID tag_domian_id, std::vector< std::pair< TagID, TagID > >&& pairs );

	/**
	 * @brief Creates a new alias for a given tag.
	 * @param aliased_id
	 * @param alias_id
	 * @param domain_id
	 * @throws AliasLoopException Throws an exception if a loop is detected
	 * @throws InvalidTagID
	 * @return
	 */
	QFuture< void > createAliasRelationship( TagDomainID tag_domain_id, TagID aliased_id, TagID alias_id );

	QFuture< void >
		createAliasRelationship( TagDomainID tag_domain_id, std::vector< std::pair< TagID, TagID > >&& pairs );

	/**
	 * @brief Creates a new tag domain, Throws if the domain exists
	 * @param name
	 * @param network
	 * @throws DomainExists
	 * @return
	 */
	QFuture< TagDomainID > createTagDomain( const std::string& name );

	/**
	 * @brief Searches for an existing tag domain. Throws if it does not exist
	 * @param name
	 * @throws DomainDoesNotExist
	 * @return
	 */
	QFuture< TagDomainID > getTagDomain( std::string_view name );

	struct TagRelationshipInfo
	{
		std::vector< TagID > m_aliased, m_aliases, m_parents, m_children, m_older_siblings, m_younger_siblings;
	};

	QFuture< TagRelationshipInfo > getTagRelationships( TagID tag_id, TagDomainID domain_id );

	struct TagInfo
	{
		TagID m_id;

		struct NamespaceInfo
		{
			NamespaceID m_id;
			std::string m_text;
		} m_namespace;

		struct SubtagInfo
		{
			SubtagID m_id;
			std::string m_text;
		} m_subtag;

		std::string toStdString() const
		{
			FGL_ASSERT( m_namespace.m_id != 0, "Namespace ID invalid" );
			FGL_ASSERT( m_subtag.m_id != 0, "Subtag ID invalid" );

			if ( m_namespace.m_text.empty() ) return m_subtag.m_text;
			return std::format( "{}:{}", m_namespace.m_text, m_subtag.m_text );
		}

		QString toQString() const { return QString::fromStdString( toStdString() ); }

		std::size_t item_count { 0 };
	};

	QFuture< TagInfo > getTagInfo( TagID tag_id );

	/**
	 * @brief Returns a list of all tag domain ids
	 * @return
	 */
	QFuture< std::vector< TagDomainID > > getTagDomains();

	QFuture< void > createFileCluster(
		const std::filesystem::path& server_path,
		const std::string& cluster_name,
		std::size_t byte_limit,
		std::uint16_t ratio,
		bool readonly );

  private:

	void sendClientGet(
		UrlVariant url,
		std::function< void( QNetworkReply* reply ) >&& responseHandler,
		std::function< void( QNetworkReply* reply, QNetworkReply::NetworkError error ) >&& errorHandler );

	void sendClientPost(
		QJsonDocument&& object,
		const UrlVariant& url,
		std::function< void( QNetworkReply* reply ) >&& responseHandler,
		std::function< void( QNetworkReply* reply, QNetworkReply::NetworkError error ) >&& errorHandler );

	void sendClientJson(
		HttpMethod method,
		UrlVariant url,
		std::function< void( QNetworkReply* reply ) >&& responseHandler,
		std::function< void( QNetworkReply* reply, QNetworkReply::NetworkError error ) >&& errorHandler,
		QJsonDocument&& object );

  public:
};

using TagRelationshipInfo = IDHANClient::TagRelationshipInfo;
using TagInfo = IDHANClient::TagInfo;

} // namespace idhan
