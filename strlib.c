/*
** strlib.c
** String library to LUA
*/

char *rcs_strlib="$Id: strlib.c,v 1.39 1997/04/04 22:24:51 roberto Exp roberto $";

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "lua.h"
#include "auxlib.h"
#include "lualib.h"


struct lbuff {
  char *b;
  size_t max;
  size_t size;
};

static struct lbuff lbuffer = {NULL, 0, 0};


static char *luaL_strbuffer (unsigned long size)
{
  if (size > lbuffer.max) {
    /* ANSI "realloc" doesn't need this test, but some machines (Sun!)
       don't follow ANSI */
    lbuffer.b = (lbuffer.b) ? realloc(lbuffer.b, lbuffer.max=size) :
                              malloc(lbuffer.max=size);
    if (lbuffer.b == NULL)
      lua_error("memory overflow");
  }
  return lbuffer.b;
}

static char *openspace (unsigned long size)
{
  char *buff = luaL_strbuffer(lbuffer.size+size);
  return buff+lbuffer.size;
}

char *luaI_addchar (int c)
{
  if (lbuffer.size >= lbuffer.max)
    luaL_strbuffer(lbuffer.max == 0 ? 100 : lbuffer.max*2);
  lbuffer.b[lbuffer.size++] = c;
  return lbuffer.b;
}

void luaI_emptybuff (void)
{
  lbuffer.size = 0;  /* prepare for next string */
}


static void addnchar (char *s, int n)
{
  char *b = openspace(n);
  strncpy(b, s, n);
  lbuffer.size += n;
}

static void addstr (char *s)
{
  addnchar(s, strlen(s));
}

/*
** Interface to strtok
*/
static void str_tok (void)
{
  char *s1 = luaL_check_string(1);
  char *del = luaL_check_string(2);
  lua_Object t = lua_createtable();
  int i = 1;
  /* As strtok changes s1, and s1 is "constant",  make a copy of it */
  s1 = strcpy(luaL_strbuffer(strlen(s1+1)), s1);
  while ((s1 = strtok(s1, del)) != NULL) {
    lua_pushobject(t);
    lua_pushnumber(i++);
    lua_pushstring(s1);
    lua_settable();
    s1 = NULL;  /* prepare for next strtok */
  }
  lua_pushobject(t);
  lua_pushnumber(i-1);  /* total number of tokens */
}


/*
** Return the string length
*/
static void str_len (void)
{
 lua_pushnumber(strlen(luaL_check_string(1)));
}

/*
** Return the substring of a string
*/
static void str_sub (void)
{
  char *s = luaL_check_string(1);
  long start = (long)luaL_check_number(2);
  long end = (long)luaL_opt_number(3, strlen(s));
  if (1 <= start && start <= end && end <= strlen(s)) {
    luaI_emptybuff();
    addnchar(s+start-1, end-start+1);
    lua_pushstring(luaI_addchar(0));
  }
  else lua_pushliteral("");
}

/*
** Convert a string to lower case.
*/
static void str_lower (void)
{
  char *s;
  luaI_emptybuff();
  for (s = luaL_check_string(1); *s; s++)
    luaI_addchar(tolower((unsigned char)*s));
  lua_pushstring(luaI_addchar(0));
}

/*
** Convert a string to upper case.
*/
static void str_upper (void)
{
  char *s;
  luaI_emptybuff();
  for (s = luaL_check_string(1); *s; s++)
    luaI_addchar(toupper((unsigned char)*s));
  lua_pushstring(luaI_addchar(0));
}

static void str_rep (void)
{
  char *s = luaL_check_string(1);
  int n = (int)luaL_check_number(2);
  luaI_emptybuff();
  while (n-- > 0)
    addstr(s);
  lua_pushstring(luaI_addchar(0));
}

/*
** get ascii value of a character in a string
*/
static void str_ascii (void)
{
  char *s = luaL_check_string(1);
  long pos = (long)luaL_opt_number(2, 1) - 1;
  luaL_arg_check(0<=pos && pos<strlen(s), 2,  "out of range");
  lua_pushnumber((unsigned char)s[pos]);
}


