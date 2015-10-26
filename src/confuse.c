/*
 * Copyright (c) 2002,2003,2007 Martin Hedenfalk <martin@bzero.se>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#ifndef _WIN32
# include <pwd.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <ctype.h>

#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
# ifndef S_ISREG
#  define S_ISREG(mode) ((mode) & S_IFREG)
# endif
#endif

#include "compat.h"
#include "confuse.h"

#define is_set(f, x) (((f) & (x)) == (f))

#if defined(ENABLE_NLS) && defined(HAVE_GETTEXT)
# include <locale.h>
# include <libintl.h>
# define _(str) dgettext(PACKAGE, str)
#else
# define _(str) str
#endif
#define N_(str) str

extern FILE *cfg_yyin;
extern int cfg_yylex(cfg_t *cfg);
extern int cfg_lexer_include(cfg_t *cfg, const char *fname);
extern void cfg_scan_string_begin(const char *buf);
extern void cfg_scan_string_end(void);
extern void cfg_scan_fp_begin(FILE *fp);
extern void cfg_scan_fp_end(void);
extern char *cfg_qstring;

char *cfg_yylval = 0;

const char confuse_version[] = PACKAGE_VERSION;
const char confuse_copyright[] = PACKAGE_STRING " by Martin Hedenfalk <martin@bzero.se>";
const char confuse_author[] = "Martin Hedenfalk <martin@bzero.se>";

static int cfg_parse_internal(cfg_t *cfg, int level, int force_state, cfg_opt_t *force_opt);

#define STATE_CONTINUE 0
#define STATE_EOF -1
#define STATE_ERROR 1

#ifndef HAVE_STRDUP
# ifdef HAVE__STRDUP
#  define strdup _strdup
# else
/*
 * Copyright (c) 1988, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
static char *strdup(const char *s)
{
	size_t siz;
	char *copy;

	siz = strlen(str) + 1;
	copy = malloc(siz);
	if (!copy)
		return NULL;

	memcpy(copy, str, siz);

	return copy;
}
# endif
#endif

#ifndef HAVE_STRNDUP
static char *strndup(const char *s, size_t n)
{
	char *r;

	if (s == 0)
		return NULL;

	r = malloc(n + 1);
	if (!r)
		return NULL;

	strncpy(r, s, n);
	r[n] = 0;

	return r;
}
#endif

#ifndef HAVE_STRCASECMP
int strcasecmp(const char *s1, const char *s2)
{
	assert(s1);
	assert(s2);

	while (*s1) {
		int c1 = tolower(*(const unsigned char *)s1);
		int c2 = tolower(*(const unsigned char *)s2);

		if (c1 < c2)
			return -1;
		if (c1 > c2)
			return +1;

		++s1;
		++s2;
	}

	if (*s2 != 0)
		return -1;

	return 0;
}
#endif

DLLIMPORT cfg_opt_t *cfg_getopt(cfg_t *cfg, const char *name)
{
	unsigned int i;
	cfg_t *sec = cfg;

	assert(cfg && cfg->name && name);

	while (name && *name) {
		char *secname;
		size_t len = strcspn(name, "|");

		if (name[len] == 0 /*len == strlen(name) */ )
			/* no more subsections */
			break;
		if (len) {
			secname = strndup(name, len);
			if (!secname)
				return NULL;

			sec = cfg_getsec(sec, secname);
			if (sec == 0) {
				cfg_error(cfg, _("no such option '%s'"), secname);
				free(secname);
				return NULL;
			}

			free(secname);
		}
		name += len;
		name += strspn(name, "|");
	}

	for (i = 0; sec->opts[i].name; i++) {
		if (is_set(CFGF_NOCASE, sec->flags)) {
			if (strcasecmp(sec->opts[i].name, name) == 0)
				return &sec->opts[i];
		} else {
			if (strcmp(sec->opts[i].name, name) == 0)
				return &sec->opts[i];
		}
	}

	cfg_error(cfg, _("no such option '%s'"), name);

	return NULL;
}

DLLIMPORT const char *cfg_title(cfg_t *cfg)
{
	if (cfg)
		return cfg->title;
	return NULL;
}

DLLIMPORT const char *cfg_name(cfg_t *cfg)
{
	if (cfg)
		return cfg->name;
	return NULL;
}

DLLIMPORT const char *cfg_opt_name(cfg_opt_t *opt)
{
	if (opt)
		return opt->name;
	return NULL;
}

DLLIMPORT unsigned int cfg_opt_size(cfg_opt_t *opt)
{
	if (opt)
		return opt->nvalues;
	return 0;
}

DLLIMPORT unsigned int cfg_size(cfg_t *cfg, const char *name)
{
	return cfg_opt_size(cfg_getopt(cfg, name));
}

DLLIMPORT signed long cfg_opt_getnint(cfg_opt_t *opt, unsigned int index)
{
	assert(opt && opt->type == CFGT_INT);

	if (opt->values && index < opt->nvalues)
		return opt->values[index]->number;
	if (opt->simple_value.number)
		return *opt->simple_value.number;

	return 0;
}

DLLIMPORT signed long cfg_getnint(cfg_t *cfg, const char *name, unsigned int index)
{
	return cfg_opt_getnint(cfg_getopt(cfg, name), index);
}

DLLIMPORT signed long cfg_getint(cfg_t *cfg, const char *name)
{
	return cfg_getnint(cfg, name, 0);
}

DLLIMPORT double cfg_opt_getnfloat(cfg_opt_t *opt, unsigned int index)
{
	assert(opt && opt->type == CFGT_FLOAT);

	if (opt->values && index < opt->nvalues)
		return opt->values[index]->fpnumber;
	if (opt->simple_value.fpnumber)
		return *opt->simple_value.fpnumber;

	return 0;
}

DLLIMPORT double cfg_getnfloat(cfg_t *cfg, const char *name, unsigned int index)
{
	return cfg_opt_getnfloat(cfg_getopt(cfg, name), index);
}

DLLIMPORT double cfg_getfloat(cfg_t *cfg, const char *name)
{
	return cfg_getnfloat(cfg, name, 0);
}

DLLIMPORT cfg_bool_t cfg_opt_getnbool(cfg_opt_t *opt, unsigned int index)
{
	assert(opt && opt->type == CFGT_BOOL);

	if (opt->values && index < opt->nvalues)
		return opt->values[index]->boolean;
	if (opt->simple_value.boolean)
		return *opt->simple_value.boolean;

	return cfg_false;
}

