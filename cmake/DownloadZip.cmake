function(DownloadZip url filename)
  set(EXTRACT_DIR "${CMAKE_SOURCE_DIR}/third_party/${filename}")
  set(DOWNLOAD_PATH "${CMAKE_SOURCE_DIR}/third_party/download/${filename}.zip")

  # Download and/or extract the binary distribution if necessary.
  if(NOT IS_DIRECTORY "${EXTRACT_DIR}")
    if(NOT EXISTS "${DOWNLOAD_PATH}")
      # Download the binary distribution and verify the hash.
      # message(STATUS "Downloading ${DOWNLOAD_PATH}...")
      file(
        DOWNLOAD "${url}" "${DOWNLOAD_PATH}"
        SHOW_PROGRESS
        )
    endif()

    # Extract the binary distribution.
    file(MAKE_DIRECTORY ${EXTRACT_DIR})
    message(STATUS "Extracting ${DOWNLOAD_PATH}...")
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E tar xzf "${DOWNLOAD_PATH}"
      WORKING_DIRECTORY ${EXTRACT_DIR}
      )
  endif()
endfunction()
