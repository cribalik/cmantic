// perfcheck stuff
enum PerfChecks {
  TIMING_MAIN_LOOP,
  TIMING_UPDATE,
  TIMING_RENDER,
  TIMING_PANE_RENDER,
  TIMING_PANE_GUTTER,
  TIMING_PANE_BUFFER,
  TIMING_PANE_PUSH_QUADS,
  TIMING_PANE_PUSH_TEXT_QUADS,
  NUM_TIMINGS,
};

static PerfCheckData my_perfcheck_data[] = {
  {"MAIN_LOOP"},
  {"UPDATE"},
  {"RENDER"},
  {"PANE_RENDER"},
  {"PANE_GUTTER"},
  {"PANE_BUFFER"},
  {"PANE_PUSH_QUADS"},
  {"PANE_PUSH_TEXT_QUADS"},
};
STATIC_ASSERT(ARRAY_LEN(my_perfcheck_data) == NUM_TIMINGS, all_perfchecks_defined);

struct PerfCheckData {
  const char *name;
  u64 t;
  int depth;
};

#ifdef DEBUG
  static StaticArray<PerfCheckData> perfcheck_data;
  #define TIMING_BEGIN(index) if (++perfcheck_data[index].depth == 1) perfcheck_data[index].t -= SDL_GetPerformanceCounter()
  #define TIMING_END(index) if (--perfcheck_data[index].depth == 0) perfcheck_data[index].t += SDL_GetPerformanceCounter()
#else
  #define TIMING_BEGIN(index)
  #define TIMING_END(index)
#endif

static void update_perf_info() {
  for (int i = 0; i < NUM_TIMINGS; ++i)
    perfcheck_data[i].t = 0;
}

static void print_perf_info() {
  puts("\n");
  for (int i = TIMING_MAIN_LOOP+1; i < NUM_TIMINGS; ++i)
    log_info("%s: %f%%\n", perfcheck_data[i].name, (double)perfcheck_data[i].t / perfcheck_data[TIMING_MAIN_LOOP].t * 100.0);
}
