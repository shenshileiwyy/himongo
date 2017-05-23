cmake_minimum_required(VERSION 3.5)
project(himongo C)

include_directories(libbson/src/bson)

aux_source_directory(. DIR_SRCS)

add_executable(himongo ${DIR_SRCS})