DLLIMPORT cfg_bool_t cfg_getnbool(cfg_t *cfg, const char *name, unsigned int index)
{
	return cfg_opt_getnbool(cfg_getopt(cfg, name), index);
}

DLLIMPORT cfg_bool_t cfg_getbool(cfg_t *cfg, const char *name)
{
	return cfg_getnbool(cfg, name, 0);
}

DLLIMPORT char *cfg_opt_getnstr(cfg_opt_t *opt, unsigned int index)
{
	assert(opt && opt->type == CFGT_STR);

	if (opt->values && index < opt->nvalues)
		return opt->values[index]->string;
	if (opt->simple_value.string)
		return *opt->simple_value.string;

	return NULL;
}

DLLIMPORT char *cfg_getnstr(cfg_t *cfg, const char *name, unsigned int index)
{
	return cfg_opt_getnstr(cfg_getopt(cfg, name), index);
}

DLLIMPORT char *cfg_getstr(cfg_t *cfg, const char *name)
{
	return cfg_getnstr(cfg, name, 0);
}

DLLIMPORT void *cfg_opt_getnptr(cfg_opt_t *opt, unsigned int index)
{
	assert(opt && opt->type == CFGT_PTR);

	if (opt->values && index < opt->nvalues)
		return opt->values[index]->ptr;
	if (opt->simple_value.ptr)
		return *opt->simple_value.ptr;

	return NULL;
}

DLLIMPORT void *cfg_getnptr(cfg_t *cfg, const char *name, unsigned int index)
{
	return cfg_opt_getnptr(cfg_getopt(cfg, name), index);
}

DLLIMPORT void *cfg_getptr(cfg_t *cfg, const char *name)
{
	return cfg_getnptr(cfg, name, 0);
}

DLLIMPORT cfg_t *cfg_opt_getnsec(cfg_opt_t *opt, unsigned int index)
{
	assert(opt && opt->type == CFGT_SEC);

	if (opt->values && index < opt->nvalues)
		return opt->values[index]->section;

	return NULL;
}

DLLIMPORT cfg_t *cfg_getnsec(cfg_t *cfg, const char *name, unsigned int index)
{
	return cfg_opt_getnsec(cfg_getopt(cfg, name), index);
}

DLLIMPORT cfg_t *cfg_opt_gettsec(cfg_opt_t *opt, const char *title)
{
	unsigned int i, n;

	assert(opt && title);
	if (!is_set(CFGF_TITLE, opt->flags))
		return NULL;

	n = cfg_opt_size(opt);
	for (i = 0; i < n; i++) {
		cfg_t *sec = cfg_opt_getnsec(opt, i);

		assert(sec && sec->title);
		if (is_set(CFGF_NOCASE, opt->flags)) {
			if (strcasecmp(title, sec->title) == 0)
				return sec;
		} else {
			if (strcmp(title, sec->title) == 0)
				return sec;
		}
	}

	return NULL;
}

DLLIMPORT cfg_t *cfg_gettsec(cfg_t *cfg, const char *name, const char *title)
{
	return cfg_opt_gettsec(cfg_getopt(cfg, name), title);
}

DLLIMPORT cfg_t *cfg_getsec(cfg_t *cfg, const char *name)
{
	return cfg_getnsec(cfg, name, 0);
}

static cfg_value_t *cfg_addval(cfg_opt_t *opt)
{
	void *ptr;

	ptr = realloc(opt->values, (opt->nvalues + 1) * sizeof(cfg_value_t *));
	if (!ptr)
		return NULL;

	opt->values = ptr;
	opt->values[opt->nvalues] = calloc(1, sizeof(cfg_value_t));
	if (!opt->values[opt->nvalues])
		return NULL;

	return opt->values[opt->nvalues++];
}

DLLIMPORT int cfg_numopts(cfg_opt_t *opts)
{
	int n;

	for (n = 0; opts[n].name; n++)
		/* do nothing */ ;
	return n;
}

static cfg_opt_t *cfg_dupopt_array(cfg_opt_t *opts)
{
	int i;
	cfg_opt_t *dupopts;
	int n = cfg_numopts(opts);

	dupopts = calloc(n + 1, sizeof(cfg_opt_t));
	if (!dupopts)
		return NULL;

	memcpy(dupopts, opts, n * sizeof(cfg_opt_t));

	for (i = 0; i < n; i++) {
		dupopts[i].name = strdup(opts[i].name);
		if (opts[i].type == CFGT_SEC && opts[i].subopts)
			dupopts[i].subopts = cfg_dupopt_array(opts[i].subopts);

		if (is_set(CFGF_LIST, opts[i].flags) || opts[i].type == CFGT_FUNC)
			dupopts[i].def.parsed = opts[i].def.parsed ? strdup(opts[i].def.parsed) : 0;
		else if (opts[i].type == CFGT_STR)
			dupopts[i].def.string = opts[i].def.string ? strdup(opts[i].def.string) : 0;
	}

	return dupopts;
}

DLLIMPORT int cfg_parse_boolean(const char *s)
{
	if (strcasecmp(s, "true") == 0 || strcasecmp(s, "on") == 0 || strcasecmp(s, "yes") == 0)
		return 1;
	if (strcasecmp(s, "false") == 0 || strcasecmp(s, "off") == 0 || strcasecmp(s, "no") == 0)
		return 0;

	return -1;
}

