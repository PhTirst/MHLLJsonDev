cmake_minimum_required(VERSION 3.20)
project(MHLLJsonDev VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Use MSVC x86: cmake -S . -B build -G \"Visual Studio 17 2022\" -A Win32")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_options(/W3 /utf-8 /EHsc)
add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

add_library(JsonDevCore STATIC
    src/JsonDevBuilder.cpp src/JsonGxtCache.cpp
    src/jsondev/json.cpp src/jsondev/unicode.cpp
    src/jsondev/Config.cpp src/jsondev/Files.cpp src/jsondev/TextRules.cpp)
target_include_directories(JsonDevCore PUBLIC src)
target_compile_definitions(JsonDevCore PRIVATE NOMINMAX)
target_link_libraries(JsonDevCore PUBLIC Shlwapi Gdiplus)

function(add_mhll target jsondev)
    add_library(${target} SHARED src/LangLoader.cpp src/dllmain.cpp src/manhunt/game.cpp src/utils.cpp)
    target_include_directories(${target} PRIVATE src src/manhunt inc third_party/rwsdk36_minimal/include)
    target_link_libraries(${target} PRIVATE Shlwapi)
    if(jsondev)
        target_compile_definitions(${target} PRIVATE MHLL_ENABLE_JSON_GXT_CACHE=1)
        target_link_libraries(${target} PRIVATE JsonDevCore delayimp)
        target_link_options(${target} PRIVATE /DELAYLOAD:Gdiplus.dll)
    endif()
    set_target_properties(${target} PROPERTIES SUFFIX ".asi" PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
endfunction()

add_mhll(MHLL FALSE)
add_mhll(MHLLJsonDev TRUE)

include(CTest)
if(BUILD_TESTING)
    add_executable(JsonDevTests tests/JsonDevTests.cpp)
    target_compile_definitions(JsonDevTests PRIVATE NOMINMAX)
    target_link_libraries(JsonDevTests PRIVATE JsonDevCore)
    add_test(NAME JsonDevTests COMMAND JsonDevTests)
    set_tests_properties(JsonDevTests PROPERTIES TIMEOUT 120)
endif()
