//
// Created by kj16609 on 2/21/25.
//

// When testing the server binary should be in the same folder as the test binary. As a result, We can start it outselves
#pragma once



struct ServerHandle
{};

[[nodiscard]] ServerHandle startServer();

#define SERVER_HANDLE const auto _ { startServer() };
