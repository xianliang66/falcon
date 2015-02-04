#############################################################################
# Grappa prototype make file

# This sets variables such that GNU Make's implicit rules will work
# for compiling basic Grappa programs with compilers supporting
# GNU-compatible flags. The file is automatically generated by CMake
# when Grappa is configured and built.

# To use this file, write a Makefile including lines like these:
# > include ${CMAKE_BINARY_DIR}/util/grappa.mk
# > grappa_app: grappa_app.o

# This uses Make's implicit rules, so "make grappa_app" will look for
# grappa_app.cpp, compile it into grappa_app.o, and link grappa_app.o
# into a binary named grappa_app.

# The GNU Make implicit compilation recipe for C++ files which turns a
# file n.cpp/n.cc/n.C into n.o looks like: 
# $(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o n.o n.cpp

# The GNU Make implicit link recipe that turns n.o into an executable n is:
# $(CC) $(LDFLAGS) n.o $(LOADLIBES) $(LDLIBS) -o n

# Grappa source files are relative to ${CMAKE_SOURCE_DIR}
# Grappa binary files are relative to ${CMAKE_BINARY_DIR}
#############################################################################

# MPI header paths should automatically be included in CXXFLAGS by
# CMake, so use the plain C++ compiler (but mpicxx should be fine too)
CXX=${CMAKE_CXX_COMPILER}

# Whatever CC points to is used for linking in GNU Make's implicit
# rules, so make that the MPI C++ compiler wrapper too to link with
# its libraries.
CC=${MPI_CXX_COMPILER}
LD=${MPI_CXX_COMPILER}

#############################################################################
# compilation flags
#############################################################################

# this is used by the C++ compile rule
CXXFLAGS=${GMAKE_CXX_DEFINES} \
	${GMAKE_CXX_FLAGS} \
	${GMAKE_CXX_INCLUDE_DIRS}

#############################################################################
# link flags
#############################################################################

# this is used by the C++ implicit link rule
LDFLAGS=${GMAKE_LINK_FLAGS} \
	${GMAKE_LINK_DIRS}

# Ideally CMake would generate these lists, but we do it by hand for now.
STATIC_LIBRARIES=\
	-lGrappa \
	-lgflags \
	-lglog \
	-lgraph500-generator \
	-lboost_filesystem \
	-lboost_system

DYNAMIC_LIBRARIES=\
	-lrt \
	-lpthread

# this is used by the C++ implicit link rule
LDLIBS=${CMAKE_EXE_LINK_STATIC_CXX_FLAGS} $(STATIC_LIBRARIES) \
	${CMAKE_EXE_LINK_DYNAMIC_CXX_FLAGS} $(DYNAMIC_LIBRARIES)