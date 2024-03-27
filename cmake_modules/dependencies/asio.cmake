add_library(asio INTERFACE ${ASIO_SRC})
target_compile_definitions(asio INTERFACE ASIO_STANDALONE=1)
target_include_directories(asio INTERFACE ${CMAKE_SOURCE_DIR}/dependencies/asio/asio/include)