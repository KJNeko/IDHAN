#include <gtest/gtest.h>
#include <idhan/downloader/ParserRegistry.hpp>

#include <ostream>
#include <string>

namespace idhan::downloader::tests
{

struct URLCase
{
	const char* name;
	const char* url;
	const char* url_class;
	const char* pattern;
	const char* generator;
	const char* capture;
	const char* value;
};

void PrintTo( const URLCase& test, std::ostream* output )
{
	*output << test.url;
}

class DownloaderURLMatch : public testing::TestWithParam< URLCase >
{};

ParserRegistry& sharedRegistry()
{
	static ParserRegistry registry { IDHAN_TEST_PARSER_DIR };
	return registry;
}

void checkMatch( ParserRegistry& registry, const URLCase& test )
{
	SCOPED_TRACE( test.url );
	const auto match { registry.tryParseUrl( test.url ) };
	ASSERT_TRUE( match.has_value() );
	EXPECT_EQ( match->match.url_class, test.url_class );
	EXPECT_EQ( match->match.pattern, test.pattern );
	ASSERT_TRUE( match->match.generator.has_value() );
	EXPECT_EQ( match->match.generator.value(), test.generator );
	const auto capture { match->match.captures.find( test.capture ) };
	ASSERT_NE( capture, match->match.captures.end() );
	EXPECT_EQ( capture->second, test.value );
}

TEST_P( DownloaderURLMatch, Isolated )
{
	ParserRegistry registry { IDHAN_TEST_PARSER_DIR };
	checkMatch( registry, GetParam() );
}

TEST_P( DownloaderURLMatch, Shared )
{
	checkMatch( sharedRegistry(), GetParam() );
}

INSTANTIATE_TEST_SUITE_P(
	Premade,
	DownloaderURLMatch,
	testing::Values(
		URLCase {
			"GelbooruPost",
			"https://gelbooru.com/index.php?page=post&s=view&id=123",
			"gelbooru",
			"Post",
			"post",
			"id",
			"123" },
		URLCase {
			"GelbooruSearch",
			"https://gelbooru.com/index.php?tags=wolf&s=list&page=post",
			"gelbooru",
			"Tag search",
			"search",
			"tags",
			"wolf" },
		URLCase { "E621Post", "https://e621.net/posts/123?q=wolf", "e621", "Post", "post", "id", "123" },
		URLCase { "E621Search", "https://e621.net/posts?tags=wolf", "e621", "Tag search", "search", "tags", "wolf" },
		URLCase { "PixivArtwork", "https://www.pixiv.net/artworks/123", "pixiv", "Artwork", "post", "id", "123" },
		URLCase {
			"PixivLocalizedArtwork",
			"https://www.pixiv.net/en/artworks/123",
			"pixiv",
			"Artwork",
			"post",
			"locale",
			"en" },
		URLCase {
			"PixivCreator",
			"https://www.pixiv.net/en/users/123/illustrations",
			"pixiv",
			"Creator",
			"user",
			"id",
			"123" },
		URLCase {
			"PawchivePost",
			"https://pawchive.pw/patreon/user/123/post/456",
			"pawchive",
			"Post",
			"post",
			"id",
			"456" },
		URLCase {
			"KemonoCreator",
			"https://kemono.su/patreon/user/123",
			"pawchive",
			"Creator",
			"creator",
			"creator",
			"123" },
		URLCase {
			"MegaFile",
			"https://mega.nz/file/example#examplekey",
			"mega",
			"Shared file or folder",
			"shared",
			"id",
			"example" },
		URLCase {
			"MegaFolder",
			"https://mega.nz/folder/example#examplekey",
			"mega",
			"Shared file or folder",
			"shared",
			"id",
			"example" } ),
	[]( const testing::TestParamInfo< URLCase >& info ) { return std::string { info.param.name }; } );

struct RejectedURL
{
	const char* name;
	const char* url;
};

void PrintTo( const RejectedURL& test, std::ostream* output )
{
	*output << test.url;
}

class DownloaderURLReject : public testing::TestWithParam< RejectedURL >
{};

void checkRejected( ParserRegistry& registry, const RejectedURL& test )
{
	SCOPED_TRACE( test.url );
	EXPECT_FALSE( registry.tryParseUrl( test.url ).has_value() );
}

TEST_P( DownloaderURLReject, Isolated )
{
	ParserRegistry registry { IDHAN_TEST_PARSER_DIR };
	checkRejected( registry, GetParam() );
}

TEST_P( DownloaderURLReject, Shared )
{
	checkRejected( sharedRegistry(), GetParam() );
}

INSTANTIATE_TEST_SUITE_P(
	Premade,
	DownloaderURLReject,
	testing::Values(
		RejectedURL { "InvalidURL", "not a url" },
		RejectedURL { "UnknownHost", "https://example.com/posts/123" },
		RejectedURL { "E621ExtraPath", "https://e621.net/posts/123/extra" },
		RejectedURL { "GelbooruMissingID", "https://gelbooru.com/index.php?page=post&s=view" },
		RejectedURL { "PixivInvalidLocale", "https://www.pixiv.net/english/artworks/123" } ),
	[]( const testing::TestParamInfo< RejectedURL >& info ) { return std::string { info.param.name }; } );

} // namespace idhan::downloader::tests
