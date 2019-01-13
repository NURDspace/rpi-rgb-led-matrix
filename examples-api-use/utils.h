#include <pthread.h>
#include <string>

#define MILLION 1000000

int hex_to_val(char c);
void hex_str_to_rgb(const std::string & in, uint8_t *const r, uint8_t *const g, uint8_t *const b);
std::string format(const char *const fmt, ...);
int64_t get_ts();
void set_thread_name(const pthread_t th, const std::string & name);

void bitblit(uint8_t *const target, const int tw, const int th, const int tx, const int ty, const uint8_t *const source, const int sw, const int sh, const int sx, const int sy, const int scw, const int sch, const std::string & transparent_color, const int alpha);
void hls_to_rgb(const double H, const double L, const double S, double *const r, double *const g, double *const b);

void check_range(int *const chk_val, const int min, const int max);
