find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_security
    tst_security.cpp
    ../src/network/TransferServer.cpp
    ../src/network/TransferClient.cpp
    ../src/model/PeerStore.cpp
    ../src/model/AppSettings.cpp
    ../src/utils/FileUtils.cpp
    ../src/utils/StartupHelper.cpp
)

target_include_directories(tst_security PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(tst_security PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Sql
    Qt6::Test
)

target_compile_definitions(tst_security PRIVATE APP_VERSION="test")
