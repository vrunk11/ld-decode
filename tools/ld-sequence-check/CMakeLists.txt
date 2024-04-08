add_executable(ld-sequence-check
    main.cpp
    sequencer.cpp
    sequencingpool.cpp
)

target_link_libraries(ld-sequence-check PRIVATE Qt::Core lddecode-library)

install(TARGETS ld-sequence-check)
