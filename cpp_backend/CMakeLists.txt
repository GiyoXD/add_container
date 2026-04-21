cmake_minimum_required(VERSION 3.16)
project(AddContainerProject VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find Qt
find_package(Qt6 COMPONENTS Core Gui Widgets Network Sql Concurrent REQUIRED)

add_executable(add_container WIN32
    main.cpp
    MainWindow.cpp
    MainWindow.h
    DatabaseManager.cpp
    DatabaseManager.h
    GeminiClient.cpp
    GeminiClient.h
    GoogleSheetsClient.cpp
    GoogleSheetsClient.h
    DataRow.h
)

target_link_libraries(add_container PRIVATE 
    Qt6::Core 
    Qt6::Gui 
    Qt6::Widgets 
    Qt6::Network 
    Qt6::Sql 
    Qt6::Concurrent
)

if(WIN32)
    target_link_libraries(add_container PRIVATE bcrypt ncrypt)
endif()
