# -*- mode: cmake -*-
# -------------------------------------------------------------
# file: CMakeLists.txt
# -------------------------------------------------------------
# -------------------------------------------------------------
# Created May  6, 2013 by William A. Perkins
# Last Change: 2013-07-01 13:43:28 d3g096
# -------------------------------------------------------------

set(target_libraries
    gridpack_components
    ${GA_LIBRARIES}
    ${Boost_LIBRARIES}
    ${MPI_CXX_LIBRARIES})

include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR})

if (GA_FOUND)
  include_directories(AFTER ${GA_INCLUDE_DIRS})
endif()
# -------------------------------------------------------------
# TEST: greetings
# A simple MPI program
# -------------------------------------------------------------
add_executable(test_parser test/PTI23_test.cpp)
target_link_libraries(test_parser ${target_libraries})

add_test(test_parser_serial test_parser)
#!!add_test(NAME greetings_parallel COMMAND 
#!!  ${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG} 4 ${MPIEXEC_PREFLAGS} $<TARGET_FILE:greetings> ${MPIEXEC_POSTFLAGS})
#!!set_tests_properties(greetings_serial greetings_parallel
#!!  PROPERTIES 
#!!  PASS_REGULAR_EXPRESSION "I am process 0 of [1-9].*$"
#!!)

# -------------------------------------------------------------
# TEST: mpi_test
# A simple MPI test using boost::test
# -------------------------------------------------------------
#!!add_executable(mpi_test test/mpi_test.cpp)
#!!target_link_libraries(mpi_test ${Boost_LIBRARIES} ${MPI_CXX_LIBRARIES})

#!!add_test(NAME mpi_test_serial COMMAND $<TARGET_FILE:mpi_test>)
#!!add_test(NAME mpi_test_parallel COMMAND 
#!!  ${MPIEXEC} ${MPIEXEC_NUMPROC_FLAG} 4 ${MPIEXEC_PREFLAGS} $<TARGET_FILE:mpi_test> ${MPIEXEC_POSTFLAGS})
#!!set_tests_properties(mpi_test_serial mpi_test_parallel
#!!  PROPERTIES 
#!!  PASS_REGULAR_EXPRESSION "No errors detected"
#!!)
