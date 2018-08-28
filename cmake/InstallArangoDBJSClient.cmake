################################################################################
### @brief install client-side JavaScript files
################################################################################

install(
  DIRECTORY
    ${ARANGODB_SOURCE_DIR}/js/common
    ${ARANGODB_SOURCE_DIR}/js/client 
  DESTINATION
    ${CMAKE_INSTALL_DATAROOTDIR_ARANGO}/${ARANGODB_JS_VERSION}
  FILES_MATCHING
    PATTERN "*.js"
)

if (USE_ENTERPRISE)
  install(
    DIRECTORY
      ${ARANGODB_SOURCE_DIR}/enterprise/js/common
      ${ARANGODB_SOURCE_DIR}/enterprise/js/client 
    DESTINATION    ${CMAKE_INSTALL_DATAROOTDIR_ARANGO}/${ARANGODB_JS_VERSION}
    FILES_MATCHING PATTERN "*.js"
  )
endif ()

# For the node modules we need all files:
install(
  DIRECTORY ${ARANGODB_SOURCE_DIR}/js/node
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR_ARANGO}/${ARANGODB_JS_VERSION}
  REGEX "^.*/eslint"                                       EXCLUDE
  REGEX "^.*/.npmignore"                                   EXCLUDE
  REGEX "^.*/.bin"                                         EXCLUDE
  REGEX "^.*/_admin/aardvark/APP/frontend/html/"           EXCLUDE
  REGEX "^.*/_admin/aardvark/APP/frontend/img/"            EXCLUDE
  REGEX "^.*/_admin/aardvark/APP/frontend/js/"             EXCLUDE
  REGEX "^.*/_admin/aardvark/APP/frontend/scss/"           EXCLUDE
  REGEX "^.*/_admin/aardvark/APP/frontend/src/"            EXCLUDE
)