/* pattern matching */

#define ESC	'%'
#define SPECIALS  "^$*?.([%-"

static char *bracket_end (char *p)
{
  return (*p == 0) ? NULL : strchr((*p=='^') ? p+2 : p+1, ']');
}

char *luaL_item_end (char *p)
{
  switch (*p++) {
    case '\0': return p-1;
    case ESC:
      if (*p == 0) luaL_verror("incorrect pattern (ends with `%c')", ESC);
      return p+1;
    case '[': {
      char *end = bracket_end(p);
      if (end == NULL) lua_error("incorrect pattern (missing `]')");
      return end+1;
    }
    default:
      return p;
  }
}

static int matchclass (int c, int cl)
{
  int res;
  switch (tolower((unsigned char)cl)) {
    case 'a' : res = isalpha((unsigned char)c); break;
    case 'c' : res = iscntrl((unsigned char)c); break;
    case 'd' : res = isdigit((unsigned char)c); break;
    case 'l' : res = islower((unsigned char)c); break;
    case 'p' : res = ispunct((unsigned char)c); break;
    case 's' : res = isspace((unsigned char)c); break;
    case 'u' : res = isupper((unsigned char)c); break;
    case 'w' : res = isalnum((unsigned char)c); break;
    default: return (cl == c);
  }
  return (islower((unsigned char)cl) ? res : !res);
}

int luaL_singlematch (int c, char *p)
{
  if (c == 0) return 0;
  switch (*p) {
    case '.': return 1;
    case ESC: return matchclass(c, *(p+1));
    case '[': {
      char *end = bracket_end(p+1);
      int sig = *(p+1) == '^' ? (p++, 0) : 1;
      while (++p < end) {
        if (*p == ESC) {
          if (((p+1) < end) && matchclass(c, *++p)) return sig;
        }
        else if ((*(p+1) == '-') && (p+2 < end)) {
          p+=2;
          if (*(p-2) <= c && c <= *p) return sig;
        }
        else if (*p == c) return sig;
      }
      return !sig;
    }
    default: return (*p == c);
  }
}

#define MAX_CAPT 9

static struct {
  char *init;
  int len;  /* -1 signals unfinished capture */
} capture[MAX_CAPT];

static int num_captures;  /* only valid after a sucessful call to match */


static void push_captures (void)
{
  int i;
  for (i=0; i<num_captures; i++) {
    int l = capture[i].len;
    char *buff = openspace(l+1);
    if (l == -1) lua_error("unfinished capture");
    strncpy(buff, capture[i].init, l);
    buff[l] = 0;
    lua_pushstring(buff);
  }
}

static int check_cap (int l, int level)
{
  l -= '1';
  if (!(0 <= l && l < level && capture[l].len != -1))
    lua_error("invalid capture index");
  return l;
}

static int capture_to_close (int level)
{
  for (level--; level>=0; level--)
    if (capture[level].len == -1) return level;
  lua_error("invalid pattern capture");
  return 0;  /* to avoid warnings */
}

static char *matchbalance (char *s, int b, int e)
{
  if (*s != b) return NULL;
  else {
    int cont = 1;
    while (*(++s)) {
      if (*s == e) {
        if (--cont == 0) return s+1;
      }
      else if (*s == b) cont++;
    }
  }
  return NULL;  /* string ends out of balance */
}

