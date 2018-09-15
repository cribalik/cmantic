fast: assets
	# -Wno-unused-but-set-variable -Wno-unused-variable
	./metaprogram ./src/cmantic.cpp ./src/out.cpp
	g++ -std=c++11 -I./3party/SDL2 -Wno-unused-function -I./3party ./src/out.cpp -o cmantic -L./3party -ldl -lX11 -lSDL2 -lGL
	rm ./src/out.cpp

release: assets
	./metaprogram ./src/cmantic.cpp ./src/out.cpp
	g++ -O3 -std=c++11 -I./3party/SDL2 -Wno-unused-function -I./3party ./src/out.cpp -o cmantic -L./3party -ldl -lX11 -lSDL2 -lGL
	rm ./src/out.cpp

debug: assets
	# -Wno-unused-but-set-variable -Wno-unused-variable
	./metaprogram ./src/cmantic.cpp ./src/out.cpp
	g++ -g -Wall -std=c++11 -I./3party/SDL2 -Wno-unused-function -I./3party ./src/out.cpp -o cmantic -L./3party -ldl -lX11 -lSDL2 -lGL -fsanitize=address
	rm ./src/out.cpp

.PHONY: tools
tools:
	g++ -std=c++11 ./tools/coroutines.cpp -o metaprogram
	g++ -std=c++11 ./tools/analyze_sprite_image.cpp -o analyze_sprite_image

.PHONY: assets
assets: tools
	./analyze_sprite_image ./assets/sprites.bmp > ./assets/sprite_positions