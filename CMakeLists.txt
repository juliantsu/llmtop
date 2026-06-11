cmake_minimum_required(VERSION 3.24)
project(llmtop VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Some transitive dependencies still declare ancient cmake_minimum_required();
# keep them loadable under CMake >= 4.
if(NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

include(FetchContent)

FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG        v5.0.0
  GIT_SHALLOW    TRUE
)

FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

# We only talk to localhost over plain HTTP; skipping SSL keeps the
# vendored curl build small and dependency-free.
set(CPR_ENABLE_SSL OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE STRING "" FORCE)
FetchContent_Declare(cpr
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG        1.11.2
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(ftxui json cpr)

find_package(Threads REQUIRED)

add_executable(llmtop
  src/main.cpp
  src/sources/nvml_source.cpp
  src/sources/llamacpp_source.cpp
  src/sources/ollama_source.cpp
  src/sources/demo_source.cpp
  src/ui/ui.cpp
  src/ui/header.cpp
  src/ui/gpu_panel.cpp
  src/ui/throughput_panel.cpp
  src/ui/slots_panel.cpp
  src/ui/footer.cpp
  src/ui/bigtext.cpp
)

target_compile_definitions(llmtop PRIVATE LLMTOP_VERSION="${PROJECT_VERSION}")
target_include_directories(llmtop PRIVATE src)
target_compile_options(llmtop PRIVATE -Wall -Wextra)
target_link_libraries(llmtop PRIVATE
  ftxui::component  # transitively provides ftxui::dom and ftxui::screen
  cpr::cpr
  nlohmann_json::nlohmann_json
  Threads::Threads
  ${CMAKE_DL_LIBS}
)
