# Args: -DPATCH=... -DGIT_EXE=... -DSRC_DIR=...
if(NOT DEFINED PATCH OR NOT DEFINED GIT_EXE OR NOT DEFINED SRC_DIR)
  message(FATAL_ERROR "Pass -DPATCH=... -DGIT_EXE=... -DSRC_DIR=...")
endif()

# Normalize line endings in case of CRLF
file(READ "${PATCH}" _p)
string(REPLACE "\r\n" "\n" _p "${_p}")
string(REPLACE "\r"   "\n" _p "${_p}")
set(_tmp "${SRC_DIR}/_tmp_patch.lf.patch")
file(WRITE "${_tmp}" "${_p}")

# Check if it applies
execute_process(
  COMMAND "${GIT_EXE}" apply --check "${_tmp}"
  WORKING_DIRECTORY "${SRC_DIR}"
  RESULT_VARIABLE rv
)

if(rv EQUAL 0)
  message(STATUS "Applying ${PATCH}")
  execute_process(
    COMMAND "${GIT_EXE}" -c core.autocrlf=false apply --ignore-whitespace --whitespace=nowarn "${_tmp}"
    WORKING_DIRECTORY "${SRC_DIR}"
    RESULT_VARIABLE rv2
  )
  if(NOT rv2 EQUAL 0)
    message(FATAL_ERROR "git apply failed with code ${rv2}")
  endif()
else()
  message(STATUS "Patch ${PATCH} does not apply to current sources; skipping.")
endif()
