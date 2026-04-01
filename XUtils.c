/*
htop - StringUtils.c
(C) 2004-2011 Hisham H. Muhammad
Released under the GNU GPLv2+, see the COPYING file
in the source distribution for its full text.
*/

#include "config.h" // IWYU pragma: keep

#include "XUtils.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "CRT.h"
#include "Macros.h"


void fail(void) {
   CRT_done();
   abort();

   _exit(1); // Should never reach here
}

void* xMalloc(size_t size) {
   assert(size > 0);
   void* data = malloc(size);
   if (!data) {
      fail();
   }
   return data;
}

void* xMallocArray(size_t nmemb, size_t size) {
   assert(nmemb > 0);
   assert(size > 0);
   if (SIZE_MAX / nmemb < size) {
      fail();
   }
   return xMalloc(nmemb * size);
}

void* xCalloc(size_t nmemb, size_t size) {
   assert(nmemb > 0);
   assert(size > 0);
   if (SIZE_MAX / nmemb < size) {
      fail();
   }
   void* data = calloc(nmemb, size);
   if (!data) {
      fail();
   }
   return data;
}

void* xRealloc(void* ptr, size_t size) {
   assert(size > 0);
   void* data = realloc(ptr, size);
   if (!data) {
      /* free'ing ptr here causes an indirect memory leak if pointers
       * are held as part of an potential array referenced in ptr.
       * In GCC 14 -fanalyzer recognizes this leak, but fails to
       * ignore it given that this path ends in a noreturn function.
       * Thus to avoid this confusing diagnostic we opt to leave
       * that pointer alone instead.
       */
      // free(ptr);
      fail();
   }
   return data;
}

void* xReallocArray(void* ptr, size_t nmemb, size_t size) {
   assert(nmemb > 0);
   assert(size > 0);
   if (SIZE_MAX / nmemb < size) {
      fail();
   }
   return xRealloc(ptr, nmemb * size);
}

void* xReallocArrayZero(void* ptr, size_t prevmemb, size_t newmemb, size_t size) {
   assert((ptr == NULL) == (prevmemb == 0));

   if (prevmemb == newmemb) {
      return ptr;
   }

   void* ret = xReallocArray(ptr, newmemb, size);

   if (newmemb > prevmemb) {
      memset((unsigned char*)ret + prevmemb * size, '\0', (newmemb - prevmemb) * size);
   }

   return ret;
}

inline bool String_contains_i(const char* s1, const char* s2, bool multi) {
   // we have a multi-string search term, handle as special case for performance reasons
   if (multi && strstr(s2, "|")) {
      size_t nNeedles;
      char** needles = String_split(s2, '|', &nNeedles);
      for (size_t i = 0; i < nNeedles; i++) {
         if (strcasestr(s1, needles[i]) != NULL) {
            String_freeArray(needles);
            return true;
         }
      }
      String_freeArray(needles);
      return false;
   } else {
      return strcasestr(s1, s2) != NULL;
   }
}

char* String_cat(const char* s1, const char* s2) {
   const size_t l1 = strlen(s1);
   const size_t l2 = strlen(s2);
   if (SIZE_MAX - l1 <= l2) {
      fail();
   }
   char* out = xMalloc(l1 + l2 + 1);
   memcpy(out, s1, l1);
   memcpy(out + l1, s2, l2);
   out[l1 + l2] = '\0';
   return out;
}

char* String_trim(const char* in) {
   while (in[0] == ' ' || in[0] == '\t' || in[0] == '\n') {
      in++;
   }

   size_t len = strlen(in);
   while (len > 0 && (in[len - 1] == ' ' || in[len - 1] == '\t' || in[len - 1] == '\n')) {
      len--;
   }

   return xStrndup(in, len);
}

char** String_split(const char* s, char sep, size_t* n) {
   const size_t rate = 10;
   char** out = xCalloc(rate, sizeof(char*));
   size_t ctr = 0;
   size_t blocks = rate;
   const char* where;
   while ((where = strchr(s, sep)) != NULL) {
      size_t size = (size_t)(where - s);
      out[ctr] = xStrndup(s, size);
      ctr++;
      if (ctr == blocks) {
         blocks += rate;
         out = (char**) xRealloc(out, sizeof(char*) * blocks);
      }
      s += size + 1;
   }
   if (s[0] != '\0') {
      out[ctr] = xStrdup(s);
      ctr++;
   }
   out = xRealloc(out, sizeof(char*) * (ctr + 1));
   out[ctr] = NULL;

   if (n)
      *n = ctr;

   return out;
}

