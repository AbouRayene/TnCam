#include "globals.h"
#include "oscam-conf.h"

#define CONFVARWIDTH 30

/* Returns the default value if string length is zero, otherwise atoi is called*/
int32_t strToIntVal(char *value, int32_t defaultvalue){
	if (strlen(value) == 0) return defaultvalue;
	errno = 0; // errno should be set to 0 before calling strtol
	int32_t i = strtol(value, NULL, 10);
	return (errno == 0) ? i : defaultvalue;
}

/* Returns the default value if string length is zero, otherwise strtoul is called*/
uint32_t strToUIntVal(char *value, uint32_t defaultvalue){
	if (strlen(value) == 0) return defaultvalue;
	errno = 0; // errno should be set to 0 before calling strtoul
	uint32_t i = strtoul(value, NULL, 10);
	return (errno == 0) ? i : defaultvalue;
}

 /* Replacement of fprintf which adds necessary whitespace to fill up the varname to a fixed width.
   If varname is longer than CONFVARWIDTH, no whitespace is added*/
void fprintf_conf(FILE *f, const char *varname, const char *fmtstring, ...){
	int32_t varlen = strlen(varname);
	int32_t max = (varlen > CONFVARWIDTH) ? varlen : CONFVARWIDTH;
	char varnamebuf[max + 3];
	char *ptr = varnamebuf + varlen;
	va_list argptr;

	cs_strncpy(varnamebuf, varname, sizeof(varnamebuf));
	while(varlen < CONFVARWIDTH){
		ptr[0] = ' ';
		++ptr;
		++varlen;
	}
	cs_strncpy(ptr, "= ", sizeof(varnamebuf)-(ptr-varnamebuf));
	if (fwrite(varnamebuf, sizeof(char), strlen(varnamebuf), f)){
		if(strlen(fmtstring) > 0){
			va_start(argptr, fmtstring);
			vfprintf(f, fmtstring, argptr);
			va_end(argptr);
		}
	}
}

void fprintf_conf_n(FILE *f, const char *varname) {
	fprintf_conf(f, varname, "%s", "");
}

int config_list_parse(const struct config_list *clist, const char *token, char *value, void *config_data) {
	const struct config_list *c;
	for (c = clist; c->opt_type != OPT_UNKNOWN; c++) {
		if (c->opt_type == OPT_SAVE_FUNC)
			continue;
		if (strcasecmp(token, c->config_name) != 0)
			continue;
		void *cfg = config_data + c->var_offset;
		switch (c->opt_type) {
		case OPT_INT: {
			*(int32_t *)cfg = strToIntVal(value, c->def.d_int);
			return 1;
		}
		case OPT_UINT: {
			*(uint32_t *)cfg = strToUIntVal(value, c->def.d_uint);
			return 1;
		}
		case OPT_STRING: {
			char **scfg = cfg;
			if (c->def.d_char && strlen(value) == 0) // Set default
				value = c->def.d_char;
			NULLFREE(*scfg);
			if (strlen(value))
				*scfg = strdup(value);
			return 1;
		}
		case OPT_SSTRING: {
			char *scfg = cfg;
			if (c->def.d_char && strlen(value) == 0) // Set default
				value = c->def.d_char;
			scfg[0] = '\0';
			unsigned int len = strlen(value);
			if (len) {
				strncpy(scfg, value, c->str_size - 1);
				if (len > c->str_size) {
					fprintf(stderr, "WARNING: Config value for '%s' (%s, len=%d) exceeds max length: %d (%s)\n",
						token, value, len, c->str_size - 1, scfg);
				}
			}
			return 1;
		}
		case OPT_FUNC: {
			if (c->ops.process_fn)
				c->ops.process_fn(token, value, cfg, NULL);
			return 1;
		}
		case OPT_SAVE_FUNC:
			return 1;
		case OPT_UNKNOWN: {
			fprintf(stderr, "Unknown config type (%s = %s).", token, value);
			break;
		}
		}
	}
	return 0;
}

void config_list_save(FILE *f, const struct config_list *clist, void *config_data, int save_all) {
	const struct config_list *c;
	for (c = clist; c->opt_type != OPT_UNKNOWN; c++) {
		void *cfg = config_data + c->var_offset;
		switch (c->opt_type) {
		case OPT_INT: {
			int32_t val = *(int32_t *)cfg;
			if (save_all || val != c->def.d_int)
				fprintf_conf(f, c->config_name, "%d\n", val);
			continue;
		}
		case OPT_UINT: {
			uint32_t val = *(uint32_t *)cfg;
			if (save_all || val != c->def.d_uint)
				fprintf_conf(f, c->config_name, "%u\n", val);
			continue;
		}
		case OPT_STRING: {
			char **val = cfg;
			if (save_all || !streq(*val, c->def.d_char)) {
				fprintf_conf(f, c->config_name, "%s\n", *val ? *val : "");
			}
			continue;
		}
		case OPT_SSTRING: {
			char *val = cfg;
			if (save_all || !streq(val, c->def.d_char)) {
				fprintf_conf(f, c->config_name, "%s\n", val[0] ? val : "");
			}
			continue;
		}
		case OPT_FUNC: {
			if (c->ops.process_fn)
				c->ops.process_fn((const char *)c->config_name, NULL, cfg, f);
			continue;
		}
		case OPT_SAVE_FUNC:
			continue;
		case OPT_UNKNOWN:
			break;
		}
	}
}

bool config_list_should_be_saved(const struct config_list *clist) {
	const struct config_list *c;
	for (c = clist; c->opt_type != OPT_UNKNOWN; c++) {
		if (c->opt_type == OPT_SAVE_FUNC && c->ops.should_save_fn) {
			return c->ops.should_save_fn();
		}
	}
	return true;
}

void config_list_set_defaults(const struct config_list *clist, void *config_data) {
	const struct config_list *c;
	for (c = clist; c->opt_type != OPT_UNKNOWN; c++) {
		void *cfg = config_data + c->var_offset;
		switch (c->opt_type) {
		case OPT_INT: {
			*(int32_t *)cfg = c->def.d_int;
			break;
		}
		case OPT_UINT: {
			*(uint32_t *)cfg = c->def.d_uint;
			break;
		}
		case OPT_STRING: {
			char **scfg = cfg;
			NULLFREE(*scfg);
			if (c->def.d_char)
				*scfg = strdup(c->def.d_char);
			break;
		}
		case OPT_SSTRING: {
			char *scfg = cfg;
			scfg[0] = '\0';
			if (c->def.d_char && strlen(c->def.d_char))
				cs_strncpy(scfg, c->def.d_char, c->str_size - 1);
			break;
		}
		case OPT_FUNC: {
			c->ops.process_fn((const char *)c->config_name, "", cfg, NULL);
			break;
		}
		case OPT_SAVE_FUNC:
		case OPT_UNKNOWN:
			continue;
		}
	}
	return;
}
