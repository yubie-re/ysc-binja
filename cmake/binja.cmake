include(FetchContent)
set(BN_INSTALL_DIR "")
set(HEADLESS ON)
set(BN_INTERNAL_BUILD OFF)


FetchContent_Declare(
    binaryninjaapi
    GIT_REPOSITORY https://github.com/Vector35/binaryninja-api.git
    GIT_TAG        745c0b3b806b7e44aba36cf3a8564dfa0631dc8f
    GIT_PROGRESS TRUE
)

message("BINJA")
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
FetchContent_MakeAvailable(binaryninjaapi)

