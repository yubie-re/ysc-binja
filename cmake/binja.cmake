include(FetchContent)
set(BN_INSTALL_DIR "")
set(HEADLESS ON)
set(BN_INTERNAL_BUILD OFF)


FetchContent_Declare(
    binaryninjaapi
    GIT_REPOSITORY https://github.com/Vector35/binaryninja-api.git
    GIT_TAG        ab5c0b34b473fa6cdc5ad55635e670e27688d71c
    GIT_PROGRESS TRUE
)

message("BINJA")
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
FetchContent_MakeAvailable(binaryninjaapi)

