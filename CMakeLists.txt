cmake_minimum_required(VERSION 3.20)

# Force 32-bit build for SimCity 4 compatibility BEFORE project() call
# Set the platform regardless of current setting to ensure 32-bit
set(CMAKE_GENERATOR_PLATFORM "Win32")
message(STATUS "Forcing 32-bit build for SC4 compatibility")
#
## Force release runtime BEFORE project() call - this is critical
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
set(CMAKE_CXX_FLAGS_DEBUG "/MD /Zi /Ob0 /Od")
set(CMAKE_C_FLAGS_DEBUG "/MD /Zi /Ob0 /Od")

# Additional 32-bit enforcement
if(WIN32)
    set(CMAKE_SIZEOF_VOID_P 4)
    set(CMAKE_C_SIZEOF_DATA_PTR 4)
    set(CMAKE_CXX_SIZEOF_DATA_PTR 4)
endif()

project(SC4AdvancedLotPlop VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform specific settings
if(WIN32)
    add_definitions(-DWIN32 -D_WINDOWS -DUNICODE -D_UNICODE)
endif()

# MSVC specific settings
if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    
    # Force release runtime for all configurations
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    
    # Override debug flags to use release runtime
    set(CMAKE_CXX_FLAGS_DEBUG "/MD /Zi /Ob0 /Od /RTC1")
    set(CMAKE_C_FLAGS_DEBUG "/MD /Zi /Ob0 /Od /RTC1")
    
    # Enable debugging in release builds
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
endif()

add_subdirectory(vendor/spdlog)
add_subdirectory(vendor/args)
add_subdirectory(vendor/mINI)

include(FetchContent)
set(WIL_BUILD_TESTS FALSE)
FetchContent_Declare(WIL
        URL https://github.com/microsoft/wil/archive/refs/tags/v1.0.250325.1.zip)
FetchContent_MakeAvailable(WIL)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz)
FetchContent_MakeAvailable(json)

# gzcom-dll doesn't have CMakeLists.txt, so we include it directly
set(GZCOM_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/vendor/gzcom-dll/gzcom-dll/include)
set(GZCOM_SOURCE_DIR ${CMAKE_SOURCE_DIR}/vendor/gzcom-dll/gzcom-dll/src)
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)

# sc4-dll-basics doesn't have CMakeLists.txt, so we include it directly
set(SC4_DLL_BASICS_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/vendor/sc4-dll-basics/include)
set(SC4_DLL_BASICS_SOURCE_DIR ${CMAKE_SOURCE_DIR}/vendor/sc4-dll-basics/src)

# Automatically find all gzcom-dll source files, excluding main.cpp
file(GLOB GZCOM_SOURCES "${GZCOM_SOURCE_DIR}/*.cpp")
file(GLOB GZCOM_HEADERS "${GZCOM_INCLUDE_DIR}/*.h")
file(GLOB SC4_DLL_BASICS_SOURCE_DIR "${SC4_DLL_BASICS_SOURCE_DIR}/*.cpp")
file(GLOB SC4_DLL_BASICS_HEADERS "${SC4_DLL_BASICS_INCLUDE_DIR}/*.h")
file(GLOB IMGUI_SOURCES "${IMGUI_DIR}/*.cpp")
file(GLOB IMGUI_HEADERS "${IMGUI_DIR}/*.h")

list(REMOVE_ITEM GZCOM_SOURCES
        "${GZCOM_SOURCE_DIR}/main.cpp"
        "${GZCOM_SOURCE_DIR}/cGZCityHallUpgradeDllDirector.cpp"
        "${GZCOM_SOURCE_DIR}/cGZCustomDllDirector.cpp"
        "${GZCOM_SOURCE_DIR}/cGZExampleDllDirector.cpp"
        "${GZCOM_SOURCE_DIR}/cGZExtraCheatsDllDirector.cpp"
        "${GZCOM_SOURCE_DIR}/cGZExtraExtraCheatsDllDirector.cpp"
)
list(APPEND GZCOM_SOURCES
        "${GZCOM_SOURCE_DIR}/cRZCOMDllDirector.cpp"
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${GZCOM_INCLUDE_DIR}
    ${SC4_DLL_BASICS_INCLUDE_DIR}
)

