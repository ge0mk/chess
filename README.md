`cmake -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -D CMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DSPIRV_SHADERS=0`

`cmake --build build`
