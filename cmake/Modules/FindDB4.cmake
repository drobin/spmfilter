# find db4 library
#

find_library(HAVE_DB4 db)
if(HAVE_DB4)
	get_filename_component(DB4_PATH "${HAVE_DB4}" PATH)
	string(REGEX REPLACE "lib$" "include" DB4_INCLUDE_PATH ${DB4_PATH})
	message(STATUS "  found db4 library ${HAVE_DB4}")
	message(STATUS "  found db4 include path ${DB4_INCLUDE_PATH}")
else(HAVE_DB4)
	message(STATUS "  db4 library could not be found, disabling db4 backend.")
endif(HAVE_DB4)