static void cfg_init_defaults(cfg_t *cfg)
{
	int i;

	for (i = 0; cfg->opts[i].name; i++) {
		/* libConfuse doesn't handle default values for "simple" options */
		if (cfg->opts[i].simple_value.ptr || is_set(CFGF_NODEFAULT, cfg->opts[i].flags))
			continue;

		if (cfg->opts[i].type != CFGT_SEC) {
			cfg->opts[i].flags |= CFGF_DEFINIT;

			if (is_set(CFGF_LIST, cfg->opts[i].flags) || cfg->opts[i].def.parsed) {
				int xstate, ret;

				/* If it's a list, but no default value was given,
				 * keep the option uninitialized.
				 */
				if (cfg->opts[i].def.parsed == 0 || cfg->opts[i].def.parsed[0] == 0)
					continue;

				/* setup scanning from the string specified for the
				 * "default" value, force the correct state and option
				 */

				if (is_set(CFGF_LIST, cfg->opts[i].flags))
					/* lists must be surrounded by {braces} */
					xstate = 3;
				else if (cfg->opts[i].type == CFGT_FUNC)
					xstate = 0;
				else
					xstate = 2;

				cfg_scan_string_begin(cfg->opts[i].def.parsed);
				do {
					ret = cfg_parse_internal(cfg, 1, xstate, &cfg->opts[i]);
					xstate = -1;
				} while (ret == STATE_CONTINUE);
				cfg_scan_string_end();
				if (ret == STATE_ERROR) {
					/*
					 * If there was an error parsing the default string,
					 * the initialization of the default value could be
					 * inconsistent or empty. What to do? It's a
					 * programming error and not an end user input
					 * error. Lets print a message and abort...
					 */
					fprintf(stderr, "Parse error in default value '%s'"
						" for option '%s'\n", cfg->opts[i].def.parsed, cfg->opts[i].name);
					fprintf(stderr, "Check your initialization macros and the" " libConfuse documentation\n");
					abort();
				}
			} else {
				switch (cfg->opts[i].type) {
				case CFGT_INT:
					cfg_opt_setnint(&cfg->opts[i], cfg->opts[i].def.number, 0);
					break;
				case CFGT_FLOAT:
					cfg_opt_setnfloat(&cfg->opts[i], cfg->opts[i].def.fpnumber, 0);
					break;
				case CFGT_BOOL:
					cfg_opt_setnbool(&cfg->opts[i], cfg->opts[i].def.boolean, 0);
					break;
				case CFGT_STR:
					cfg_opt_setnstr(&cfg->opts[i], cfg->opts[i].def.string, 0);
					break;
				case CFGT_FUNC:
				case CFGT_PTR:
					break;
				default:
					cfg_error(cfg, "internal error in cfg_init_defaults(%s)", cfg->opts[i].name);
					break;
				}
			}

			/* The default value should only be returned if no value
			 * is given in the configuration file, so we set the RESET
			 * flag here. When/If cfg_setopt() is called, the value(s)
			 * will be freed and the flag unset.
			 */
			cfg->opts[i].flags |= CFGF_RESET;
		} /* end if cfg->opts[i].type != CFGT_SEC */
		else if (!is_set(CFGF_MULTI, cfg->opts[i].flags)) {
			cfg_setopt(cfg, &cfg->opts[i], 0);
			cfg->opts[i].flags |= CFGF_DEFINIT;
		}
	}
}

DLLIMPORT cfg_value_t *cfg_setopt(cfg_t *cfg, cfg_opt_t *opt, const char *value)
{
	cfg_value_t *val = NULL;
	int b;
	const char *s;
	double f;
	long int i;
	void *p;
	char *endptr;

	assert(cfg && opt);

	if (opt->simple_value.ptr) {
		assert(opt->type != CFGT_SEC);
		val = (cfg_value_t *)opt->simple_value.ptr;
	} else {
		if (is_set(CFGF_RESET, opt->flags)) {
			cfg_free_value(opt);
			opt->flags &= ~CFGF_RESET;
		}

		if (opt->nvalues == 0 || is_set(CFGF_MULTI, opt->flags) || is_set(CFGF_LIST, opt->flags)) {
			val = NULL;

			if (opt->type == CFGT_SEC && is_set(CFGF_TITLE, opt->flags)) {
				unsigned int i;

				/* Check if there already is a section with the same title.
				 */

				/* Assert that there are either no sections at all, or a
				 * non-NULL section title. */
				assert(opt->nvalues == 0 || value);

				for (i = 0; i < opt->nvalues && val == NULL; i++) {
					cfg_t *sec = opt->values[i]->section;

					if (is_set(CFGF_NOCASE, cfg->flags)) {
						if (strcasecmp(value, sec->title) == 0)
							val = opt->values[i];
					} else {
						if (strcmp(value, sec->title) == 0)
							val = opt->values[i];
					}
				}

				if (val && is_set(CFGF_NO_TITLE_DUPES, opt->flags)) {
					cfg_error(cfg, _("found duplicate title '%s'"), value);
					return NULL;
				}
			}

			if (!val) {
				val = cfg_addval(opt);
				if (!val)
					return NULL;
			}
		} else
			val = opt->values[0];
	}

	switch (opt->type) {
	case CFGT_INT:
		if (opt->parsecb) {
			if ((*opt->parsecb) (cfg, opt, value, &i) != 0)
				return NULL;
		} else {
			i = strtol(value, &endptr, 0);
			if (*endptr != '\0') {
				cfg_error(cfg, _("invalid integer value for option '%s'"), opt->name);
				return NULL;
			}
			if (errno == ERANGE) {
				cfg_error(cfg, _("integer value for option '%s' is out of range"), opt->name);
				return NULL;
			}
		}
		val->number = i;
		break;

	case CFGT_FLOAT:
		if (opt->parsecb) {
			if ((*opt->parsecb) (cfg, opt, value, &f) != 0)
				return NULL;
		} else {
			f = strtod(value, &endptr);
			if (*endptr != '\0') {
				cfg_error(cfg, _("invalid floating point value for option '%s'"), opt->name);
				return NULL;
			}
			if (errno == ERANGE) {
				cfg_error(cfg, _("floating point value for option '%s' is out of range"), opt->name);
				return NULL;
			}
		}
		val->fpnumber = f;
		break;

	case CFGT_STR:
		if (opt->parsecb) {
			s = 0;
			if ((*opt->parsecb) (cfg, opt, value, &s) != 0)
				return NULL;
		} else {
			s = value;
		}

		free(val->string);
		val->string = strdup(s);
		if (!val->string)
			return NULL;
		break;

	case CFGT_SEC:
		if (is_set(CFGF_MULTI, opt->flags) || val->section == 0) {
			if (val->section)
				val->section->path = 0;
			cfg_free(val->section);
			val->section = calloc(1, sizeof(cfg_t));
			if (!val->section)
				return NULL;

			val->section->name = strdup(opt->name);
			if (!val->section->name) {
				free(val->section);
				return NULL;
			}

			val->section->flags = cfg->flags;
			val->section->filename = cfg->filename ? strdup(cfg->filename) : NULL;
			if (cfg->filename && !val->section->filename) {
				free(val->section->name);
				free(val->section);
				return NULL;
			}

			val->section->line = cfg->line;
			val->section->errfunc = cfg->errfunc;
			val->section->title = value ? strdup(value) : NULL;
			if (value && !val->section->title) {
				free(val->section->filename);
				free(val->section->name);
				free(val->section);
				return NULL;
			}

			val->section->opts = cfg_dupopt_array(opt->subopts);
			if (!val->section->opts) {
				if (val->section->title)
					free(val->section->title);
				if (val->section->filename)
					free(val->section->filename);
				free(val->section->name);
				free(val->section);
				return NULL;
			}
		}
		if (!is_set(CFGF_DEFINIT, opt->flags))
			cfg_init_defaults(val->section);
		break;

	case CFGT_BOOL:
		if (opt->parsecb) {
			if ((*opt->parsecb) (cfg, opt, value, &b) != 0)
				return NULL;
		} else {
			b = cfg_parse_boolean(value);
			if (b == -1) {
				cfg_error(cfg, _("invalid boolean value for option '%s'"), opt->name);
				return NULL;
			}
		}
		val->boolean = (cfg_bool_t)b;
		break;

	case CFGT_PTR:
		assert(opt->parsecb);
		if ((*opt->parsecb) (cfg, opt, value, &p) != 0)
			return NULL;
		val->ptr = p;
		break;

	default:
		cfg_error(cfg, "internal error in cfg_setopt(%s, %s)", opt->name, value);
		assert(0);
		break;
	}

	return val;
}

