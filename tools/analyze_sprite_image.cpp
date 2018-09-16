#include "../src/util.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "../3party/stb_image.h"

bool is_separator_color(unsigned char *v) {
	return v[0] == 255 && v[1] == 0 && v[2] == 255;
}

int main(int argc, char const *argv[])
{
	util_init();

	if (argc < 2) {
		log_info("Usage: analyze_sprite_image IMAGE_FILE\n");
		return 1;
	}

  int w,h;
  int channels = 3;
  unsigned char *data = stbi_load(argv[1], &w, &h, &channels, 0);
  if (!data) {
  	log_err("Failed to parse %s\n", argv[1]);
    return 1;
  }

  struct SpriteData {
  	int x,y,w,h;
  };

  int y0 = 0;
  for (int y = 0; y < h; ++y) {
  	for (; y < h; ++y)
  		if (is_separator_color(&data[y*w*3]))
  			break;
  	// find width
  	int x;
  	for (x = 0; x < w; ++x)
  		if (is_separator_color(&data[(y0*w + x)*3]))
  			break;
  	printf("%i %i %i %i\n", 0, y0, x, y - y0);
  	for (; y < h; ++y)
  		if (!is_separator_color(&data[y*w*3]))
  			break;
  	y0 = y;
  }


	return 0;
}