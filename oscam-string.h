#ifndef OSCAM_STRING_H_
#define OSCAM_STRING_H_

void *cs_malloc(void *result, size_t size, int32_t quiterror);
void *cs_realloc(void *result, size_t size, int32_t quiterror);
char *strnew(char *str);

void cs_strncpy(char *destination, const char *source, size_t num);
int32_t cs_strnicmp(const char * str1, const char * str2, size_t num);
char *strtolower(char *txt);
char *trim(char *txt);
int8_t strCmpSuffix(const char *str, const char *suffix);
bool streq(const char *s1, const char *s2);

char *cs_hexdump(int32_t m, const uchar *buf, int32_t n, char *target, int32_t len);

int32_t gethexval(char c);

int32_t cs_atob(uchar *buf, char *asc, int32_t n);
uint32_t cs_atoi(char *asc, int32_t l, int32_t val_on_err);
int32_t byte_atob(char *asc);
int32_t word_atob(char *asc);
int32_t dyn_word_atob(char *asc);
int32_t key_atob_l(char *asc, uchar *bin, int32_t l);
uint32_t b2i(int32_t n, const uchar *b);
uint64_t b2ll(int32_t n, uchar *b);
uchar *i2b_buf(int32_t n, uint32_t i, uchar *b);
uint32_t a2i(char *asc, int32_t bytes);

int32_t boundary(int32_t exp, int32_t n);

#endif