/* same as String_split() but only split on first occurrence of sep */
char** String_splitFirst(const char* s, char sep, size_t* n) {
   char** out = xCalloc(3, sizeof(char*));
   size_t ctr = 0;
   const char* where;
   if ((where = strchr(s, sep)) != NULL) {
      size_t size = (size_t)(where - s);
      out[ctr] = xStrndup(s, size);
      ctr++;
      s += size + 1;
   }
   if (s[0] != '\0') {
      out[ctr] = xStrdup(s);
      ctr++;
   }
   out[ctr] = NULL;

   if (n)
      *n = ctr;

   return out;
}

void String_freeArray(char** s) {
   if (!s) {
      return;
   }
   for (size_t i = 0; s[i] != NULL; i++) {
      free(s[i]);
   }
   free(s);
}

char* String_readLine(FILE* fp) {
   const size_t step = 1024;
   size_t bufSize = step;
   char* buffer = xMalloc(step + 1);
   char* at = buffer;
   for (;;) {
      const char* ok = fgets(at, step + 1, fp);
      if (!ok) {
         free(buffer);
         return NULL;
      }
      char* newLine = strrchr(at, '\n');
      if (newLine) {
         *newLine = '\0';
         return buffer;
      } else {
         if (feof(fp)) {
            return buffer;
         }
      }
      bufSize += step;
      buffer = xRealloc(buffer, bufSize + 1);
      at = buffer + bufSize - step;
   }
}

size_t String_safeStrncpy(char* restrict dest, const char* restrict src, size_t size) {
   assert(size > 0);

   size_t i = 0;
   for (; i < size - 1 && src[i]; i++)
      dest[i] = src[i];

   dest[i] = '\0';

   return i;
}

#ifndef HAVE_STRNLEN
size_t strnlen(const char* str, size_t maxLen) {
   for (size_t len = 0; len < maxLen; len++) {
      if (!str[len]) {
         return len;
      }
   }
   return maxLen;
}
#endif

int xAsprintf(char** strp, const char* fmt, ...) {
   *strp = NULL;

   va_list vl;
   va_start(vl, fmt);
   int r = vasprintf(strp, fmt, vl);
   va_end(vl);

   if (r < 0 || !*strp) {
      fail();
   }

   return r;
}

int xSnprintf(char* buf, size_t len, const char* fmt, ...) {
   assert(len > 0);

   // POSIX says snprintf() can fail if (len > INT_MAX).
   len = MINIMUM(INT_MAX, len);

   va_list vl;
   va_start(vl, fmt);
   int n = vsnprintf(buf, len, fmt, vl);
   va_end(vl);

   if (n < 0 || (size_t)n >= len) {
      fail();
   }

   return n;
}

char* xStrdup(const char* str) {
   char* data = strdup(str);
   if (!data) {
      fail();
   }
   return data;
}

void free_and_xStrdup(char** ptr, const char* str) {
   if (*ptr && String_eq(*ptr, str))
      return;

   free(*ptr);
   *ptr = xStrdup(str);
}

char* xStrndup(const char* str, size_t len) {
   char* data = strndup(str, len);
   if (!data) {
      fail();
   }
   return data;
}

#ifdef BUILD_STATIC
/*
 * Avoid libc NSS lookups in static builds. A statically linked glibc binary may
 * still dlopen libnss_* modules from the target system for getpwuid/getpwnam,
 * which breaks when the binary is moved across distributions with older glibc.
 * Parse /etc/passwd directly instead.
 */
static bool Compat_parsePasswdLine(char* line, char** userName, uid_t* uid, char** homeDir) {
   char* fields[7];
   char* field = line;

   for (size_t i = 0; i < ARRAYSIZE(fields) - 1; i++) {
      fields[i] = field;
      char* sep = strchr(field, ':');
      if (!sep)
         return false;
      *sep = '\0';
      field = sep + 1;
   }
   fields[ARRAYSIZE(fields) - 1] = field;

   errno = 0;
   char* end = NULL;
   unsigned long parsedUid = strtoul(fields[2], &end, 10);
   if (errno || !end || end == fields[2] || *end != '\0' || parsedUid > (unsigned long)((uid_t)-1))
      return false;

   if (userName)
      *userName = fields[0];
   if (uid)
      *uid = (uid_t)parsedUid;
   if (homeDir)
      *homeDir = fields[5];
   return true;
}

