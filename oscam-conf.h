#ifndef OSCAM_CONF_H
#define OSCAM_CONF_H

#include <stdio.h>
#include <inttypes.h>

enum opt_types {
	OPT_UNKNOWN = 0,
	OPT_INT     = 1 << 1,
	OPT_UINT    = 1 << 2,
	OPT_STRING  = 1 << 3,
	OPT_SSTRING = 1 << 4,
	OPT_FUNC    = 1 << 5,
};

struct config_list {
	enum opt_types	opt_type;
	char			*config_name;
	size_t			var_offset;
	long			default_value;
	unsigned int	str_size;
	union {
		void			(*process_fn)(const char *token, char *value, void *setting, FILE *config_file);
	} ops;
};

#define DEF_OPT_INT(__name, __var_ofs, __default) \
	{ \
		.opt_type		= OPT_INT, \
		.config_name	= __name, \
		.var_offset		= __var_ofs, \
		.default_value	= __default \
	}

#define DEF_OPT_UINT(__name, __var_ofs, __default) \
	{ \
		.opt_type		= OPT_UINT, \
		.config_name	= __name, \
		.var_offset		= __var_ofs, \
		.default_value	= __default \
	}

#define DEF_OPT_STR(__name, __var_ofs) \
	{ \
		.opt_type		= OPT_STRING, \
		.config_name	= __name, \
		.var_offset		= __var_ofs \
	}

#define DEF_OPT_SSTR(__name, __var_ofs, __str_size) \
	{ \
		.opt_type		= OPT_SSTRING, \
		.config_name	= __name, \
		.var_offset		= __var_ofs, \
		.str_size		= __str_size \
	}

#define DEF_OPT_FUNC(__name, __var_ofs, __process_fn) \
	{ \
		.opt_type		= OPT_FUNC, \
		.config_name	= __name, \
		.var_offset		= __var_ofs, \
		.ops.process_fn	= __process_fn \
	}

#define DEF_LAST_OPT \
	{ \
		.opt_type		= OPT_UNKNOWN \
	}

struct config_sections {
	const char					*section;
	const struct config_list	*config;
};

int32_t  strToIntVal(char *value, int32_t defaultvalue);
uint32_t strToUIntVal(char *value, uint32_t defaultvalue);

void fprintf_conf(FILE *f, const char *varname, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));
void fprintf_conf_n(FILE *f, const char *varname);

int  config_list_parse(const struct config_list *clist, const char *token, char *value, void *config_data);
void config_list_save(FILE *f, const struct config_list *clist, void *config_data, int save_all);

#endif
