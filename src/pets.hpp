
static void do_pet_update_and_draw(float dt) {
  static float frame_time;
  static float frame_delay = 60.0f;
  static int frame = 0;
  frame_time += dt;
  if (frame_time > frame_delay) {
    ++frame;
    frame_time = 0;
  }

  G.pet_index = G.pet_index % 3;

  int animation;
  int num_frames;
  int num_images;
  const int *frames;

  // sleeping animation
  // if (G.activation_meter < 30.0f) {
  {
    static int f[] = {0,1};
    animation = G.pet_index*3 + 0;
    frame_delay = 60.0f;
    frames = f;
    num_frames = ARRAY_LEN(f);
    num_images = 2;
  }
  #if 0
  // running animation
  else {
    static int f[] = {0,1,2,1};
    static int g[] = {0,1,2};
    animation = G.pet_index*3 + 2;
    frame_delay = 10.0f;
    frames = f;
    num_frames = ARRAY_LEN(f);
    num_images = 3;
    if (G.pet_index == 2) {
      frames = g;
      num_frames = ARRAY_LEN(g);
    }
  }
  #endif

  Rect r = G.pet_sprites[animation];
  r.w /= num_images;
  frame = frame % num_frames;
  r.x = r.w * frames[frame];

  const int w = r.w*2;
  const int h = r.h*2;
  const int x = G.win_width - w - 10;
  const int y = G.win_height - h - 10;
  push_square_tquad({x, y, w, h}, r);
}
