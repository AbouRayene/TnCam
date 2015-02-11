/*
 * OSCam self tests
 * This file contains tests for different config parsers and generators
 * Build this file using `make tests`
 */
#include "globals.h"

#include "oscam-string.h"
#include "oscam-conf-chk.h"
#include "oscam-conf-mk.h"

struct test_vec
{
	const char *in;  // Input data
	const char *out; // Expected output data (if out is NULL, then assume in == out)
};

typedef void  (CHK_FN)  (char *, void *);
typedef char *(MK_T_FN) (void *);
typedef void  (CLEAR_FN)(void *);

struct test_type
{
	char		*desc;		// Test textual description
	void		*data;		// Pointer to basic data structure
	size_t		data_sz;	// Data structure size
	CHK_FN		*chk_fn;	// chk_XXX() func for the data type
	MK_T_FN		*mk_t_fn;	// mk_t_XXX() func for the data type
	CLEAR_FN	*clear_fn;	// clear_XXX() func for the data type
	const struct test_vec *test_vec; // Array of test vectors
};

static void run_parser_test(struct test_type *t)
{
	memset(t->data, 0, t->data_sz);
	printf("%s\n", t->desc);
	const struct test_vec *vec = t->test_vec;
	while (vec->in)
	{
		bool ok;
		printf(" Testing \"%s\"", vec->in);
		char *input_setting = cs_strdup(vec->in);
		t->chk_fn(input_setting, t->data);
		char *generated = t->mk_t_fn(t->data);
		if (vec->out)
			ok = strcmp(vec->out, generated) == 0;
		else
			ok = strcmp(vec->in, generated) == 0;
		if (ok)
		{
			printf(" [OK]\n");
		} else {
			printf("\n");
			printf(" === ERROR ===\n");
			printf("  Input data:   \"%s\"\n", vec->in);
			printf("  Got result:   \"%s\"\n", generated);
			printf("  Expected out: \"%s\"\n", vec->out ? vec->out : vec->in);
			printf("\n");
		}
		free_mk_t(generated);
		free(input_setting);
		fflush(stdout);
		vec++;
	}
	t->clear_fn(t->data);
}

int main(void)
{
	ECM_WHITELIST ecm_whitelist;
	struct test_type ecm_whitelist_test =
	{
		.desc     = "ECM white list setting (READERS: 'ecmwhitelist')",
		.data     = &ecm_whitelist,
		.data_sz  = sizeof(ecm_whitelist),
		.chk_fn   = (CHK_FN *)&chk_ecm_whitelist,
		.mk_t_fn  = (MK_T_FN *)&mk_t_ecm_whitelist,
		.clear_fn = (CLEAR_FN *)&clear_ecm_whitelist,
		.test_vec = (const struct test_vec[])
		{
			{ .in = "0500@043800:70,6E,6C,66,7A,61,67,75,5D,6B;0600@070800:11,22,33,44,55,66;0700:AA,BB,CC,DD,EE;01,02,03,04;0123@456789:01,02,03,04" },
			{ .in = "0500@043800:70,6E,6C,66,7A,61,67,75,5D,6B" },
			{ .in = "0500@043800:70,6E,6C,66" },
			{ .in = "0500@043800:70,6E,6C" },
			{ .in = "0500@043800:70" },
			{ .in = "0500:81,82,83;0600:91" },
			{ .in = "0500:81,82" },
			{ .in = "0500:81" },
			{ .in = "@123456:81" },
			{ .in = "@123456:81;@000789:AA,BB,CC" },
			{ .in = "81" },
			{ .in = "81,82,83" },
			{ .in = "81,82,83,84" },
			{ .in = "0500@043800:70;0600@070800:11;0123@456789:01,02" },
			{ .in = "" },
			{ .in = "0500:81,32;0600:aa,bb", .out = "0500:81,32;0600:AA,BB" },
			{ .in = "500:1,2;60@77:a,b,z,,", .out = "0500:01,02;0060@000077:0A,0B" },
			{ .in = "@ff:81;@bb:11,22",      .out = "@0000FF:81;@0000BB:11,22" },
			{ .in = "@:81",                  .out = "81" },
			{ .in = "81;zzs;;;;;ab",         .out = "81,AB" },
			{ .in = ":@",                    .out = "" },
			{ .in = ",:,@,",                 .out = "" },
			{ .in = "@:",                    .out = "" },
			{ .in = "@:,,",                  .out = "" },
			{ .in = "@:;;;",                 .out = "" },
			{ .in = ",",                     .out = "" },
			{ .in = NULL },
		},
	};
	run_parser_test(&ecm_whitelist_test);

	return 0;
}