DLLIMPORT int cfg_opt_setmulti(cfg_t *cfg, cfg_opt_t *opt, unsigned int nvalues, char **values)
{
	cfg_opt_t old;
	unsigned int i;

	if (!opt || !nvalues)
		return -1;

	old = *opt;
	opt->nvalues = 0;
	opt->values = 0;

	for (i = 0; i < nvalues; i++) {
		if (cfg_setopt(cfg, opt, values[i]))
			continue;

		/* ouch, revert */
		cfg_free_value(opt);
		opt->nvalues = old.nvalues;
		opt->values = old.values;

		return -1;
	}

	cfg_free_value(&old);

	return 0;
}

DLLIMPORT int cfg_setmulti(cfg_t *cfg, const char *name, unsigned int nvalues, char **values)
{
	cfg_opt_t *opt = cfg_getopt(cfg, name);

	if (!opt)
		return -1;
	return cfg_opt_setmulti(cfg, opt, nvalues, values);
}

/* searchpath */

struct cfg_searchpath_t {
	char *dir;	        /**< directory to search */
	cfg_searchpath_t *next; /**< next in list */
};

/* prepend a new cfg_searchpath_t to the linked list */

DLLIMPORT int cfg_add_searchpath(cfg_t *cfg, const char *dir)
{
	cfg_searchpath_t *p;
	char *d;

	assert(cfg && dir);

	d = cfg_tilde_expand(dir);
	if (!d)
		return CFG_PARSE_ERROR;

	p = malloc(sizeof(cfg_searchpath_t));
	if (!p) {
		free(d);
		return CFG_PARSE_ERROR;
	}

	p->next = cfg->path;
	p->dir = d;

	cfg->path = p;

	return CFG_SUCCESS;
}

DLLIMPORT cfg_errfunc_t cfg_set_error_function(cfg_t *cfg, cfg_errfunc_t errfunc)
{
	cfg_errfunc_t old;

	assert(cfg);
	old = cfg->errfunc;
	cfg->errfunc = errfunc;
	return old;
}

DLLIMPORT void cfg_error(cfg_t *cfg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);

	if (cfg && cfg->errfunc)
		(*cfg->errfunc) (cfg, fmt, ap);
	else {
		if (cfg && cfg->filename && cfg->line)
			fprintf(stderr, "%s:%d: ", cfg->filename, cfg->line);
		else if (cfg && cfg->filename)
			fprintf(stderr, "%s: ", cfg->filename);
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
	}

	va_end(ap);
}

static int call_function(cfg_t *cfg, cfg_opt_t *opt, cfg_opt_t *funcopt)
{
	int ret;
	const char **argv;
	unsigned int i;

	/* create a regular argv string vector and call
	 * the registered function
	 */
	argv = calloc(funcopt->nvalues, sizeof(char *));
	if (!argv)
		return 1;

	for (i = 0; i < funcopt->nvalues; i++)
		argv[i] = funcopt->values[i]->string;
	ret = (*opt->func) (cfg, opt, funcopt->nvalues, argv);
	cfg_free_value(funcopt);
	free(argv);

	return ret;
}

