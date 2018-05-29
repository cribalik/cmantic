all:
	# -Wno-unused-but-set-variable -Wno-unused-variable
	g++ -g -Wall -std=c++11 -I./3party/SDL2 -Wno-unused-function -I./3party ./src/cmantic.cpp -o cmantic -L./3party -ldl -lX11 -lSDL2 -lGL -fsanitize=address
