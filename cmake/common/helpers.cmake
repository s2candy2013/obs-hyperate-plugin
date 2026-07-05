include_guard(GLOBAL)

function(set_target_properties_plugin target)
  set(output_name "${target}")

  set(args ${ARGN})
  list(FIND args "OUTPUT_NAME" output_name_index)
  if(NOT output_name_index EQUAL -1)
    math(EXPR output_name_value_index "${output_name_index} + 1")
    list(GET args ${output_name_value_index} output_name)
  endif()

  set_target_properties(
    ${target}
    PROPERTIES
      PREFIX ""
      OUTPUT_NAME "${output_name}"
  )

  if(APPLE)
    set_target_properties(
      ${target}
      PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "plugin"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos-info.plist.in"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${MACOS_BUNDLEID}"
    )

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/locale")
      add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/locale"
        COMMAND
          ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data/locale"
          "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/locale"
        COMMENT "Copying OBS plugin locale files"
      )
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/effects")
      add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/effects"
        COMMAND
          ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data/effects"
          "$<TARGET_BUNDLE_DIR:${target}>/Contents/Resources/effects"
        COMMENT "Copying OBS plugin effect files"
      )
    endif()
  endif()

  install(
    TARGETS ${target}
    LIBRARY DESTINATION "${OBS_PLUGIN_DESTINATION}"
    RUNTIME DESTINATION "${OBS_PLUGIN_DESTINATION}"
    BUNDLE DESTINATION "${OBS_PLUGIN_DESTINATION}"
  )

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/" DESTINATION "${OBS_DATA_DESTINATION}")
  endif()
endfunction()
