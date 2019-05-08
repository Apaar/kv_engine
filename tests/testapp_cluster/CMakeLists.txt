if (NOT WIN32)
    add_library(cluster_framework STATIC
                bucket.cc bucket.h
                cluster.cc cluster.h
                clustertest.cc clustertest.h
                dcppipe.cc dcppipe.h
                dcp_replicator.cc dcp_replicator.h
                node.cc node.h)
    target_include_directories(cluster_framework PRIVATE SYSTEM
                               ${FOLLY_INCLUDE_DIR})
    target_link_libraries(cluster_framework PUBLIC
                          mc_client_connection
                          dirutils
                          platform
                          gtest
                          ${FOLLY_LIBRARIES})

    add_executable(cluster_test main.cc)
    target_link_libraries(cluster_test cluster_framework)
    add_dependencies(cluster_test memcached ep default_engine)

add_test(NAME cluster_test
         WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
         COMMAND cluster_test)
endif()