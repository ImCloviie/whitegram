# Copies the Python runtime and the Whitegram plugin SDK next to the binary.
# Used as a POST_BUILD step on Windows; runs `cmake -P` with variables:
#   WG_PY_HOME   - directory of the build-time python executable
#   WG_PY_ABI    - e.g. "314" (major+minor)
#   WG_PY_STDLIB - standard library directory (Lib)
#   WG_PY_DEST   - destination python/ folder next to the binary
#   WG_PY_SDK    - SourceFiles/ayu/features/whitegram_plugins/python
#
# The runtime copy happens only once (when it is missing); the SDK scripts
# are small, so they refresh on every build.

if (NOT EXISTS "${WG_PY_DEST}/python${WG_PY_ABI}.dll")
    file(COPY "${WG_PY_HOME}/python${WG_PY_ABI}.dll" DESTINATION "${WG_PY_DEST}")
endif()
if (EXISTS "${WG_PY_HOME}/python${WG_PY_ABI}.zip")
    if (NOT EXISTS "${WG_PY_DEST}/python${WG_PY_ABI}.zip")
        file(COPY "${WG_PY_HOME}/python${WG_PY_ABI}.zip" DESTINATION "${WG_PY_DEST}")
    endif()
endif()
if (EXISTS "${WG_PY_HOME}/DLLs" AND NOT EXISTS "${WG_PY_DEST}/DLLs")
    file(COPY "${WG_PY_HOME}/DLLs" DESTINATION "${WG_PY_DEST}")
endif()
if (WG_PY_STDLIB AND EXISTS "${WG_PY_STDLIB}" AND NOT EXISTS "${WG_PY_DEST}/Lib")
    file(COPY "${WG_PY_STDLIB}" DESTINATION "${WG_PY_DEST}")
endif()
file(COPY "${WG_PY_SDK}/" DESTINATION "${WG_PY_DEST}/whitegram")
