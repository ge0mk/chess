build/main: main.c build/vert.spv build/frag.spv
	clang -o build/main main.c glad.c -std=c2x -Wall -Wextra -Werror -g -lglfw -lm -DSPIRV_SHADERS

build/vert.spv: main.vert
	-@mkdir -p build
	glslc main.vert -o build/vert.spv

build/frag.spv: main.frag
	-@mkdir -p build
	glslc main.frag -o build/frag.spv
