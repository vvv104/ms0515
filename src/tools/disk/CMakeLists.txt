# ── ms0515-disk ──────────────────────────────────────────────────────────────
# Offline RT-11 / MS-0515 disk utility (dir / extract / build) over the
# ms0515_disk library.
# ─────────────────────────────────────────────────────────────────────────────

add_executable(ms0515-disk
    main.cpp
)

target_compile_features(ms0515-disk PRIVATE cxx_std_23)

target_link_libraries(ms0515-disk
    PRIVATE ms0515_disk
)
