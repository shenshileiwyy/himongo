cmake_minimum_required(VERSION 3.5)
project(himongo C)

aux_source_directory(. DIR_SRCS)

add_executable(himongo ${DIR_SRCS})
