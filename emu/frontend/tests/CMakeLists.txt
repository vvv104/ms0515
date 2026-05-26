# ── ms0515 frontend unit tests ───────────────────────────────────────────────
# Placeholder — Platform tests moved to platform/gui/tests when the layer
# was extracted to its own sublib.  Hook reserved for things like Video
# render-to-buffer checks or Keymap scancode round-trips that would link
# against the frontend's own pieces (Video, Keymap, etc.) without dragging
# in SDL2 / ImGui at runtime.
# ─────────────────────────────────────────────────────────────────────────────

# When the first frontend test lands, declare it here:
#
#     add_executable(ms0515_frontend_tests
#         test_main.cpp
#         test_keymap.cpp
#         ...
#     )
#     target_link_libraries(ms0515_frontend_tests
#         PRIVATE ms0515_lib doctest::doctest
#     )
#     add_test(NAME ms0515_frontend_tests COMMAND ms0515_frontend_tests)
