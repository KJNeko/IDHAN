#include <catch2/catch_test_macros.hpp>

#include <openssl/pem.h>
#include <openssl/x509.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include "idhan/http/HTTPConfig.hpp"

namespace
{
class TemporaryDirectory
{
	std::filesystem::path m_path;

public:
	explicit TemporaryDirectory( std::filesystem::path path ) : m_path( std::move( path ) )
	{
		std::filesystem::create_directory( m_path );
	}

	~TemporaryDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all( m_path, error );
	}

	[[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }
};
} // namespace

TEST_CASE( "Self-signed TLS credentials form a usable persistent pair", "[http][tls]" )
{
	const auto unique_name {
		"idhan-self-signed-" + std::to_string( std::chrono::steady_clock::now().time_since_epoch().count() ) };
	TemporaryDirectory directory { std::filesystem::temp_directory_path() / unique_name };

	idhan::http::HTTPConfig config { idhan::http::HTTPConfig::defaults() };
	config.tls = true;
	config.self_sign_tls = true;
	config.certificate = directory.path() / "server.crt";
	config.private_key = directory.path() / "server.key";
	idhan::http::ensureSelfSignedCertificate( config );

	REQUIRE( std::filesystem::exists( config.certificate ) );
	REQUIRE( std::filesystem::exists( config.private_key ) );

	BIO* raw_certificate_file { BIO_new_file( config.certificate.c_str(), "r" ) };
	REQUIRE( raw_certificate_file != nullptr );
	std::unique_ptr< BIO, decltype( &BIO_free_all ) > certificate_file { raw_certificate_file, BIO_free_all };
	X509* raw_certificate { PEM_read_bio_X509( certificate_file.get(), nullptr, nullptr, nullptr ) };
	REQUIRE( raw_certificate != nullptr );
	std::unique_ptr< X509, decltype( &X509_free ) > certificate { raw_certificate, X509_free };

	BIO* raw_key_file { BIO_new_file( config.private_key.c_str(), "r" ) };
	REQUIRE( raw_key_file != nullptr );
	std::unique_ptr< BIO, decltype( &BIO_free_all ) > key_file { raw_key_file, BIO_free_all };
	EVP_PKEY* raw_key { PEM_read_bio_PrivateKey( key_file.get(), nullptr, nullptr, nullptr ) };
	REQUIRE( raw_key != nullptr );
	std::unique_ptr< EVP_PKEY, decltype( &EVP_PKEY_free ) > key { raw_key, EVP_PKEY_free };
	REQUIRE( X509_check_private_key( certificate.get(), key.get() ) == 1 );

	const auto certificate_time { std::filesystem::last_write_time( config.certificate ) };
	idhan::http::ensureSelfSignedCertificate( config );
	REQUIRE( std::filesystem::last_write_time( config.certificate ) == certificate_time );
}
