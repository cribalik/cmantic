all:
	g++ -g -Wall -Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-function -std=c++11 -I./3party/SDL2 -I./3party ./src/cmantic.cpp -o cmantic -L./3party -ldl -lX11 -lSDL2 -lGL

