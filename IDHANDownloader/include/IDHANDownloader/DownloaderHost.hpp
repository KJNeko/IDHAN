#pragma once

namespace idhan::downloader
{

class ImportSinkFactory;
class CookiePersistence;
class SecretProvider;
class LaneObserver;
class ParserHostPersistence;

struct DownloaderHost
{
	ImportSinkFactory* imports {};
	CookiePersistence* cookies {};
	SecretProvider* secrets {};
	LaneObserver* lanes {};
	ParserHostPersistence* parser_hosts {};
};

} // namespace idhan::downloader
