release:
	g++ -std=c++11 ./tools/coroutines.cpp -o metaprogram
	./metaprogram ./src/cmantic.cpp ./src/out.cpp
	g++ -O3 -std=c++11 -I./3party/SDL2 -Wno-unused-function -I./3party ./src/out.cpp -o cmantic -L./3party -ldl -lX11 -lSDL2 -lGL
	rm ./src/out.cpp

debug:
	# -Wno-unused-but-set-variable -Wno-unused-variable
	g++ -std=c++11 ./tools/coroutines.cpp -o metaprogram
	./metaprogram ./src/cmantic.cpp ./src/out.cpp
	g++ -g -Wall -std=c++11 -I./3party/SDL2 -Wno-unused-function -I./3party ./src/out.cpp -o cmantic -L./3party -ldl -lX11 -lSDL2 -lGL -fsanitize=address
	rm ./src/out.cpp

