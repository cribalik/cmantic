COMMON_FLAGS=-no-pie -std=c++11 -Wall -Wno-unused-function 
fast: tools
	# -Wno-unused-but-set-variable -Wno-unused-variable
	./metaprogram ./src/cmantic.cpp ./src/out.cpp
	g++ ${COMMON_FLAGS} -I./3party/SDL2 -I./3party ./src/out.cpp -o cmantic -L./3party -ldl -lX11 -lSDL2 -lGL
	rm ./src/out.cpp

release: tools
	./metaprogram ./src/cmantic.cpp ./src/out.cpp
	g++ ${COMMON_FLAGS} -O3 -I./3party/SDL2 -I./3party ./src/out.cpp -o cmantic -L./3party -ldl -lX11 -lSDL2 -lGL
	rm ./src/out.cpp

debug: tools
	# -Wno-unused-but-set-variable -Wno-unused-variable
	./metaprogram ./src/cmantic.cpp ./src/out.cpp
	g++ -g ${COMMON_FLAGS} -I./3party/SDL2 -I./3party ./src/out.cpp -o cmantic -L./3party -ldl -lX11 -lSDL2 -lGL -fsanitize=address
	rm ./src/out.cpp

.PHONY: tools
tools:
	g++ ${COMMON_FLAGS} ./tools/coroutines.cpp -o metaprogram
