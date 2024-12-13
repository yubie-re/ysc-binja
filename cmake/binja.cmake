include(FetchContent)
set(BN_INSTALL_DIR "")
set(HEADLESS ON)
set(BN_INTERNAL_BUILD OFF)


FetchContent_Declare(
    binaryninjaapi
    GIT_REPOSITORY https://github.com/Vector35/binaryninja-api.git
    GIT_TAG        9229ebde590febc9635d824ae9284ae170dee9da
    GIT_PROGRESS TRUE
)

message("BINJA")
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
FetchContent_MakeAvailable(binaryninjaapi)