static char *match (char *s, char *p, int level)
{
  init: /* using goto's to optimize tail recursion */
  switch (*p) {
    case '(':  /* start capture */
      if (level >= MAX_CAPT) lua_error("too many captures");
      capture[level].init = s;
      capture[level].len = -1;
      level++; p++; goto init;  /* return match(s, p+1, level); */
    case ')': {  /* end capture */
      int l = capture_to_close(level);
      char *res;
      capture[l].len = s - capture[l].init;  /* close capture */
      if ((res = match(s, p+1, level)) == NULL) /* match failed? */
        capture[l].len = -1;  /* undo capture */
      return res;
    }
    case ESC:
      if (isdigit((unsigned char)*(p+1))) {  /* capture */
        int l = check_cap(*(p+1), level);
        if (strncmp(capture[l].init, s, capture[l].len) == 0) {
          /* return match(p+2, s+capture[l].len, level); */
          p+=2; s+=capture[l].len; goto init;
        }
        else return NULL;
      }
      else if (*(p+1) == 'b') {  /* balanced string */
        if (*(p+2) == 0 || *(p+3) == 0)
          lua_error("bad balanced pattern specification");
        s = matchbalance(s, *(p+2), *(p+3));
        if (s == NULL) return NULL;
        else {  /* return match(p+4, s, level); */
          p+=4; goto init;
        }
      }
      else goto dflt;
    case '\0': case '$':  /* (possibly) end of pattern */
      if (*p == 0 || (*(p+1) == 0 && *s == 0)) {
        num_captures = level;
        return s;
      }
      else goto dflt;
    default: dflt: {  /* it is a pattern item */
      int m = luaL_singlematch(*s, p);
      char *ep = luaL_item_end(p);  /* get what is next */
      switch (*ep) {
        case '*': {  /* repetition */
          char *res;
          if (m && (res = match(s+1, p, level)))
            return res;
          p=ep+1; goto init;  /* else return match(s, ep+1, level); */
        }
        case '-': {  /* repetition */
          char *res;
          if ((res = match(s, ep+1, level)) != 0)
            return res;
          else if (m) {
            s++;
            goto init;  /* return match(s+1, p, level); */
          }
          else
            return NULL;
        }
        case '?': {  /* optional */
          char *res;
          if (m && (res = match(s+1, ep+1, level)))
            return res;
          p=ep+1; goto init;  /* else return match(s, ep+1, level); */
        }
        default:
          if (m) { s++; p=ep; goto init; }  /* return match(s+1, ep, level); */
          else return NULL;
      }
    }
  }
}

static void str_find (void)
{
  char *s = luaL_check_string(1);
  char *p = luaL_check_string(2);
  long init = (long)luaL_opt_number(3, 1) - 1;
  luaL_arg_check(0 <= init && init <= strlen(s), 3, "out of range");
  if (lua_getparam(4) != LUA_NOOBJECT || 
      strpbrk(p, SPECIALS) == NULL) {  /* no special caracters? */
    char *s2 = strstr(s+init, p);
    if (s2) {
      lua_pushnumber(s2-s+1);
      lua_pushnumber(s2-s+strlen(p));
    }
  }
  else {
    int anchor = (*p == '^') ? (p++, 1) : 0;
    char *s1=s+init;
    do {
      char *res;
      if ((res=match(s1, p, 0)) != NULL) {
        lua_pushnumber(s1-s+1);  /* start */
        lua_pushnumber(res-s);   /* end */
        push_captures();
        return;
      }
    } while (*s1++ && !anchor);
  }
}

static void add_s (lua_Object newp)
{
  if (lua_isstring(newp)) {
    char *news = lua_getstring(newp);
    while (*news) {
      if (*news != ESC || !isdigit((unsigned char)*++news))
        luaI_addchar(*news++);
      else {
        int l = check_cap(*news++, num_captures);
        addnchar(capture[l].init, capture[l].len);
      }
    }
  }
  else if (lua_isfunction(newp)) {
    lua_Object res;
    struct lbuff oldbuff;
    lua_beginblock();
    push_captures();
    /* function may use lbuffer, so save it and create a new one */
    oldbuff = lbuffer;
    lbuffer.b = NULL; lbuffer.max = lbuffer.size = 0;
    lua_callfunction(newp);
    /* restore old buffer */
    free(lbuffer.b);
    lbuffer = oldbuff;
    res = lua_getresult(1);
    addstr(lua_isstring(res) ? lua_getstring(res) : "");
    lua_endblock();
  }
  else luaL_arg_check(0, 3, NULL);
}

