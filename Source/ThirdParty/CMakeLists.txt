# STB
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/stb")
install (DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/stb DESTINATION ${INSTALL_THIRDPARTY_DIR} FILES_MATCHING PATTERN *.h)

# Add targets
#foreach (TARGET ETCPACK FreeType LZ4 PugiXml rapidjson SDL StanHull)
#    add_subdirectory (${TARGET})
#    set_property(TARGET ${TARGET} PROPERTY FOLDER "ThirdParty")
#endforeach ()

#if (URHO3D_NETWORK)
#    add_subdirectory (Civetweb)
#    add_subdirectory (SLikeNet)
#
#    set_property(TARGET Civetweb PROPERTY FOLDER "ThirdParty")
#    set_property(TARGET SLikeNet PROPERTY FOLDER "ThirdParty")
#endif ()

if (NOT ANDROID AND NOT ARM AND NOT WEB)
    if (URHO3D_OPENGL)
        add_subdirectory (glad)
    endif ()
endif ()
