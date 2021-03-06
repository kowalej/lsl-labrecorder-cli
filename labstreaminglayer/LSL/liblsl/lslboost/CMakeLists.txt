# Platform-independent sources
set(lslboost_sources
	libs/atomic/src/lockpool.cpp
	libs/chrono/src/chrono.cpp
	libs/program_options/src/convert.cpp
	libs/serialization/src/archive_exception.cpp
	libs/serialization/src/basic_archive.cpp
	libs/serialization/src/basic_iarchive.cpp
	libs/serialization/src/basic_iserializer.cpp
	libs/serialization/src/basic_oarchive.cpp
	libs/serialization/src/basic_oserializer.cpp
	libs/serialization/src/basic_serializer_map.cpp
	libs/serialization/src/extended_type_info.cpp
	libs/serialization/src/extended_type_info_typeid.cpp
	libs/serialization/src/void_cast.cpp
	libs/system/src/error_code.cpp
)
if (UNIX)
	set(lslboost_sources_platform
		libs/thread/src/pthread/once.cpp
		libs/thread/src/pthread/thread.cpp
	)
	find_package(Threads REQUIRED)
	set(Boost_LIBRARIES "Threads::Threads")
else ()  # WIN32
	set(lslboost_sources_platform
		libs/serialization/src/codecvt_null.cpp
		libs/thread/src/win32/thread.cpp
		libs/thread/src/win32/tss_dll.cpp
		libs/thread/src/win32/tss_pe.cpp
	)
	set(Boost_LIBRARIES "")
endif ()

add_library (lslboost STATIC ${lslboost_sources} ${lslboost_sources_platform})
target_compile_definitions(lslboost PUBLIC
	BOOST_ALL_NO_LIB
	BOOST_ASIO_ENABLE_OLD_SERVICES
	BOOST_ASIO_USE_BOOST_DATE_TIME_FOR_SOCKET_IOSTREAM
	_WIN32_WINNT=${LSL_WINVER}
	BOOST_UUID_RANDOM_PROVIDER_FORCE_WINCRYPT
	PRIVATE
	BOOST_THREAD_BUILD_DLL
)

target_include_directories(lslboost SYSTEM PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_link_libraries(lslboost PRIVATE ${Boost_LIBRARIES})
set_target_properties(lslboost PROPERTIES
	CXX_VISIBILITY_PRESET hidden
	POSITION_INDEPENDENT_CODE On
)

# gcc 5 needs -std=c++0x to compile lslboost
if(NOT MSVC)
  target_compile_features(lslboost PRIVATE cxx_auto_type)
endif ()

export(TARGETS lslboost
	FILE lslboostConfig.cmake)
