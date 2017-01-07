cmake_minimum_required(VERSION 3.2)
project(sspay)
set(CMAKE_CXX_SANDARD 11)
find_package(Threads)

include(cxx11.cmake)

add_definitions(-DAVHTTP_ENABLE_OPENSSL)

option(Boost_USE_STATIC_LIBS "" ON)

find_package(Boost COMPONENTS locale regex thread system program_options)

include_directories(include/)

include_directories(third_party/avhttp/include/)

include_directories(third_party/soci-3.2.3/core
	third_party/soci-3.2.3/backends)

add_executable(test example/main.cpp src/http_server.cpp src/http_connection.cpp)


target_link_libraries(test
	Boost::system Boost::thread Boost::program_options Boost::regex Boost::locale
	${CMAKE_THREAD_LIBS_INIT} dl systemd)
