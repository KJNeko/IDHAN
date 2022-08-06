project(IDHANDatabaseUnitTest LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

find_package(Qt6 COMPONENTS Widgets Core Concurrent REQUIRED)

file(GLOB_RECURSE TEST_SOURCES
        "${CMAKE_SOURCE_DIR}/tests/database/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/DatabaseModule/*.cpp"
        )

add_executable(DatabaseUnitTest ${TEST_SOURCES})

target_include_directories(DatabaseUnitTest PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(DatabaseUnitTest PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(DatabaseUnitTest PUBLIC ${CMAKE_SOURCE_DIR}/interface)

target_include_directories(DatabaseUnitTest PUBLIC ${CMAKE_SOURCE_DIR}/submodules/libpqxx/include)
target_include_directories(DatabaseUnitTest PUBLIC ${CMAKE_SOURCE_DIR}/submodules/spdlog/include)
target_include_directories(DatabaseUnitTest PUBLIC ${CMAKE_SOURCE_DIR}/submodules/libFGL/include)


target_compile_features(DatabaseUnitTest PRIVATE cxx_std_20)
target_compile_definitions(DatabaseUnitTest PRIVATE FGL_ACKNOWLEDGE_INCOMPATIBLE_COMPILER)

target_link_libraries(DatabaseUnitTest Qt6::Widgets Qt6::Core Qt6::Concurrent GTest::gtest_main spdlog pqxx TracyClient)

include(GoogleTest)
gtest_discover_tests(DatabaseUnitTest)