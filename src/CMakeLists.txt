
if(UNIX)
	add_definitions(-std=c++11)
endif()
add_ugdk_executable(shader-bullets main.cc)
