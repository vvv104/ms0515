# stamp.cmake - copy a page source to dist with @STAMP@ replaced by the build
# time, so a browser never pairs a cached module with a newer page (every
# script / .wasm URL carries ?v=<stamp>).  Run as: cmake -DIN=.. -DOUT=.. -P stamp.cmake
string(TIMESTAMP STAMP "%Y%m%d%H%M%S" UTC)
configure_file("${IN}" "${OUT}" @ONLY)
