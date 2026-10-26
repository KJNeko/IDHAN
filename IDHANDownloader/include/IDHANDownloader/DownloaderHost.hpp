#pragma once

namespace idhan::downloader
{

class ImportSinkFactory;
class CookiePersistence;
class SecretProvider;
class LaneObserver;

struct DownloaderHost
{
	ImportSinkFactory* imports {};
	CookiePersistence* cookies {};
	SecretProvider* secrets {};
	LaneObserver* lanes {};
};

} // namespace idhan::downloader
