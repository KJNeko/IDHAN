// The server binary sits beside the test binary, so the tests can start it themselves.
#pragma once



struct ServerHandle
{};

[[nodiscard]] ServerHandle startServer();

#define SERVER_HANDLE const auto _ { startServer() };
