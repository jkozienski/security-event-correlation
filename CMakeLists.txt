cmake_minimum_required(VERSION 3.16)
project(event_correlation VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE) # benchmarks need optimization
endif()
option(CORRELATION_ENABLE_OPENMP "Build the OpenMP backend when OpenMP is found" ON)
option(CORRELATION_ENABLE_MPI "Build the MPI backend when MPI is found" ON)
set(CORRELATION_HAS_OPENMP OFF)
if(CORRELATION_ENABLE_OPENMP)
  find_package(OpenMP COMPONENTS CXX)
  if(OpenMP_CXX_FOUND)
    set(CORRELATION_HAS_OPENMP ON)
  else()
    message(STATUS "OpenMP not found: building without the openmp backend")
  endif()
endif()
set(CORRELATION_HAS_MPI OFF)
if(CORRELATION_ENABLE_MPI)
  set(MPI_CXX_SKIP_MPICXX TRUE)
  find_package(MPI COMPONENTS CXX)
  if(MPI_FOUND)
    # MS-MPI reports MPI 2.0 even though it provides the two datatype APIs below.
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_LIBRARIES MPI::MPI_CXX)
    check_cxx_source_compiles([=[
      #include <mpi.h>
      int main() {
        MPI_Datatype resized;
        return MPI_Type_create_resized(MPI_UINT64_T, 0, 8, &resized);
      }
    ]=] CORRELATION_MPI_HAS_REQUIRED_TYPES)
    cmake_pop_check_state()
    if(CORRELATION_MPI_HAS_REQUIRED_TYPES)
      set(CORRELATION_HAS_MPI ON)
    else()
      message(STATUS "MPI lacks required datatype support: building without the mpi backend")
    endif()
  else()
    message(STATUS "MPI not found: building without the mpi backend")
  endif()
endif()
add_library(common common/src/common.cpp common/src/csv.cpp)
target_include_directories(common PUBLIC common/include)
add_library(generator generator/src/generator.cpp)
target_include_directories(generator PUBLIC generator/include)
target_link_libraries(generator PUBLIC common)
add_library(sequential sequential/src/sequential.cpp)
target_include_directories(sequential PUBLIC sequential/include)
target_link_libraries(sequential PUBLIC common)
set(search_targets common generator sequential search)
add_library(search search/src/search.cpp)
target_include_directories(search PUBLIC search/include)
target_link_libraries(search PUBLIC sequential)
if(CORRELATION_HAS_OPENMP)
  add_library(openmp_search openmp/src/openmp.cpp)
  target_include_directories(openmp_search PUBLIC openmp/include)
  target_link_libraries(openmp_search PUBLIC sequential OpenMP::OpenMP_CXX)
  target_link_libraries(search PUBLIC openmp_search)
  target_compile_definitions(search PRIVATE CORRELATION_HAS_OPENMP)
  list(APPEND search_targets openmp_search)
endif()
if(CORRELATION_HAS_MPI)
  add_library(mpi_search mpi/src/mpi.cpp)
  target_include_directories(mpi_search PUBLIC mpi/include)
  target_link_libraries(mpi_search PUBLIC sequential MPI::MPI_CXX)
  target_link_libraries(search PUBLIC mpi_search)
  target_compile_definitions(search PUBLIC CORRELATION_HAS_MPI)
  list(APPEND search_targets mpi_search)
endif()
foreach(target ${search_targets})
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /fp:precise)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endforeach()
add_executable(generate_events apps/generate_events.cpp)
target_link_libraries(generate_events PRIVATE generator)
add_executable(main apps/main.cpp)
target_link_libraries(main PRIVATE search)
add_executable(benchmark benchmarks/benchmark.cpp)
target_include_directories(benchmark PRIVATE apps)
target_compile_definitions(benchmark PRIVATE BENCH_BUILD_TYPE="$<CONFIG>")
target_link_libraries(benchmark PRIVATE generator search)
include(CTest)
if(BUILD_TESTING)
  add_executable(correlation_tests tests/correlation_tests.cpp)
  target_link_libraries(correlation_tests PRIVATE generator sequential)
  add_test(NAME reference_correctness COMMAND correlation_tests)
  if(CORRELATION_HAS_OPENMP)
    add_executable(openmp_tests tests/openmp_tests.cpp)
    target_link_libraries(openmp_tests PRIVATE generator openmp_search)
    add_test(NAME openmp_correctness COMMAND openmp_tests)
  endif()
  add_test(NAME cli_integration COMMAND ${CMAKE_COMMAND}
    -DHAS_OPENMP=${CORRELATION_HAS_OPENMP}
    -DHAS_MPI=${CORRELATION_HAS_MPI}
    -DGENERATE=$<TARGET_FILE:generate_events>
    -DSEARCH=$<TARGET_FILE:main>
    -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli-test
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli.cmake)
  if(CORRELATION_HAS_MPI)
    if(MPIEXEC_EXECUTABLE)
      foreach(processes 1 2)
        add_test(NAME mpi_correctness_${processes} COMMAND ${CMAKE_COMMAND}
          "-DMPIEXEC=${MPIEXEC_EXECUTABLE}"
          "-DNUMPROC_FLAG=${MPIEXEC_NUMPROC_FLAG}"
          "-DPREFLAGS=${MPIEXEC_PREFLAGS}"
          "-DPOSTFLAGS=${MPIEXEC_POSTFLAGS}"
          -DPROCESSES=${processes}
          -DGENERATE=$<TARGET_FILE:generate_events>
          -DSEARCH=$<TARGET_FILE:main>
          -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures
          -DWORK=${CMAKE_CURRENT_BINARY_DIR}/mpi-test-${processes}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/mpi.cmake)
        set_tests_properties(mpi_correctness_${processes} PROPERTIES PROCESSORS ${processes} TIMEOUT 180)
      endforeach()
    else()
      message(STATUS "MPI launcher not found: MPI integration tests are unavailable")
    endif()
  endif()
endif()
