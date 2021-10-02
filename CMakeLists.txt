cmake_policy(SET CMP0048 NEW)
project(stackoverflow)
cmake_minimum_required(VERSION 3.5)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

SET(BOOST_DIR /home/sehe/custom/boost_1_76_0)

set(CMAKE_INSTALL_RPATH "${BOOST_DIR}/stage/lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_SKIP_RPATH FALSE)

SET(CMAKE_C_COMPILER gcc-10)
SET(CMAKE_CXX_COMPILER g++-10)
SET(CMAKE_EXE_LINKER_FLAGS "-Wl,--disable-new-dtags")

LINK_LIBRARIES(boost_system)
LINK_LIBRARIES(boost_thread)

LINK_DIRECTORIES("${BOOST_DIR}/stage/lib")

INCLUDE_DIRECTORIES(SYSTEM ${BOOST_DIR})

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wno-unknown-pragmas  ") # -Wconversion
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a ")
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O3 -pthread -march=native")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -fno-omit-frame-pointer -pthread -march=native")
ADD_DEFINITIONS(-DBOOST_USE_ASAN)
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined")
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto")

ADD_EXECUTABLE(server Server-Test.cpp)
ADD_EXECUTABLE(client Client-Test.cpp)
