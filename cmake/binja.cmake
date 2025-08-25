include(FetchContent)
set(BN_INSTALL_DIR "")
set(HEADLESS ON)
set(BN_INTERNAL_BUILD OFF)


FetchContent_Declare(
    binaryninjaapi
    GIT_REPOSITORY https://github.com/Vector35/binaryninja-api.git
    GIT_TAG        acf2e005d3fd454c1c6a0c93224be74dcaef33b7
    GIT_PROGRESS TRUE
)

message("BINJA")
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
FetchContent_MakeAvailable(binaryninjaapi)

