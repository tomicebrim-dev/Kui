cmake_minimum_required(VERSION 3.25)
project(Kui VERSION 1.2.5 DESCRIPTION "Automatic time tracker" LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "Kui is Windows-only (Win32 + Direct3D 11).")
endif()

option(KUI_BUILD_TESTS "Build kui_tests" ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: kui.exe needs no VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- Vendored dependencies (ThirdParty/, see VERSION.txt in each) ---
set(IMGUI_DIR ${PROJECT_SOURCE_DIR}/ThirdParty/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_demo.cpp  # stubs only (IMGUI_DISABLE_DEMO_WINDOWS)
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
  ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp)
# SYSTEM: vendored headers don't trip our warnings
target_include_directories(imgui SYSTEM PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends ${IMGUI_DIR}/misc/cpp)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS)
target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE ${PROJECT_SOURCE_DIR}/ThirdParty)

set(KUI_DEFS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
set(KUI_OPTS $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /W3 /permissive- /MP>)

# --- Everything that isn't UI: data files, tracker, Win32 helpers, setup detection, updater ---
add_library(kui_core STATIC
  Source/Core/Text.cpp
  Source/Core/TimeUtil.cpp
  Source/Core/Store.cpp
  Source/Core/Layout.cpp
  Source/Platform/Platform.cpp
  Source/Setup/SetupDetection.cpp
  Source/Tracker/Tracker.cpp
  Source/Updater/Updater.cpp)
target_include_directories(kui_core PUBLIC Source)
target_compile_definitions(kui_core PUBLIC ${KUI_DEFS} PRIVATE KUI_VERSION="${PROJECT_VERSION}")
target_compile_options(kui_core PRIVATE ${KUI_OPTS})
target_link_libraries(kui_core PUBLIC nlohmann_json shell32 advapi32 dwmapi winhttp version wintrust comdlg32)

# --- The app: one exe; --tracker runs the same exe as the tray tracker ---
configure_file(Resources/Kui.rc.in ${PROJECT_BINARY_DIR}/Kui.rc @ONLY)
add_executable(kui WIN32
  Source/Main.cpp
  Source/UI/Theme.cpp
  Source/UI/Widgets.cpp
  Source/UI/App.cpp
  Source/UI/App_Sidebar.cpp
  Source/UI/App_TopBar.cpp
  Source/UI/App_Entries.cpp
  Source/UI/App_Calendar.cpp
  Source/UI/App_Timeline.cpp
  Source/UI/App_Reports.cpp
  Source/UI/App_Programs.cpp
  Source/UI/App_Setup.cpp
  Source/UI/App_Settings.cpp
  ${PROJECT_BINARY_DIR}/Kui.rc)
target_compile_options(kui PRIVATE ${KUI_OPTS})
target_link_libraries(kui PRIVATE kui_core imgui windowscodecs ole32)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT kui)  # F5 in VS runs kui, not ALL_BUILD
install(TARGETS kui RUNTIME DESTINATION .)

# --- Code signing (optional): -DKUI_SIGN_THUMBPRINT=<SHA1 of a cert in your store> ---
set(KUI_SIGN_THUMBPRINT "" CACHE STRING "SHA1 thumbprint of the code-signing cert; empty = no signing")
set(KUI_SIGN_TIMESTAMP "http://timestamp.digicert.com" CACHE STRING "RFC 3161 timestamp server")
if(KUI_SIGN_THUMBPRINT)
  set(KUI_PF86 "ProgramFiles(x86)")
  file(GLOB KUI_SDK_BINS "$ENV{${KUI_PF86}}/Windows Kits/10/bin/*/x64")
  find_program(SIGNTOOL_EXE signtool HINTS ${KUI_SDK_BINS} REQUIRED)
  set(KUI_SIGN ${SIGNTOOL_EXE} sign /sha1 ${KUI_SIGN_THUMBPRINT} /fd SHA256 /tr ${KUI_SIGN_TIMESTAMP} /td SHA256)
  add_custom_command(TARGET kui POST_BUILD COMMAND ${KUI_SIGN} $<TARGET_FILE:kui> VERBATIM)
  set(KUI_MSI_SIGN COMMAND ${KUI_SIGN} ${PROJECT_BINARY_DIR}/Kui-${PROJECT_VERSION}-x64.msi)
endif()

# --- Tests (enable_testing, not include(CTest): no Dart/Continuous clutter targets) ---
if(KUI_BUILD_TESTS)
  enable_testing()
  add_executable(kui_tests Tests/CoreTests.cpp)
  target_compile_options(kui_tests PRIVATE ${KUI_OPTS})
  target_compile_definitions(kui_tests PRIVATE KUI_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(kui_tests PRIVATE kui_core)
  add_test(NAME core COMMAND kui_tests)
endif()

# --- MSI: cmake --build --preset msi ---
# Needs: dotnet tool install -g wix --version 5.0.2
#        wix extension add -g WixToolset.Util.wixext/5.0.2
find_program(WIX_EXE wix PATHS $ENV{USERPROFILE}/.dotnet/tools)
if(WIX_EXE)
  set(KUI_MSI ${PROJECT_BINARY_DIR}/Kui-${PROJECT_VERSION}-x64.msi)
  add_custom_target(msi
    COMMAND ${WIX_EXE} build ${PROJECT_SOURCE_DIR}/Installer/Package.wxs
      -ext WixToolset.Util.wixext -arch x64
      -d Version=${PROJECT_VERSION}
      -d Exe=$<TARGET_FILE:kui>
      -d Icon=${PROJECT_SOURCE_DIR}/Resources/Kui.ico
      -o ${KUI_MSI}
    ${KUI_MSI_SIGN}
    COMMENT "Packaging ${KUI_MSI}"
    VERBATIM)
  add_dependencies(msi kui)
else()
  message(STATUS "wix not found: 'msi' target disabled")
endif()
