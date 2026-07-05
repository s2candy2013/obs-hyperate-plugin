include_guard(GLOBAL)

include(GNUInstallDirs)

if(APPLE)
  set(OBS_PLUGIN_DESTINATION ".")
  set(OBS_DATA_DESTINATION "data")
elseif(WIN32)
  set(OBS_PLUGIN_DESTINATION "obs-plugins/64bit")
  set(OBS_DATA_DESTINATION "data/obs-plugins/${_name}")
else()
  set(OBS_PLUGIN_DESTINATION "${CMAKE_INSTALL_LIBDIR}/obs-plugins")
  set(OBS_DATA_DESTINATION "${CMAKE_INSTALL_DATADIR}/obs/obs-plugins/${_name}")
endif()
