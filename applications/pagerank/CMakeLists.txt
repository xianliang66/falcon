add_definitions(-Drestrict=__restrict__ -DGRAPH_GENERATOR_GRAPPA -D_GRAPPA)

add_grappa_application(pagerank.exe ${SOURCES} pagerank.cpp)

