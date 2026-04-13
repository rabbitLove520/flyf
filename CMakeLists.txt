cmake_minimum_required(VERSION 3.20)

project(flyf C)

set(CMAKE_C_STANDARD 17)

aux_source_directory(./src DIR_SRCS)

add_executable(ftp ${DIR_SRCS})