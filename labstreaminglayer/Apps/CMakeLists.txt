cmake_minimum_required(VERSION 3.5)

option(LSLAPPS_LabRecorder      "Build the recorder app"            ON)

# add all Apps the user wants to build
get_cmake_property(appnames CACHE_VARIABLES)
foreach(appvar ${appnames})
	string(REGEX MATCH "^LSLAPPS_.+" isappvar ${appvar})
	if(isappvar)
		string(SUBSTRING ${appvar} 8 -1 appname)
		if(${appvar})
			if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${appname}/CMakeLists.txt")
				message(WARNING "${appvar} was enabled, but its folder is either nonexistent or not checked out")
			else()
				add_subdirectory(${appname})
			endif()
		endif()
	endif()
endforeach()