static int cfg_parse_internal(cfg_t *cfg, int level, int force_state, cfg_opt_t *force_opt)
{
	int state = 0;
	char *opttitle = 0;
	cfg_opt_t *opt = 0;
	cfg_value_t *val = 0;
	cfg_opt_t funcopt = CFG_STR(0, 0, 0);
	int num_values = 0;	/* number of values found for a list option */
	int rc;

	if (force_state != -1)
		state = force_state;
	if (force_opt)
		opt = force_opt;

	while (1) {
		int tok = cfg_yylex(cfg);

		if (tok == 0) {
			/* lexer.l should have called cfg_error */
			return STATE_ERROR;
		}

		if (tok == EOF) {
			if (state != 0) {
				cfg_error(cfg, _("premature end of file"));
				return STATE_ERROR;
			}
			return STATE_EOF;
		}

		switch (state) {
		case 0:	/* expecting an option name */
			if (tok == '}') {
				if (level == 0) {
					cfg_error(cfg, _("unexpected closing brace"));
					return STATE_ERROR;
				}
				return STATE_EOF;
			}
			if (tok != CFGT_STR) {
				cfg_error(cfg, _("unexpected token '%s'"), cfg_yylval);
				return STATE_ERROR;
			}
			opt = cfg_getopt(cfg, cfg_yylval);
			if (opt == 0) {
				if (cfg->flags & CFGF_IGNORE_UNKNOWN)
					opt = cfg_getopt(cfg, "__unknown");
				if (opt == 0)
					return STATE_ERROR;
			}
			if (opt->type == CFGT_SEC) {
				if (is_set(CFGF_TITLE, opt->flags))
					state = 6;
				else
					state = 5;
			} else if (opt->type == CFGT_FUNC) {
				state = 7;
			} else
				state = 1;
			break;

		case 1:	/* expecting an equal sign or plus-equal sign */
			if (tok == '+') {
				if (!is_set(CFGF_LIST, opt->flags)) {
					cfg_error(cfg, _("attempt to append to non-list option '%s'"), opt->name);
					return STATE_ERROR;
				}
				/* Even if the reset flag was set by
				 * cfg_init_defaults, appending to the defaults
				 * should be ok.
				 */
				opt->flags &= ~CFGF_RESET;
			} else if (tok == '=') {
				/* set the (temporary) reset flag to clear the old
				 * values, since we obviously didn't want to append */
				opt->flags |= CFGF_RESET;
			} else {
				cfg_error(cfg, _("missing equal sign after option '%s'"), opt->name);
				return STATE_ERROR;
			}
			if (is_set(CFGF_LIST, opt->flags)) {
				state = 3;
				num_values = 0;
			} else
				state = 2;
			break;

		case 2:	/* expecting an option value */
			if (tok == '}' && is_set(CFGF_LIST, opt->flags)) {
				state = 0;
				if (num_values == 0 && is_set(CFGF_RESET, opt->flags))
					/* Reset flags was set, and the empty list was
					 * specified. Free all old values. */
					cfg_free_value(opt);
				break;
			}

			if (tok != CFGT_STR) {
				cfg_error(cfg, _("unexpected token '%s'"), cfg_yylval);
				return STATE_ERROR;
			}

			if (cfg_setopt(cfg, opt, cfg_yylval) == 0)
				return STATE_ERROR;
			if (opt->validcb && (*opt->validcb) (cfg, opt) != 0)
				return STATE_ERROR;
			if (is_set(CFGF_LIST, opt->flags)) {
				++num_values;
				state = 4;
			} else
				state = 0;
			break;

		case 3:	/* expecting an opening brace for a list option */
			if (tok != '{') {
				if (tok != CFGT_STR) {
					cfg_error(cfg, _("unexpected token '%s'"), cfg_yylval);
					return STATE_ERROR;
				}

				if (cfg_setopt(cfg, opt, cfg_yylval) == 0)
					return STATE_ERROR;
				if (opt->validcb && (*opt->validcb) (cfg, opt) != 0)
					return STATE_ERROR;
				++num_values;
				state = 0;
			} else
				state = 2;
			break;

		case 4:	/* expecting a separator for a list option, or
				 * closing (list) brace */
			if (tok == ',')
				state = 2;
			else if (tok == '}') {
				state = 0;
				if (opt->validcb && (*opt->validcb) (cfg, opt) != 0)
					return STATE_ERROR;
			} else {
				cfg_error(cfg, _("unexpected token '%s'"), cfg_yylval);
				return STATE_ERROR;
			}
			break;

		case 5:	/* expecting an opening brace for a section */
			if (tok != '{') {
				cfg_error(cfg, _("missing opening brace for section '%s'"), opt->name);
				return STATE_ERROR;
			}

			val = cfg_setopt(cfg, opt, opttitle);
			free(opttitle);
			opttitle = 0;
			if (!val)
				return STATE_ERROR;

			val->section->path = cfg->path;
			val->section->line = cfg->line;
			val->section->errfunc = cfg->errfunc;
			rc = cfg_parse_internal(val->section, level + 1, -1, 0);
			cfg->line = val->section->line;
			if (rc != STATE_EOF)
				return STATE_ERROR;
			if (opt->validcb && (*opt->validcb) (cfg, opt) != 0)
				return STATE_ERROR;
			state = 0;
			break;

		case 6:	/* expecting a title for a section */
			if (tok != CFGT_STR) {
				cfg_error(cfg, _("missing title for section '%s'"), opt->name);
				return STATE_ERROR;
			} else {
				opttitle = strdup(cfg_yylval);
				if (!opttitle)
					return STATE_ERROR;
			}
			state = 5;
			break;

		case 7:	/* expecting an opening parenthesis for a function */
			if (tok != '(') {
				cfg_error(cfg, _("missing parenthesis for function '%s'"), opt->name);
				return STATE_ERROR;
			}
			state = 8;
			break;

		case 8:	/* expecting a function parameter or a closing paren */
			if (tok == ')') {
				int ret = call_function(cfg, opt, &funcopt);

				if (ret != 0)
					return STATE_ERROR;
				state = 0;
			} else if (tok == CFGT_STR) {
				val = cfg_addval(&funcopt);
				if (!val)
					return STATE_ERROR;

				val->string = strdup(cfg_yylval);
				if (!val->string)
					return STATE_ERROR;

				state = 9;
			} else {
				cfg_error(cfg, _("syntax error in call of function '%s'"), opt->name);
				return STATE_ERROR;
			}
			break;

		case 9:	/* expecting a comma in a function or a closing paren */
			if (tok == ')') {
				int ret = call_function(cfg, opt, &funcopt);

				if (ret != 0)
					return STATE_ERROR;
				state = 0;
			} else if (tok == ',')
				state = 8;
			else {
				cfg_error(cfg, _("syntax error in call of function '%s'"), opt->name);
				return STATE_ERROR;
			}
			break;

		default:
			/* missing state, internal error, abort */
			assert(0);
		}
	}

	return STATE_EOF;
}

DLLIMPORT int cfg_parse_fp(cfg_t *cfg, FILE *fp)
{
	int ret;

	assert(cfg && fp);

	if (!cfg->filename)
		cfg->filename = strdup("FILE");
	if (!cfg->filename)
		return CFG_PARSE_ERROR;

	cfg->line = 1;

	cfg_yyin = fp;
	cfg_scan_fp_begin(cfg_yyin);
	ret = cfg_parse_internal(cfg, 0, -1, 0);
	cfg_scan_fp_end();
	if (ret == STATE_ERROR)
		return CFG_PARSE_ERROR;

	return CFG_SUCCESS;
}

static char *cfg_make_fullpath(const char *dir, const char *file)
{
	int np;
	char *path;
	size_t len;

	assert(dir && file);

	len = strlen(dir) + strlen(file) + 2;
	path = malloc(len);
	if (!path)
		return NULL;

	np = snprintf(path, len, "%s/%s", dir, file);

	/*
	 * np is the number of characters that would have
	 * been printed if there was enough room in path.
	 * if np >= n then the snprintf() was truncated
	 * (which must be a bug).
	 */
	assert(np < len);

	return path;
}

DLLIMPORT char *cfg_searchpath(cfg_searchpath_t *p, const char *file)
{
	char *fullpath;
	struct stat st;
	int err;

	if (!p)
		return NULL;

	if ((fullpath = cfg_searchpath(p->next, file)) != NULL)
		return fullpath;

	if ((fullpath = cfg_make_fullpath(p->dir, file)) == NULL)
		return NULL;

#ifdef HAVE_SYS_STAT_H

	err = stat((const char *)fullpath, &st);
	if ((!err) && S_ISREG(st.st_mode))
		return fullpath;

#else

	/* needs an alternative check here for win32 */

#endif

	free(fullpath);
	return NULL;
}

