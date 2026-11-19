# Seihai project, 2026-11-19: see CHANGES-SEIHAI.md.
# libFuzzer over the real client, RC and NC packet handlers (#87). Built from the
# top-level project with -DFUZZ_HANDLERS=ON -DSANITIZE=ON -DTESTS=ON (Clang only).

add_executable(fuzz_handlers fuzz_handlers.cpp)
target_compile_features(fuzz_handlers PUBLIC cxx_std_23)
set_target_properties(fuzz_handlers PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(fuzz_handlers PRIVATE -Wno-unknown-attributes -Wno-narrowing -Wno-switch)
target_link_libraries(fuzz_handlers PRIVATE ${APP_LIBRARY_NAME_TESTREF})
target_link_options(fuzz_handlers PRIVATE -fsanitize=fuzzer -fstack-protector)
