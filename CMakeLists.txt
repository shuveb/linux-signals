cmake_minimum_required(VERSION 3.17)
project(linux_async_sync C)

set(CMAKE_C_STANDARD 99)

add_executable(signal_no_restart 01_signal_no_restart.c)
add_executable(signal_restart 02_signal_restart.c)
add_executable(signal_epoll 03_signal_epoll.c)
add_executable(04_signalfd_epoll 04_signalfd_epoll.c)
add_executable(05_pidfd_epoll 05_pidfd_epoll.c)
