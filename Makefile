COMMON_FLAGS=-no-pie -std=c++11 -Wall -Wno-unused-function 
fast: assets
	# -Wno-unused-but-set-variable -Wno-unused-variable
	./metaprogram ./src/cmantic.cpp ./src/out.cpp
	g++ ${COMMON_FLAGS} -I./3party/SDL2 -I./3party ./src/out.cpp -o cmantic -L./3party -ldl -lX11 -lSDL2 -lGL
	rm ./src/out.cpp

release: assets
	./metaprogram ./src/cmantic.cpp ./src/out.cpp
	g++ ${COMMON_FLAGS} -O3 -I./3party/SDL2 -I./3party ./src/out.cpp -o cmantic -L./3party -ldl -lX11 -lSDL2 -lGL
	rm ./src/out.cpp

debug: assets
	# -Wno-unused-but-set-variable -Wno-unused-variable
	./metaprogram ./src/cmantic.cpp ./src/out.cpp
	g++ -g ${COMMON_FLAGS} -I./3party/SDL2 -I./3party ./src/out.cpp -o cmantic -L./3party -ldl -lX11 -lSDL2 -lGL -fsanitize=address
	rm ./src/out.cpp

.PHONY: tools
tools:
	g++ ${COMMON_FLAGS} ./tools/coroutines.cpp -o metaprogram
	g++ ${COMMON_FLAGS} ./tools/analyze_sprite_image.cpp -o analyze_sprite_image

.PHONY: assets
assets: tools
	./analyze_sprite_image ./assets/sprites.bmp > ./assets/sprite_positions