static void str_gsub (void)
{
  char *src = luaL_check_string(1);
  char *p = luaL_check_string(2);
  lua_Object newp = lua_getparam(3);
  int max_s = (int)luaL_opt_number(4, strlen(src)+1);
  int anchor = (*p == '^') ? (p++, 1) : 0;
  int n = 0;
  luaI_emptybuff();
  while (n < max_s) {
    char *e = match(src, p, 0);
    if (e) {
      add_s(newp);
      n++;
    }
    if (e && e>src) /* non empty match? */
      src = e;  /* skip it */
    else if (*src)
      luaI_addchar(*src++);
    else break;
    if (anchor) break;
  }
  addstr(src);
  lua_pushstring(luaI_addchar(0));
  lua_pushnumber(n);  /* number of substitutions */
}

static void str_set (void)
{
  char *item = luaL_check_string(1);
  int i;
  luaL_arg_check(*luaL_item_end(item) == 0, 1, "wrong format");
  luaI_emptybuff();
  for (i=1; i<256; i++)  /* 0 cannot be part of a set */
    if (luaL_singlematch(i, item))
      luaI_addchar(i);
  lua_pushstring(luaI_addchar(0));
}


void luaI_addquoted (char *s)
{
  luaI_addchar('"');
  for (; *s; s++) {
    if (strchr("\"\\\n", *s))
      luaI_addchar('\\');
    luaI_addchar(*s);
  }
  luaI_addchar('"');
}

#define MAX_FORMAT 200

static void str_format (void)
{
  int arg = 1;
  char *strfrmt = luaL_check_string(arg++);
  luaI_emptybuff();  /* initialize */
  while (*strfrmt) {
    if (*strfrmt != '%')
      luaI_addchar(*strfrmt++);
    else if (*++strfrmt == '%')
      luaI_addchar(*strfrmt++);  /* %% */
    else { /* format item */
      char form[MAX_FORMAT];      /* store the format ('%...') */
      char *buff;
      char *initf = strfrmt-1;  /* -1 to include % */
      strfrmt = match(strfrmt, "[-+ #]*(%d*)%.?(%d*)", 0);
      if (capture[0].len > 3 || capture[1].len > 3)  /* < 1000? */
        lua_error("invalid format (width or precision too long)");
      strncpy(form, initf, strfrmt-initf+1); /* +1 to include convertion */
      form[strfrmt-initf+1] = 0;
      buff = openspace(1000);  /* to store the formated value */
      switch (*strfrmt++) {
        case 'q':
          luaI_addquoted(luaL_check_string(arg++));
          continue;
        case 's': {
          char *s = luaL_check_string(arg++);
          buff = openspace(strlen(s));
          sprintf(buff, form, s);
          break;
        }
        case 'c':  case 'd':  case 'i': case 'o':
        case 'u':  case 'x':  case 'X':
          sprintf(buff, form, (int)luaL_check_number(arg++));
          break;
        case 'e':  case 'E': case 'f': case 'g':
          sprintf(buff, form, luaL_check_number(arg++));
          break;
        default:  /* also treat cases 'pnLlh' */
          lua_error("invalid format option in function `format'");
      }
      lbuffer.size += strlen(buff);
    }
  }
  lua_pushstring(luaI_addchar(0));  /* push the result */
}


static struct luaL_reg strlib[] = {
{"strtok", str_tok},
{"strlen", str_len},
{"strsub", str_sub},
{"strset", str_set},
{"strlower", str_lower},
{"strupper", str_upper},
{"strrep", str_rep},
{"ascii", str_ascii},
{"format", str_format},
{"strfind", str_find},
{"gsub", str_gsub}
};


/*
** Open string library
*/
void strlib_open (void)
{
  luaL_openlib(strlib, (sizeof(strlib)/sizeof(strlib[0])));
}
