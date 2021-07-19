#
# Copyright 2019 The Nakama Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

cmake_minimum_required(VERSION 3.15)

project(nakama-cpp)
set_property(GLOBAL PROPERTY CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CROSSCOMPILING 1)

cmake_policy(SET CMP0042 NEW)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0054 NEW)
cmake_policy(SET CMP0063 NEW)
cmake_policy(SET CMP0074 NEW)
cmake_policy(SET CMP0077 NEW)
cmake_policy(SET CMP0091 NEW)

#set(CMAKE_C_VISIBILITY_PRESET hidden)
#set(CMAKE_CXX_VISIBILITY_PRESET hidden)
#set(CMAKE_VISIBILITY_INLINES_HIDDEN TRUE)

if(NOT WIN32)
    add_compile_options(-fvisibility=hidden -fPIC)
endif()
add_compile_options(/wd4577)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

find_package(cpprestsdk CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

include(windows_support) 

option(BUILD_REST_CLIENT "Build REST client" ON)
option(BUILD_HTTP_CPPREST "Build HTTP transport using cpprest SDK" ON)
option(BUILD_WEBSOCKET_CPPREST "Build Websocket transport using cpprest SDK" ON)
option(BUILD_GRPC_CLIENT "Build gRPC client" ON)
option(LOGS_ENABLED "enable logs" ON)
option(NAKAMA_SSL_ENABLED "enable SSL" ON)

option(gRPC_BUILD_CSHARP_EXT "" OFF)
set(protobuf_BUILD_TESTS OFF)
set(BENCHMARK_ENABLE_LTO:BOOL OFF)
set(BENCHMARK_ENABLE_TESTING:BOOL OFF)

set(BUILD_STATIC_LIBS ON)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)

set(RUN_HAVE_STD_REGEX ON)
set(RUN_HAVE_POSIX_REGEX 0)
set(RUN_HAVE_GNU_POSIX_REGEX 0)
set(RUN_HAVE_STEADY_CLOCK 0)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(WERROR OFF CACHE BOOL "" FORCE)
set(CPPREST_EXCLUDE_COMPRESSION ON CACHE BOOL "" FORCE)
set(CPPREST_ABI_TAG "" CACHE STRING "" FORCE)

set(PUBLIC_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)
set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)

set(OPENSSL_NO_ASM ON)

add_compile_definitions(_WIN32)
add_compile_definitions(WIN32)
add_compile_definitions(_UNICODE)
add_compile_definitions(UNICODE)

add_compile_definitions(WINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP)
add_compile_definitions(__WRL_NO_DEFAULT_LIB__)
add_compile_definitions(UWP)
add_compile_definitions(NTDDI_VERSION=NTDDI_WIN10)

add_compile_definitions(BUILD_GRPC_CLIENT)

set(USE_CPPREST TRUE)

include_directories(src/api/github.com/heroiclabs/nakama-common/)

add_subdirectory(src)

target_link_libraries(nakama-cpp PRIVATE rapidjson)
target_link_libraries(nakama-cpp PRIVATE gRPC::gpr gRPC::upb gRPC::grpc gRPC::grpc++)
target_link_libraries(nakama-cpp PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(nakama-cpp PRIVATE protobuf::libprotobuf protobuf::libprotobuf-lite)
target_link_libraries(nakama-cpp PRIVATE cpprestsdk::cpprest cpprestsdk::cpprestsdk_zlib_internal cpprestsdk::cpprestsdk_brotli_internal)

# only build tests for desktop OS
if(NOT CMAKE_CROSSCOMPILING)
    add_subdirectory(test)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT nakama-test)
endif()

install(TARGETS nakama-cpp 
        EXPORT nakama-cpp-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
