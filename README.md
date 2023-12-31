# chess

building:
- linux - x11:
  `cmake -B build -GNinja -DCMAKE_C_COMPILER=clang -DGLFW_BUILD_WAYLAND=0 -DGLFW_BUILD_X11=1`
- linux - wayland:
  `cmake -B build -GNinja -DCMAKE_C_COMPILER=clang -DGLFW_BUILD_WAYLAND=1 -DGLFW_BUILD_X11=0`
