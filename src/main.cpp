#include "SDL_oldnames.h"
#include "io.hpp"
#include "server.hpp"
#include "window.hpp"

#include "SDL3/SDL.h"
#include "SDL3_image/SDL_image.h"
#include "SDL3_net/SDL_net.h"

int main(int argc, char *argv[]) {
	std::vector<std::string> args(argv, argv + argc);

	bool headless = args.size() >= 2 && args[1] == "--headless";

	if (!headless) {
		if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS) != 0) {
			panic("Failed to initialize SDL");
		}

		if (IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_TIF | IMG_INIT_WEBP | IMG_INIT_JXL | IMG_INIT_AVIF) == 0) {
			panic("Failed to initialize SDL_image");
		}
	}

	if (SDLNet_Init() != 0) {
		panic("Failed to initialize SDL_net");
	}

	int result = 0;
	if (headless) {
		result = Server::run(args);
	} else {
		result = Window::run(args);
	}

	SDLNet_Quit();

	if (!headless) {
		IMG_Quit();
		SDL_Quit();
	}

	return result;
}