static bool Compat_lookupPasswd(const char* wantedName, uid_t wantedUid, char** foundName, char** foundHome, uid_t* foundUid) {
   FILE* fp = fopen("/etc/passwd", "r");
   if (!fp)
      return false;

   bool found = false;
   char* line = NULL;
   while ((line = String_readLine(fp))) {
      char* userName = NULL;
      char* homeDir = NULL;
      uid_t uid = (uid_t)-1;

      if (line[0] == '\0' || line[0] == '#')
         goto next;

      if (!Compat_parsePasswdLine(line, &userName, &uid, &homeDir))
         goto next;

      if (wantedName) {
         if (!String_eq(userName, wantedName))
            goto next;
      } else if (uid != wantedUid) {
         goto next;
      }

      if (foundName)
         *foundName = xStrdup(userName);
      if (foundHome)
         *foundHome = xStrdup(homeDir);
      if (foundUid)
         *foundUid = uid;
      found = true;

next:
      free(line);
      if (found)
         break;
   }

   fclose(fp);
   return found;
}
#endif

bool Compat_getUserName(uid_t uid, char** name) {
   assert(name);
   *name = NULL;

#ifdef BUILD_STATIC
   return Compat_lookupPasswd(NULL, uid, name, NULL, NULL);
#else
   const struct passwd* user = getpwuid(uid);
   if (!user || !user->pw_name)
      return false;

   *name = xStrdup(user->pw_name);
   return true;
#endif
}

bool Compat_getUserHome(uid_t uid, char** home) {
   assert(home);
   *home = NULL;

#ifdef BUILD_STATIC
   return Compat_lookupPasswd(NULL, uid, NULL, home, NULL);
#else
   const struct passwd* user = getpwuid(uid);
   if (!user || !user->pw_dir)
      return false;

   *home = xStrdup(user->pw_dir);
   return true;
#endif
}

bool Compat_getUidForUser(const char* userName, uid_t* uid) {
   assert(userName);
   assert(uid);
   *uid = (uid_t)-1;

#ifdef BUILD_STATIC
   return Compat_lookupPasswd(userName, (uid_t)-1, NULL, NULL, uid);
#else
   const struct passwd* user = getpwnam(userName);
   if (!user)
      return false;

   *uid = user->pw_uid;
   return true;
#endif
}

ssize_t full_write(int fd, const void* buf, size_t count) {
   ssize_t written = 0;

   while (count > 0) {
      ssize_t r = write(fd, buf, count);
      if (r < 0) {
         if (errno == EINTR)
            continue;

         return r;
      }

      if (r == 0)
         break;

      written += r;
      buf = (const unsigned char*)buf + r;
      count -= (size_t)r;
   }

   return written;
}

/* Compares floating point values for ordering data entries. In this function,
   NaN is considered "less than" any other floating point value (regardless of
   sign), and two NaNs are considered "equal" regardless of payload. */
int compareRealNumbers(double a, double b) {
   int result = isgreater(a, b) - isgreater(b, a);
   if (result)
      return result;
   return !isNaN(a) - !isNaN(b);
}

/* Computes the sum of all positive floating point values in an array.
   NaN values in the array are skipped. The returned sum will always be
   nonnegative. */
double sumPositiveValues(const double* array, size_t count) {
   double sum = 0.0;
   for (size_t i = 0; i < count; i++) {
      if (isPositive(array[i]))
         sum += array[i];
   }
   return sum;
}

/* Counts the number of digits needed to print "n" with a given base.
   If "n" is zero, returns 1. This function expects small numbers to
   appear often, hence it uses a O(log(n)) time algorithm. */
size_t countDigits(size_t n, size_t base) {
   assert(base > 1);
   size_t res = 1;
   for (size_t limit = base; n >= limit; limit *= base) {
      res++;
      if (base && limit > SIZE_MAX / base) {
         break;
      }
   }
   return res;
}

#if !defined(HAVE_BUILTIN_CTZ)
// map a bit value mod 37 to its position
static const uint8_t mod37BitPosition[] = {
  32, 0, 1, 26, 2, 23, 27, 0, 3, 16, 24, 30, 28, 11, 0, 13, 4,
  7, 17, 0, 25, 22, 31, 15, 29, 10, 12, 6, 0, 21, 14, 9, 5,
  20, 8, 19, 18
};

/* Returns the number of trailing zero bits */
unsigned int countTrailingZeros(unsigned int x) {
   return mod37BitPosition[(-x & x) % 37];
}
#endif
