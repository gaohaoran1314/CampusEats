cmake_minimum_required(VERSION 3.20)
project(CampusEats LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# 核心库：除 main.cpp 之外的所有实现都在这儿。
# 只让一个目标拥有这些 .cpp —— 以前库和 exe 各列了一份，同一份代码编两遍，
# exe 自带的那份会把库里的顶掉，编译宏因此一直没生效（存档路径问题的根源）。
# 以后加服务端：新建一个带自己 main 的 exe，链接 eats_core 就行。
# ---------------------------------------------------------------------------
add_library(eats_core STATIC
        App.cpp
        Cart.cpp
        Console.cpp
        CsvExporter.cpp
        DeliveryService.cpp
        Merchant.cpp
        MerchantConsole.cpp
        Order.cpp
        Pricing.cpp
        RiderConsole.cpp
        SaveFile.cpp
        SeedData.cpp
        StudentConsole.cpp
        Views.cpp
)

target_include_directories(eats_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 把工程根目录固化进程序：存档、导出都落在这儿，跟「从哪个目录启动」无关。
# PUBLIC 是故意的 —— 链接 eats_core 的目标都拿到同一个值，不会再出现
# "库里有宏、exe 里没有"这种一半生效的情况。挪动工程目录后要重新构建。
target_compile_definitions(eats_core PUBLIC
        CAMPUS_EATS_DATA_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\")

if(MSVC)
    set(EATS_CXX_FLAGS /utf-8 /W4 /permissive- /Zc:__cplusplus)
else()
    set(EATS_CXX_FLAGS -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# 控制台程序：只管 main.cpp，其余全部来自 eats_core。
# ---------------------------------------------------------------------------
add_executable(CampusEats main.cpp)
target_link_libraries(CampusEats PRIVATE eats_core)

target_compile_options(eats_core  PRIVATE ${EATS_CXX_FLAGS})
target_compile_options(CampusEats PRIVATE ${EATS_CXX_FLAGS})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT CampusEats)

# ---------------------------------------------------------------------------
# 服务端：给浏览器用的 HTTP 接口，和控制台共用同一个 eats_core。
# ---------------------------------------------------------------------------
add_executable(CampusEatsServer main_server.cpp)
target_link_libraries(CampusEatsServer PRIVATE eats_core)
target_compile_options(CampusEatsServer PRIVATE ${EATS_CXX_FLAGS})

# main_server.cpp 这一个文件里堆了 nlohmann/json 的大量模板实例化
# （每家店、每张订单都要过一遍转换函数），编出来的 .obj 里 .debug_frame 段
# 撑破了 COFF 格式 32 位段内偏移的上限，链接时直接报：
#     relocation truncated to fit: IMAGE_REL_AMD64_SECREL against .debug_frame
# 让汇编器改用「大对象」格式就行 —— MinGW-w64 这边对应 MSVC 的 /bigobj。
#
# 只给服务端加这一条：eats_core 和控制台版离这个量级还差得远，
# 没必要跟着一起变。反过来，要是哪天 eats_core 里某个文件也撑破了，
# 那说明该拆文件了，而不是全项目一起开大对象格式。
if(MSVC)
    target_compile_options(CampusEatsServer PRIVATE /bigobj)
else()
    target_compile_options(CampusEatsServer PRIVATE -Wa,-mbig-obj)
endif()

# SYSTEM 是让这两个头文件自己的警告闭嘴，
# 否则 -Wpedantic 会刷几百行跟你无关的噪音，把真问题淹掉。
target_include_directories(CampusEatsServer SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

if(WIN32)
    # 单头文件版的 httplib 不会自动链上 winsock
    target_link_libraries(CampusEatsServer PRIVATE ws2_32)

    # httplib 0.56.0 在头文件开头硬性要求「目标系统 ≥ Windows 10」：
    #     #if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0A00
    #     #error "cpp-httplib doesn't support Windows 8 or lower..."
    # 而什么宏都不给的话，MinGW 又不会声明 httplib 内部用到的 CreateFile2
    # （那是 Windows 8 起的 API）。所以按它的要求，明说跑在 Win10 上：
    # 0x0A00 = Windows 10，NTDDI_WIN10 = 0x0A000000
    target_compile_definitions(CampusEatsServer PRIVATE
            _WIN32_WINNT=0x0A00
            WINVER=0x0A00
            NTDDI_VERSION=0x0A000000)
endif()