DLLIMPORT int cfg_parse(cfg_t *cfg, const char *filename)
{
	int ret;
	FILE *fp;

	assert(cfg && filename);

	if (cfg->path) {
		char *f;

		if ((f = cfg_searchpath(cfg->path, filename)) == NULL)
			return CFG_FILE_ERROR;

		free(cfg->filename);
		cfg->filename = f;

	} else {
		free(cfg->filename);
		cfg->filename = cfg_tilde_expand(filename);
		if (!cfg->filename)
			return CFG_FILE_ERROR;
	}

	fp = fopen(cfg->filename, "r");
	if (!fp)
		return CFG_FILE_ERROR;

	ret = cfg_parse_fp(cfg, fp);
	fclose(fp);

	return ret;
}

DLLIMPORT int cfg_parse_buf(cfg_t *cfg, const char *buf)
{
	int ret;

	assert(cfg);
	if (buf == 0)
		return CFG_SUCCESS;

	free(cfg->filename);
	cfg->filename = strdup("[buf]");
	if (!cfg->filename)
		return CFG_PARSE_ERROR;

	cfg->line = 1;
	cfg_scan_string_begin(buf);
	ret = cfg_parse_internal(cfg, 0, -1, 0);
	cfg_scan_string_end();

	if (ret == STATE_ERROR)
		return CFG_PARSE_ERROR;

	return CFG_SUCCESS;
}

