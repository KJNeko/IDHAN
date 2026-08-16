find_package(libpqxx 7 CONFIG QUIET)

if(NOT libpqxx_FOUND)
    # Fallback: pkg-config for Ubuntu system libpqxx
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBPQXX REQUIRED IMPORTED_TARGET libpqxx)

    if(LIBPQXX_FOUND AND NOT TARGET libpqxx::pqxx)
        add_library(libpqxx::pqxx ALIAS PkgConfig::LIBPQXX)
    endif()

    include(FindPackageHandleStandardArgs)
    find_package_handle_standard_args(libpqxx DEFAULT_MSG LIBPQXX_LIBRARIES LIBPQXX_INCLUDE_DIRS)
endif()
