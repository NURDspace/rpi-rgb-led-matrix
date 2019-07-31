#include <pthread.h>
#include <string>

#define MILLION 1000000

int hex_to_val(char c);
void hex_str_to_rgb(const std::string & in, uint8_t *const r, uint8_t *const g, uint8_t *const b);
std::string format(const char *const fmt, ...);

uint64_t get_ts();

void hls_to_rgb(const double H, const double L, const double S, double *const r, double *const g, double *const b);

void check_range(int *const chk_val, const int min, const int max);

bool WRITE(const int fd, const char *what, int len);
int make_socket(const char *const interface, const uint16_t port, const bool is_udp = true);
int wait_for_data(const int fd);
