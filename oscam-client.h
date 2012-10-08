#ifndef OSCAM_CLIENT_H_
#define OSCAM_CLIENT_H_

struct s_client *cur_client(void);
int32_t get_threadnum(struct s_client *client);
int8_t check_client(struct s_client *client);
struct s_auth *get_account_by_name(char *name);
int8_t is_valid_client(struct s_client *client);
const char *remote_txt(void);
const char *client_get_proto(struct s_client *cl);
char *username(struct s_client * client);
void init_first_client(void);
struct s_client *create_client(IN_ADDR_T ip);
int32_t cs_auth_client(struct s_client * client, struct s_auth *account, const char *e_txt);
void cs_disconnect_client(struct s_client * client);
void cs_reinit_clients(struct s_auth *new_accounts);
void kill_all_clients(void);

#endif
