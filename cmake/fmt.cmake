include(FetchContent)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        40626af88bd7df9a5fb80be7b25ac85b122d6c21
    GIT_PROGRESS TRUE
)
message("FMT")
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
FetchContent_MakeAvailable(fmt)

