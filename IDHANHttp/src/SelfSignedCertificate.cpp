#include "idhan/http/HTTPConfig.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace idhan::http
{
namespace
{
template< typename T, void ( *Free )( T* ) >
using OpenSSLPointer = std::unique_ptr< T, decltype( Free ) >;

[[nodiscard]] bool credentialExists( const std::filesystem::path& path )
{
	std::error_code error;
	const bool result { std::filesystem::exists( path, error ) };
	if ( error ) throw std::runtime_error( "Unable to inspect TLS credential path '" + path.string() + "': " + error.message() );

	return result;
}

void require( const bool result, const std::string& operation )
{
	if ( !result ) throw std::runtime_error( "Unable to create self-signed TLS certificate: " + operation );
}

void addExtension( X509* certificate, const int nid, const char* value )
{
	X509_EXTENSION* extension { X509V3_EXT_conf_nid( nullptr, nullptr, nid, const_cast< char* >( value ) ) };
	if ( extension == nullptr ) throw std::runtime_error( "Unable to create self-signed TLS certificate extension" );

	const bool added { X509_add_ext( certificate, extension, -1 ) == 1 };
	X509_EXTENSION_free( extension );
	require( added, "add certificate extension" );
}
} // namespace

void ensureSelfSignedCertificate( const HTTPConfig& config )
{
	if ( !config.tls || !config.self_sign_tls ) return;

	const bool certificate_exists { credentialExists( config.certificate ) };
	const bool key_exists { credentialExists( config.private_key ) };
	if ( certificate_exists && key_exists ) return;

	if ( certificate_exists || key_exists )
		throw std::runtime_error( "Refusing to replace an incomplete TLS credential pair; provide the missing file or remove both files" );

	OpenSSLPointer< EVP_PKEY_CTX, EVP_PKEY_CTX_free > key_context { EVP_PKEY_CTX_new_id( EVP_PKEY_RSA, nullptr ),
		EVP_PKEY_CTX_free };
	if ( !key_context ) throw std::runtime_error( "Unable to create self-signed TLS certificate key context" );

	require( EVP_PKEY_keygen_init( key_context.get() ) == 1, "initialize key generation" );
	require( EVP_PKEY_CTX_set_rsa_keygen_bits( key_context.get(), 2048 ) == 1, "configure key generation" );
	EVP_PKEY* raw_key { nullptr };
	require( EVP_PKEY_keygen( key_context.get(), &raw_key ) == 1, "generate private key" );
	OpenSSLPointer< EVP_PKEY, EVP_PKEY_free > key { raw_key, EVP_PKEY_free };

	OpenSSLPointer< X509, X509_free > certificate { X509_new(), X509_free };
	if ( !certificate ) throw std::runtime_error( "Unable to allocate self-signed TLS certificate" );

	require( X509_set_version( certificate.get(), 2 ) == 1, "set certificate version" );
	require( ASN1_INTEGER_set( X509_get_serialNumber( certificate.get() ), 1 ) == 1, "set certificate serial number" );
	require( X509_gmtime_adj( X509_getm_notBefore( certificate.get() ), 0 ) != nullptr, "set certificate start time" );
	require( X509_gmtime_adj( X509_getm_notAfter( certificate.get() ), 365L * 24L * 60L * 60L ) != nullptr,
		"set certificate expiration" );
	require( X509_set_pubkey( certificate.get(), key.get() ) == 1, "set certificate public key" );

	X509_NAME* subject { X509_get_subject_name( certificate.get() ) };
	require( X509_NAME_add_entry_by_txt( subject, "CN", MBSTRING_ASC,
			 reinterpret_cast< const unsigned char* >( "localhost" ), -1, -1, 0 ) == 1,
		"set certificate subject" );
	require( X509_set_issuer_name( certificate.get(), subject ) == 1, "set certificate issuer" );
	addExtension( certificate.get(), NID_basic_constraints, "critical,CA:FALSE" );
	addExtension( certificate.get(), NID_key_usage, "critical,digitalSignature,keyEncipherment" );
	addExtension( certificate.get(), NID_ext_key_usage, "serverAuth" );
	addExtension( certificate.get(), NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1,IP:::1" );
	require( X509_sign( certificate.get(), key.get(), EVP_sha256() ) > 0, "sign certificate" );

	BIO* raw_key_file { BIO_new_file( config.private_key.c_str(), "w" ) };
	if ( raw_key_file == nullptr ) throw std::runtime_error( "Unable to write self-signed TLS private key" );
	std::unique_ptr< BIO, decltype( &BIO_free_all ) > key_file { raw_key_file, BIO_free_all };
	require( PEM_write_bio_PrivateKey( key_file.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr ) == 1,
		"write private key" );
	key_file.reset();

	std::error_code error;
	std::filesystem::permissions(
		config.private_key,
		std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
		std::filesystem::perm_options::replace,
		error );
	if ( error ) throw std::runtime_error( "Unable to restrict TLS private key permissions: " + error.message() );

	BIO* raw_certificate_file { BIO_new_file( config.certificate.c_str(), "w" ) };
	if ( raw_certificate_file == nullptr ) throw std::runtime_error( "Unable to write self-signed TLS certificate" );
	std::unique_ptr< BIO, decltype( &BIO_free_all ) > certificate_file { raw_certificate_file, BIO_free_all };
	require( PEM_write_bio_X509( certificate_file.get(), certificate.get() ) == 1, "write certificate" );
}
} // namespace idhan::http