# Our source files
set(SOURCES
    src/AdvancedLotPlopDllDirector.cpp
    src/ui/AdvancedLotPlopUI.cpp
    ${GZCOM_SOURCES}
    ${SC4_DLL_BASICS_SOURCE_DIR}
    ${IMGUI_SOURCES}
    src/utils/Logger.cpp
    src/utils/D3D11Hook.cpp
    src/exemplar/ExemplarUtil.cpp
    src/exemplar/IconResourceUtil.cpp
    src/gfx/DX11ImageLoader.cpp
    src/utils/Config.cpp
    src/utils/CacheSerializer.cpp
    src/s3d/S3DReader.cpp
    src/s3d/FSHReader.cpp
    src/s3d/QFSDecompressor.cpp
    src/s3d/S3DRenderer.cpp
)

set(HEADERS
    ${GZCOM_HEADERS}
    ${SC4_DLL_BASICS_HEADERS}
    ${IMGUI_HEADERS}
    ${CMAKE_SOURCE_DIR}/src/utils/Logger.h
    ${CMAKE_SOURCE_DIR}/src/utils/D3D11Hook.h
    ${CMAKE_SOURCE_DIR}/src/exemplar/ExemplarUtil.h
        src/exemplar/PropertyUtil.cpp
        src/exemplar/PropertyUtil.h
)

# Create the main DLL with explicit exports
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS} SC4AdvancedLotPlop.def)

## Add STLport includes BEFORE other includes
#target_include_directories(${PROJECT_NAME} BEFORE PRIVATE
#        ${STLPORT_ROOT}/stlport
#)
#
#target_compile_definitions(${PROJECT_NAME} PRIVATE
#        _STLP_NO_OWN_IOSTREAMS
#)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    spdlog
    WIL::WIL
    args
    mINI
    nlohmann_json
)

# Windows-specific libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE kernel32 user32 Version d3d11 dxgi windowscodecs)
endif()

# Set output properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "SC4AdvancedLotPlop"
    SUFFIX ".dll"
    PREFIX ""
)

# Force Release runtime even in Debug builds to avoid debug runtime dependency issues
if(MSVC)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
    )
endif()

# Automatic deployment to SC4 directories
if(WIN32)
    # Try to find SC4 installation directory
    set(SC4_INSTALL_PATHS
        "C:/Program Files (x86)/SimCity 4 Deluxe Edition"
        "C:/Program Files (x86)/Maxis/SimCity 4 Deluxe"
        "C:/Program Files/Maxis/SimCity 4 Deluxe"
        "C:/Program Files (x86)/Steam/steamapps/common/SimCity 4 Deluxe"
        "C:/Program Files/Steam/steamapps/common/SimCity 4 Deluxe"
    )
    
    foreach(SC4_PATH ${SC4_INSTALL_PATHS})
        if(EXISTS "${SC4_PATH}/Apps/SimCity 4.exe")
            set(SC4_INSTALL_DIR "${SC4_PATH}")
            break()
        endif()
    endforeach()
    
    # Get user's Documents folder
    set(USERPROFILE_DIR "$ENV{USERPROFILE}")
    set(SC4_PLUGINS_DIR "${USERPROFILE_DIR}/Documents/SimCity 4/Plugins")
    
    if(SC4_INSTALL_DIR)
        message(STATUS "Found SC4 installation at: ${SC4_INSTALL_DIR}")
        
        # Copy main DLL to user's Plugins folder
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SC4_PLUGINS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${PROJECT_NAME}>
            "${SC4_PLUGINS_DIR}/"
            COMMENT "Deploying SC4AdvancedLotPlop.dll to Plugins folder"
        )
        
        message(STATUS "Deployment targets configured:")
        message(STATUS "  - SC4AdvancedLotPlop.dll -> ${SC4_PLUGINS_DIR}/")
    else()
        message(WARNING "SC4 installation not found. Manual deployment required.")
    endif()
endif()

# Install targets
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY docs/
    DESTINATION docs
    FILES_MATCHING PATTERN "*"
)
