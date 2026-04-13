#ifndef _ENCODE_H
#define _ENCODE_H

#include <iconv.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

int code_convert(const char *from_charset, const char *to_charset, char *inbuf, size_t inlen, char *outbuf, size_t outlen);
int g2u(char *inbuf, size_t inlen, char *outbuf, size_t outlen);



#endif