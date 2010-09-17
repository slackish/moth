#include <sys/types.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include "common.h"
#include "arc4.h"

#define itokenlen 3

char const consonants[] = "bcdfghklmnprstvz";
char const vowels[]     = "aeiouy";

#define bubblebabble_len(n) (6*(((n)/2)+1))

/** Compute bubble babble for input buffer.
 *
 * The generated output will be of length 6*((inlen/2)+1), including the
 * trailing NULL.
 *
 * Test vectors:
 *     `' (empty string) `xexax'
 *     `1234567890'      `xesef-disof-gytuf-katof-movif-baxux'
 *     `Pineapple'       `xigak-nyryk-humil-bosek-sonax'
 */
void
bubblebabble(char *out, char const *in, const size_t inlen)
{
  size_t pos  = 0;
  int    seed = 1;
  size_t i    = 0;

  out[pos++] = 'x';
  while (1) {
    unsigned char c;

    if (i == inlen) {
      out[pos++] = vowels[seed % 6];
      out[pos++] = 'x';
      out[pos++] = vowels[seed / 6];
      break;
    }

    c = in[i++];
    out[pos++] = vowels[(((c >> 6) & 3) + seed) % 6];
    out[pos++] = consonants[(c >> 2) & 15];
    out[pos++] = vowels[((c & 3) + (seed / 6)) % 6];
    if (i == inlen) {
      break;
    }
    seed = ((seed * 5) + (c * 7) + in[i]) % 36;

    c = in[i++];
    out[pos++] = consonants[(c >> 4) & 15];
    out[pos++] = '-';
    out[pos++] = consonants[c & 15];
  }

  out[pos++] = 'x';
  out[pos] = '\0';
}

int
main(int argc, char *argv[])
{
  char    service[50];
  size_t  servicelen;
  char    token[80];
  size_t  tokenlen;
  uint8_t key[256];
  size_t  keylen;

  /* Seed the random number generator.  This ought to be unpredictable
     enough for a contest. */
  srand((int)time(NULL) * (int)getpid());

  /* Read service name. */
  {
    ssize_t len;

    len = read(0, service, sizeof(service));
    for (servicelen = 0;
         (servicelen < len) && isalnum(service[servicelen]);
         servicelen += 1);
  }

  /* Read in that service's key. */
  {
    int fd;
    int ret;

    fd = open(srv_path("token.keys/%.*s", servicelen, service), O_RDONLY);
    if (-1 == fd) {
      write(1, "!nosvc", 6);
      return 0;
    }

    ret = read(fd, &key, sizeof(key));
    if (-1 == ret) {
      write(1, "!read", 5);
      return 0;
    }
    keylen = (size_t)ret;

    close(fd);
  }

  /* Create the token. */
  {
    uint8_t crap[itokenlen];
    char    digest[bubblebabble_len(itokenlen)];
    int     i;

    /* Digest some random junk. */
    for (i = 0; i < itokenlen; i += 1) {
      crap[i] = (uint8_t)random();
    }
    bubblebabble(digest, (char *)crap, itokenlen);

    /* Append digest to service name. */
    tokenlen = (size_t)snprintf(token, sizeof(token),
                               "%.*s:%s",
                                servicelen, service, digest);
  }

  /* Write that token out now. */
  {
    int fd;
    int ret;

    do {
      fd = open(srv_path("tokens.db"), O_WRONLY | O_CREAT, 0666);
      if (-1 == fd) break;

      ret = lockf(fd, F_LOCK, 0);
      if (-1 == ret) break;

      ret = lseek(fd, 0, SEEK_END);
      if (-1 == ret) break;

      ret = write(fd, token, tokenlen);
      if (-1 == ret) break;

      ret = write(fd, "\n", 1);
      if (-1 == ret) break;

      ret = close(fd);
      if (-1 == ret) break;
    } while (0);

    if ((-1 == fd) || (-1 == ret)) {
      printf("!%s", strerror(errno));
      return 0;
    }
  }

  /* Encrypt the token. */
  {
    arc4_crypt_buffer(key, keylen, (uint8_t *)token, tokenlen);
  }

  /* Send it back.  If there's an error here, it's okay.  Better to have
     unclaimed tokens than unclaimable ones. */
  write(1, token, tokenlen);

  return 0;
}
