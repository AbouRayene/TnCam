#include "globals.h"
#include "module-datastruct-llist.h"

void init_stat();

READER_STAT *get_stat(struct s_reader *rdr, ushort caid, ulong prid, ushort srvid);

int remove_stat(struct s_reader *rdr, ushort caid, ulong prid, ushort srvid);

void calc_stat(READER_STAT *stat);

void add_stat(struct s_reader *rdr, ECM_REQUEST *er, int time, int rc);

int get_best_reader(ECM_REQUEST *er);

void clear_reader_stat(struct s_reader *rdr);
