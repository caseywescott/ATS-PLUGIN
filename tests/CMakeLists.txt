find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
  NAME golden_offline
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/golden_compare.py"
          "$<TARGET_FILE:ats_offline>" "${CMAKE_BINARY_DIR}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME ats_roundtrip COMMAND $<TARGET_FILE:ats_roundtrip> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