DLLIMPORT cfg_t *cfg_init(cfg_opt_t *opts, cfg_flag_t flags)
{
	cfg_t *cfg;

	cfg = calloc(1, sizeof(cfg_t));
	if (!cfg)
		return NULL;

	cfg->name = strdup("root");
	if (!cfg->name) {
		free(cfg);
		return NULL;
	}

	cfg->opts = cfg_dupopt_array(opts);
	if (!cfg->opts) {
		free(cfg->name);
		free(cfg);
		return NULL;
	}

	cfg->flags = flags;
	cfg->filename = 0;
	cfg->line = 0;
	cfg->errfunc = 0;

	cfg_init_defaults(cfg);

#if defined(ENABLE_NLS) && defined(HAVE_GETTEXT)
	setlocale(LC_MESSAGES, "");
	setlocale(LC_CTYPE, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
#endif

	return cfg;
}

DLLIMPORT char *cfg_tilde_expand(const char *filename)
{
	char *expanded = 0;

#ifndef _WIN32
	/* Do tilde expansion */
	if (filename[0] == '~') {
		struct passwd *passwd = 0;
		const char *file = 0;

		if (filename[1] == '/' || filename[1] == 0) {
			/* ~ or ~/path */
			passwd = getpwuid(geteuid());
			file = filename + 1;
		} else {
			/* ~user or ~user/path */
			char *user;

			file = strchr(filename, '/');
			if (file == 0)
				file = filename + strlen(filename);

			user = malloc(file - filename);
			if (!user)
				return NULL;

			strncpy(user, filename + 1, file - filename - 1);
			passwd = getpwnam(user);
			free(user);
		}

		if (passwd) {
			expanded = malloc(strlen(passwd->pw_dir) + strlen(file) + 1);
			if (!expanded)
				return NULL;

			strcpy(expanded, passwd->pw_dir);
			strcat(expanded, file);
		}
	}
#endif
	if (!expanded)
		expanded = strdup(filename);

	return expanded;
}

DLLIMPORT void cfg_free_value(cfg_opt_t *opt)
{
	if (!opt)
		return;

	if (opt->values) {
		unsigned int i;

		for (i = 0; i < opt->nvalues; i++) {
			if (opt->type == CFGT_STR)
				free((void *)opt->values[i]->string);
			else if (opt->type == CFGT_SEC) {
				opt->values[i]->section->path = 0;
				cfg_free(opt->values[i]->section);
			} else if (opt->type == CFGT_PTR && opt->freecb && opt->values[i]->ptr)
				(opt->freecb) (opt->values[i]->ptr);
			free(opt->values[i]);
		}
		free(opt->values);
	}
	opt->values = 0;
	opt->nvalues = 0;
}

static void cfg_free_opt_array(cfg_opt_t *opts)
{
	int i;

	for (i = 0; opts[i].name; ++i) {
		free((void *)opts[i].name);
		if (opts[i].type == CFGT_FUNC || is_set(CFGF_LIST, opts[i].flags))
			free(opts[i].def.parsed);
		else if (opts[i].type == CFGT_STR)
			free((void *)opts[i].def.string);
		else if (opts[i].type == CFGT_SEC)
			cfg_free_opt_array(opts[i].subopts);
	}
	free(opts);
}

static void cfg_free_searchpath(cfg_searchpath_t *p)
{
	if (p) {
		cfg_free_searchpath(p->next);
		free(p->dir);
		free(p);
	}
}

DLLIMPORT void cfg_free(cfg_t *cfg)
{
	int i;

	if (cfg == 0)
		return;

	for (i = 0; cfg->opts[i].name; ++i)
		cfg_free_value(&cfg->opts[i]);

	cfg_free_opt_array(cfg->opts);
	cfg_free_searchpath(cfg->path);

	free(cfg->name);
	free(cfg->title);
	free(cfg->filename);

	free(cfg);
}

DLLIMPORT int cfg_include(cfg_t *cfg, cfg_opt_t *opt, int argc, const char **argv)
{
	opt = NULL;
	if (argc != 1) {
		cfg_error(cfg, _("wrong number of arguments to cfg_include()"));
		return 1;
	}

	return cfg_lexer_include(cfg, argv[0]);
}

static cfg_value_t *cfg_opt_getval(cfg_opt_t *opt, unsigned int index)
{
	cfg_value_t *val = 0;

	assert(index == 0 || is_set(CFGF_LIST, opt->flags) || is_set(CFGF_MULTI, opt->flags));

	if (opt->simple_value.ptr)
		val = (cfg_value_t *)opt->simple_value.ptr;
	else {
		if (is_set(CFGF_RESET, opt->flags)) {
			cfg_free_value(opt);
			opt->flags &= ~CFGF_RESET;
		}

		if (index >= opt->nvalues)
			val = cfg_addval(opt);
		else
			val = opt->values[index];
	}

	return val;
}

DLLIMPORT void cfg_opt_setnint(cfg_opt_t *opt, long int value, unsigned int index)
{
	cfg_value_t *val;

	assert(opt && opt->type == CFGT_INT);
	val = cfg_opt_getval(opt, index);
	val->number = value;
}

DLLIMPORT void cfg_setnint(cfg_t *cfg, const char *name, long int value, unsigned int index)
{
	cfg_opt_setnint(cfg_getopt(cfg, name), value, index);
}

DLLIMPORT void cfg_setint(cfg_t *cfg, const char *name, long int value)
{
	cfg_setnint(cfg, name, value, 0);
}

DLLIMPORT void cfg_opt_setnfloat(cfg_opt_t *opt, double value, unsigned int index)
{
	cfg_value_t *val;

	assert(opt && opt->type == CFGT_FLOAT);
	val = cfg_opt_getval(opt, index);
	val->fpnumber = value;
}

DLLIMPORT void cfg_setnfloat(cfg_t *cfg, const char *name, double value, unsigned int index)
{
	cfg_opt_setnfloat(cfg_getopt(cfg, name), value, index);
}

DLLIMPORT void cfg_setfloat(cfg_t *cfg, const char *name, double value)
{
	cfg_setnfloat(cfg, name, value, 0);
}

DLLIMPORT void cfg_opt_setnbool(cfg_opt_t *opt, cfg_bool_t value, unsigned int index)
{
	cfg_value_t *val;

	assert(opt && opt->type == CFGT_BOOL);
	val = cfg_opt_getval(opt, index);
	val->boolean = value;
}

DLLIMPORT void cfg_setnbool(cfg_t *cfg, const char *name, cfg_bool_t value, unsigned int index)
{
	cfg_opt_setnbool(cfg_getopt(cfg, name), value, index);
}

DLLIMPORT void cfg_setbool(cfg_t *cfg, const char *name, cfg_bool_t value)
{
	cfg_setnbool(cfg, name, value, 0);
}

DLLIMPORT void cfg_opt_setnstr(cfg_opt_t *opt, const char *value, unsigned int index)
{
	cfg_value_t *val;

	assert(opt && opt->type == CFGT_STR);
	val = cfg_opt_getval(opt, index);
	if (val->string)
		free(val->string);
	val->string = value ? strdup(value) : 0;
}

DLLIMPORT void cfg_setnstr(cfg_t *cfg, const char *name, const char *value, unsigned int index)
{
	cfg_opt_setnstr(cfg_getopt(cfg, name), value, index);
}

DLLIMPORT void cfg_setstr(cfg_t *cfg, const char *name, const char *value)
{
	cfg_setnstr(cfg, name, value, 0);
}

static void cfg_addlist_internal(cfg_opt_t *opt, unsigned int nvalues, va_list ap)
{
	unsigned int i;

	for (i = 0; i < nvalues; i++) {
		switch (opt->type) {
		case CFGT_INT:
			cfg_opt_setnint(opt, va_arg(ap, int), opt->nvalues);

			break;
		case CFGT_FLOAT:
			cfg_opt_setnfloat(opt, va_arg(ap, double), opt->nvalues);

			break;
		case CFGT_BOOL:
			cfg_opt_setnbool(opt, va_arg(ap, cfg_bool_t), opt->nvalues);

			break;
		case CFGT_STR:
			cfg_opt_setnstr(opt, va_arg(ap, char *), opt->nvalues);

			break;
		case CFGT_FUNC:
		case CFGT_SEC:
		default:
			break;
		}
	}
}

DLLIMPORT void cfg_setlist(cfg_t *cfg, const char *name, unsigned int nvalues, ...)
{
	va_list ap;
	cfg_opt_t *opt = cfg_getopt(cfg, name);

	assert(opt && is_set(CFGF_LIST, opt->flags));

	cfg_free_value(opt);
	va_start(ap, nvalues);
	cfg_addlist_internal(opt, nvalues, ap);
	va_end(ap);
}

DLLIMPORT void cfg_addlist(cfg_t *cfg, const char *name, unsigned int nvalues, ...)
{
	va_list ap;
	cfg_opt_t *opt = cfg_getopt(cfg, name);

	assert(opt && is_set(CFGF_LIST, opt->flags));

	va_start(ap, nvalues);
	cfg_addlist_internal(opt, nvalues, ap);
	va_end(ap);
}

DLLIMPORT void cfg_opt_rmnsec(cfg_opt_t *opt, unsigned int index)
{
	unsigned int n;
	cfg_value_t *val;

	assert(opt && opt->type == CFGT_SEC);
	n = cfg_opt_size(opt);
	if (index >= n)
		return;

	val = cfg_opt_getval(opt, index);
	if (index + 1 != n) {
		/* not removing last, move the tail */
		memmove(&opt->values[index], &opt->values[index + 1], sizeof(opt->values[index]) * (n - index - 1));
	}
	--opt->nvalues;

	cfg_free(val->section);
	free(val);
}

DLLIMPORT void cfg_rmnsec(cfg_t *cfg, const char *name, unsigned int index)
{
	cfg_opt_rmnsec(cfg_getopt(cfg, name), index);
}

DLLIMPORT void cfg_rmsec(cfg_t *cfg, const char *name)
{
	cfg_rmnsec(cfg, name, 0);
}

DLLIMPORT void cfg_opt_rmtsec(cfg_opt_t *opt, const char *title)
{
	unsigned int i, n;

	assert(opt && title);
	if (!is_set(CFGF_TITLE, opt->flags))
		return;

	n = cfg_opt_size(opt);
	for (i = 0; i < n; i++) {
		cfg_t *sec = cfg_opt_getnsec(opt, i);

		assert(sec && sec->title);
		if (is_set(CFGF_NOCASE, opt->flags)) {
			if (strcasecmp(title, sec->title) == 0)
				break;
		} else {
			if (strcmp(title, sec->title) == 0)
				break;
		}
	}
	if (i == n)
		return;

	cfg_opt_rmnsec(opt, i);
}

DLLIMPORT void cfg_rmtsec(cfg_t *cfg, const char *name, const char *title)
{
	cfg_opt_rmtsec(cfg_getopt(cfg, name), title);
}

DLLIMPORT void cfg_opt_nprint_var(cfg_opt_t *opt, unsigned int index, FILE *fp)
{
	const char *str;

	assert(opt && fp);

	switch (opt->type) {
	case CFGT_INT:
		fprintf(fp, "%ld", cfg_opt_getnint(opt, index));
		break;
	case CFGT_FLOAT:
		fprintf(fp, "%f", cfg_opt_getnfloat(opt, index));
		break;
	case CFGT_STR:
		str = cfg_opt_getnstr(opt, index);
		fprintf(fp, "\"");
		while (str && *str) {
			if (*str == '"')
				fprintf(fp, "\\\"");
			else if (*str == '\\')
				fprintf(fp, "\\\\");
			else
				fprintf(fp, "%c", *str);
			str++;
		}
		fprintf(fp, "\"");
		break;
	case CFGT_BOOL:
		fprintf(fp, "%s", cfg_opt_getnbool(opt, index) ? "true" : "false");
		break;
	case CFGT_NONE:
	case CFGT_SEC:
	case CFGT_FUNC:
	case CFGT_PTR:
		break;
	}
}

static void cfg_indent(FILE *fp, int indent)
{
	while (indent--)
		fprintf(fp, "  ");
}

DLLIMPORT void cfg_opt_print_indent(cfg_opt_t *opt, FILE *fp, int indent)
{
	assert(opt && fp);

	if (opt->type == CFGT_SEC) {
		cfg_t *sec;
		unsigned int i;

		for (i = 0; i < cfg_opt_size(opt); i++) {
			sec = cfg_opt_getnsec(opt, i);
			cfg_indent(fp, indent);
			if (is_set(CFGF_TITLE, opt->flags))
				fprintf(fp, "%s \"%s\" {\n", opt->name, cfg_title(sec));
			else
				fprintf(fp, "%s {\n", opt->name);
			cfg_print_indent(sec, fp, indent + 1);
			cfg_indent(fp, indent);
			fprintf(fp, "}\n");
		}
	} else if (opt->type != CFGT_FUNC && opt->type != CFGT_NONE) {
		if (is_set(CFGF_LIST, opt->flags)) {
			cfg_indent(fp, indent);
			fprintf(fp, "%s = {", opt->name);

			if (opt->nvalues) {
				unsigned int i;

				if (opt->pf)
					opt->pf(opt, 0, fp);
				else
					cfg_opt_nprint_var(opt, 0, fp);
				for (i = 1; i < opt->nvalues; i++) {
					fprintf(fp, ", ");
					if (opt->pf)
						opt->pf(opt, i, fp);
					else
						cfg_opt_nprint_var(opt, i, fp);
				}
			}

			fprintf(fp, "}");
		} else {
			cfg_indent(fp, indent);
			/* comment out the option if is not set */
			if (opt->simple_value.ptr) {
				if (opt->type == CFGT_STR && *opt->simple_value.string == 0)
					fprintf(fp, "# ");
			} else {
				if (cfg_opt_size(opt) == 0 || (opt->type == CFGT_STR && (opt->values[0]->string == 0 ||
											 opt->values[0]->string[0] == 0)))
					fprintf(fp, "# ");
			}
			fprintf(fp, "%s=", opt->name);
			if (opt->pf)
				opt->pf(opt, 0, fp);
			else
				cfg_opt_nprint_var(opt, 0, fp);
		}

		fprintf(fp, "\n");
	} else if (opt->pf) {
		cfg_indent(fp, indent);
		opt->pf(opt, 0, fp);
		fprintf(fp, "\n");
	}
}

DLLIMPORT void cfg_opt_print(cfg_opt_t *opt, FILE *fp)
{
	cfg_opt_print_indent(opt, fp, 0);
}

DLLIMPORT void cfg_print_indent(cfg_t *cfg, FILE *fp, int indent)
{
	int i;

	for (i = 0; cfg->opts[i].name; i++)
		cfg_opt_print_indent(&cfg->opts[i], fp, indent);
}

DLLIMPORT void cfg_print(cfg_t *cfg, FILE *fp)
{
	cfg_print_indent(cfg, fp, 0);
}

DLLIMPORT cfg_print_func_t cfg_opt_set_print_func(cfg_opt_t *opt, cfg_print_func_t pf)
{
	cfg_print_func_t oldpf;

	assert(opt);

	oldpf = opt->pf;
	opt->pf = pf;

	return oldpf;
}

DLLIMPORT cfg_print_func_t cfg_set_print_func(cfg_t *cfg, const char *name, cfg_print_func_t pf)
{
	return cfg_opt_set_print_func(cfg_getopt(cfg, name), pf);
}

static cfg_opt_t *cfg_getopt_array(cfg_opt_t *rootopts, int cfg_flags, const char *name)
{
	unsigned int i;
	cfg_opt_t *opts = rootopts;

	assert(rootopts && name);

	while (name && *name) {
		cfg_t *seccfg;
		char *secname;
		size_t len = strcspn(name, "|");

		if (name[len] == 0 /*len == strlen(name) */ )
			/* no more subsections */
			break;

		if (len) {
			cfg_opt_t *secopt;

			secname = strndup(name, len);
			if (!secname)
				return NULL;

			secopt = cfg_getopt_array(opts, cfg_flags, secname);
			free(secname);
			if (!secopt) {
				/*fprintf(stderr, "section not found\n"); */
				return NULL;
			}
			if (secopt->type != CFGT_SEC) {
				/*fprintf(stderr, "not a section!\n"); */
				return NULL;
			}

			if (!is_set(CFGF_MULTI, secopt->flags) && (seccfg = cfg_opt_getnsec(secopt, 0)) != 0)
				opts = seccfg->opts;
			else
				opts = secopt->subopts;

			if (!opts) {
				/*fprintf(stderr, "section have no subopts!?\n"); */
				return NULL;
			}
		}
		name += len;
		name += strspn(name, "|");
	}

	for (i = 0; opts[i].name; i++) {
		if (is_set(CFGF_NOCASE, cfg_flags)) {
			if (strcasecmp(opts[i].name, name) == 0)
				return &opts[i];
		} else {
			if (strcmp(opts[i].name, name) == 0)
				return &opts[i];
		}
	}

	return NULL;
}

DLLIMPORT cfg_validate_callback_t cfg_set_validate_func(cfg_t *cfg, const char *name, cfg_validate_callback_t vf)
{
	cfg_opt_t *opt;
	cfg_validate_callback_t oldvf;

	opt = cfg_getopt_array(cfg->opts, cfg->flags, name);
	if (!opt)
		return NULL;

	oldvf = opt->validcb;
	opt->validcb = vf;

	return oldvf;
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
