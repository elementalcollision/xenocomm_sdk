# Package version
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})

# Package information
set(CPACK_PACKAGE_NAME "xenocomm")
set(CPACK_PACKAGE_VENDOR "XenoComm Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance communication SDK for AI agents")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CONTACT "support@xenocomm.ai")

# Source package settings
set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")
set(CPACK_SOURCE_IGNORE_FILES
    /.git/
    /build/
    /.vscode/
    /.idea/
    /.vs/
    ".*~$"
    ".*.swp$"
    ".DS_Store"
)

# Binary package settings
if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_PACKAGE_NAME "XenoComm SDK")
    set(CPACK_NSIS_MODIFY_PATH ON)
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ;ZIP")
else()
    set(CPACK_GENERATOR "TGZ;DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "cmake (>= 3.15), libstdc++6")
    set(CPACK_RPM_PACKAGE_REQUIRES "cmake >= 3.15, libstdc++")
endif()

# Component-based installation
set(CPACK_COMPONENTS_ALL libraries headers examples documentation)
set(CPACK_COMPONENT_LIBRARIES_DISPLAY_NAME "XenoComm Libraries")
set(CPACK_COMPONENT_HEADERS_DISPLAY_NAME "C++ Headers")
set(CPACK_COMPONENT_EXAMPLES_DISPLAY_NAME "Example Programs")
set(CPACK_COMPONENT_DOCUMENTATION_DISPLAY_NAME "Documentation")

# Dependencies
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_RPM_PACKAGE_AUTOREQ ON)

include(CPack) 