#include "globals.h"

int logfd=0;
extern struct s_reader * reader;

static int proxy;
static struct s_emm *emmcache;
static int last_idx=1;
static ushort idx=1;

void reader_do_idle(struct s_reader * reader);

void cs_ri_brk(struct s_reader * reader, int flag)
{
  static int brk_pos=0;
  if (flag)
    brk_pos=reader->init_history_pos;
  else
    reader->init_history_pos=brk_pos;
}

void cs_ri_log(struct s_reader * reader, char *fmt,...)
{
	char txt[256];

	va_list params;
	va_start(params, fmt);
	vsprintf(txt, fmt, params);
	va_end(params);
	cs_log("%s", txt);
#ifdef CS_RDR_INIT_HIST
	int val;
	val=sizeof(reader->init_history)-reader->init_history_pos-1;
	if (val>0)
		snprintf((char *) reader->init_history+reader->init_history_pos, val, "%s", txt);
#endif
	if (cfg->saveinithistory) {
		FILE *fp;
		char filename[32];
		char *buffer;
		sprintf(filename, "%s/reader%d", get_tmp_dir(), reader->ridx);
		int size = reader->init_history_pos+strlen(txt)+1;
		buffer = malloc(size+1);

		if (buffer == NULL)
			return;

		memset(buffer, 32, size);

		fp = fopen(filename, "r");

		if (fp) {
			fread(buffer, 1, reader->init_history_pos, fp);
			fclose(fp);
		}

		sprintf(buffer+reader->init_history_pos, "%s\n", txt);

		fp = fopen(filename, "w");
		fwrite(buffer, 1, reader->init_history_pos+strlen(txt)+1, fp);
		fclose(fp);

		free(buffer);
	}
	reader->init_history_pos+=strlen(txt)+1;
}

static void casc_check_dcw(struct s_reader * reader, int idx, int rc, uchar *cw)
{
  int i;
  for (i=1; i<CS_MAXPENDING; i++)
  {
    if ((ecmtask[i].rc>=10) &&
        (!memcmp(ecmtask[i].ecmd5, ecmtask[idx].ecmd5, CS_ECMSTORESIZE)))
    {
      if (rc)
      {
        ecmtask[i].rc=(i==idx) ? 1 : 2;
#ifdef CS_WITH_GBOX
        if(ecmtask[i].gbxRidx)ecmtask[i].rc=0;
#endif
        memcpy(ecmtask[i].cw, cw, 16);
      }
      else
        ecmtask[i].rc=0;    
      write_ecm_answer(reader, fd_c2m, &ecmtask[i]);
      ecmtask[i].idx=0;
    }
  }
}

int casc_recv_timer(struct s_reader * reader, uchar *buf, int l, int msec)
{
  struct timeval tv;
  fd_set fds;
  int rc;

  if (!pfd) return(-1);
  tv.tv_sec = msec/1000;
  tv.tv_usec = (msec%1000)*1000;
  FD_ZERO(&fds);
  FD_SET(pfd, &fds);
  select(pfd+1, &fds, 0, 0, &tv);
  rc=0;
  if (FD_ISSET(pfd, &fds))
    if (!(rc=reader->ph.recv(buf, l)))
      rc=-1;

  return(rc);
}

#define MSTIMEOUT                 0x800000 
#define DEFAULT_CONNECT_TIMEOUT   500
  
int network_select(int forRead, int timeout) 
{ 
   int sd = client[cs_idx].udp_fd; 
   if(sd>=0) { 
       fd_set fds; 
       FD_ZERO(&fds); FD_SET(sd,&fds); 
       struct timeval tv; 
       if(timeout&MSTIMEOUT) { tv.tv_sec=0; tv.tv_usec=(timeout&~MSTIMEOUT)*1000; } 
       else { tv.tv_sec=timeout; tv.tv_usec=0; } 
       int r=select(sd+1,forRead ? &fds:0,forRead ? 0:&fds,0,&tv); 
       if(r>0) return 1; 
       else if(r<0) { 
         cs_debug("socket: select failed: %s",strerror(errno)); 
         return -1; 
       } 
       else { 
         if(timeout>0)  
           cs_debug("socket: select timed out (%d %s)",timeout&~MSTIMEOUT,(timeout&MSTIMEOUT)?"ms":"secs"); 
         errno=ETIMEDOUT;
         return 0; 
       } 
   } 
   return -1; 
} 

// according to documentation getaddrinfo() is thread safe
int hostResolve(int ridx)
{
   int result = 0;
   int cs_idx = reader[ridx].cs_idx;
   
   pthread_mutex_lock(&gethostbyname_lock);
   
   in_addr_t last_ip = client[cs_idx].ip;
   
   if (cfg->resolve_gethostbyname) { //Resolve with gethostbyname:
     struct hostent *rht = gethostbyname(reader[ridx].device);
     if (!rht) {
       cs_log("can't resolve %s", reader[ridx].device);
       result = 0;
     } else {
       memcpy(&client[cs_idx].udp_sa.sin_addr, rht->h_addr, sizeof(client[cs_idx].udp_sa.sin_addr));
       client[cs_idx].ip=cs_inet_order(client[cs_idx].udp_sa.sin_addr.s_addr);
       result = 1;
     }
   }
   else { //Resolve with getaddrinfo:
     struct addrinfo hints, *res = NULL;
     memset(&hints, 0, sizeof(hints));
     hints.ai_socktype = SOCK_STREAM;
     hints.ai_family = client[cs_idx].udp_sa.sin_family;
     hints.ai_protocol = IPPROTO_TCP;

     int err = getaddrinfo(reader[ridx].device, NULL, &hints, &res);
     if (err != 0 || !res || !res->ai_addr) {
       cs_log("can't resolve %s, error: %s", reader[ridx].device, err ? gai_strerror(err) : "unknown");
       result = 0;
     } else {
       client[cs_idx].udp_sa.sin_addr.s_addr = ((struct sockaddr_in *)(res->ai_addr))->sin_addr.s_addr;
       client[cs_idx].ip = cs_inet_order(client[cs_idx].udp_sa.sin_addr.s_addr);
       result = 1;
     }
     if (res) freeaddrinfo(res);
   }

   if (!result) {
     client[cs_idx].udp_sa.sin_addr.s_addr = 0;
     client[cs_idx].ip = 0;
   } else if (client[cs_idx].ip != last_ip) {
     uchar *ip = (uchar*) &client[cs_idx].ip;
     cs_log("%s: resolved ip=%d.%d.%d.%d", reader[ridx].device, ip[3], ip[2], ip[1], ip[0]);
   }

   pthread_mutex_unlock(&gethostbyname_lock);

   return result;
}

int network_tcp_connection_open()
{
  cs_log("connecting to %s", reader[ridx].device);

  if (!hostResolve(ridx))
     return -1;
 
  int sd = client[cs_idx].udp_fd;
  if (connect(sd, (struct sockaddr *)&client[cs_idx].udp_sa, sizeof(client[cs_idx].udp_sa)) == 0)
     return sd;
	 
  if (errno == EINPROGRESS || errno == EALREADY) {
     if (network_select(0, DEFAULT_CONNECT_TIMEOUT) > 0) {
        int r = -1;
        uint l = sizeof(r);
        if (getsockopt(sd, SOL_SOCKET, SO_ERROR, &r, (socklen_t*)&l) == 0) {
           if (r == 0) return sd;
	}
     }
  }
  else if (errno == EBADF || errno == ENOTSOCK) {
    cs_log("connect failed: bad socket/descriptor %d", sd);
    return -1;
  }
  else if (errno == EISCONN) {
    cs_log("already connected!");
    return sd;
  }
  else if (errno == ETIMEDOUT) {
    cs_log("connect failed: timeout");
    return -1;
  }
  else if (errno == ECONNREFUSED) {
    cs_log("connection refused");
    return -1;
  }
  else if (errno == ENETUNREACH) {
    cs_log("connect failed: network unreachable!");
    return -1;
  }
  else if (errno == EADDRINUSE) {
    cs_log("connect failed: address in use!");
    return -1;
  }
                                                 
  cs_log("connect(fd=%d) failed: (errno=%d)", sd, errno);
  return -1; 
}

void network_tcp_connection_close(struct s_reader * reader, int fd)
{
  cs_debug("tcp_conn_close(): fd=%d, is_server=%d", fd, is_server);
  close(fd);
  client[cs_idx].udp_fd = 0;

  if (!is_server)
  {
    int i;
    pfd = 0;
    reader->tcp_connected = 0;

    if (ecmtask) {
	for (i = 0; i < CS_MAXPENDING; i++) {
	   ecmtask[i].idx = 0;
	   ecmtask[i].rc = 0;
	}
    }

    reader->ncd_msgid=0;
    reader->last_s=reader->last_g=0;

    /* FIXME: this changes from r2318 only for CCcam */
    if (reader->typ == R_CCCAM) {
      if (reader->ph.cleanup) reader->ph.cleanup();
      if (client[cs_idx].typ == 'p') return;
    }
    /* END */

    if (reader->ph.c_init()) {
         cs_debug("network_tcp_connection_close() exit(1);");

       if (reader->ph.cleanup)
         reader->ph.cleanup();

         cs_exit(1);
    }
  }
}

static void casc_do_sock_log(struct s_reader * reader)
{
  int i, idx;
  ushort caid, srvid;
  ulong provid;

  idx=reader->ph.c_recv_log(&caid, &provid, &srvid);
  client[cs_idx].last=time((time_t)0);
  if (idx<0) return;        // no dcw-msg received

  for (i=1; i<CS_MAXPENDING; i++)
  {
    if (  (ecmtask[i].rc>=10)
       && (ecmtask[i].idx==idx)
       && (ecmtask[i].caid==caid)
       && (ecmtask[i].prid==provid)
       && (ecmtask[i].srvid==srvid))
    {
      casc_check_dcw(reader, i, 0, ecmtask[i].cw);  // send "not found"
      break;
    }
  }
}

static void casc_do_sock(struct s_reader * reader, int w)
{
  int i, n, idx, rc, j;
  uchar buf[1024];
  uchar dcw[16];

  if ((n=casc_recv_timer(reader, buf, sizeof(buf), w))<=0)
  {
    if (reader->ph.type==MOD_CONN_TCP && reader->typ != R_RADEGAST)
    {
      if (reader->ph.c_idle)
      	reader_do_idle(reader);
      else {
        cs_debug("casc_do_sock: close connection");
        network_tcp_connection_close(reader, client[cs_idx].udp_fd);
      }
    }
    return;
  }
  client[cs_idx].last=time((time_t)0);
  idx=reader->ph.c_recv_chk(dcw, &rc, buf, n);

  if (idx<0) return;  // no dcw received
  reader->last_g=time((time_t*)0); // for reconnect timeout
//cs_log("casc_do_sock: last_s=%d, last_g=%d", reader->last_s, reader->last_g);
  if (!idx) idx=last_idx;
  j=0;
  for (i=1; i<CS_MAXPENDING; i++)
  {

   if (ecmtask[i].idx==idx)
    {
      casc_check_dcw(reader, i, rc, dcw);
      j=1;
      break;
    }
  }
}

static void casc_get_dcw(struct s_reader * reader, int n)
{
  int w;
  struct timeb tps, tpe;
  tpe=ecmtask[n].tps;
  //tpe.millitm+=1500;    // TODO: timeout of 1500 should be config
  tpe.millitm+=cfg->srtimeout;
  tpe.time+=(tpe.millitm/1000);
  tpe.millitm%=1000;
  
  cs_ftime(&tps);
  while (((w=1000*(tpe.time-tps.time)+tpe.millitm-tps.millitm)>0)
          && (ecmtask[n].rc>=10))
  {
    casc_do_sock(reader, w);
    cs_ftime(&tps);
  }
  if (ecmtask[n].rc>=10)
    casc_check_dcw(reader, n, 0, ecmtask[n].cw);  // simulate "not found"
}



int casc_process_ecm(struct s_reader * reader, ECM_REQUEST *er)
{
  int rc, n, i, sflag;
  time_t t;//, tls;
  
  uchar buf[512];

  t=time((time_t *)0);
  for (n=0, i=sflag=1; i<CS_MAXPENDING; i++)
  {
    if ((t-(ulong)ecmtask[i].tps.time > ((cfg->ctimeout + 500) / 1000) + 1) &&
        (ecmtask[i].rc>=10))      // drop timeouts
        {
          ecmtask[i].rc=0;
        }
    if ((!n) && (ecmtask[i].rc<10))   // free slot found
      n=i;
    if ((ecmtask[i].rc>=10) &&      // ecm already pending
        (!memcmp(er->ecmd5, ecmtask[i].ecmd5, CS_ECMSTORESIZE)) &&
        (er->level<=ecmtask[i].level))    // ... this level at least
      sflag=0;
  }
  if (!n)
  {
    cs_log("WARNING: ecm pending table overflow !!");
    return(-2);
  }
  memcpy(&ecmtask[n], er, sizeof(ECM_REQUEST));
  if( reader->typ == R_NEWCAMD )
    ecmtask[n].idx=(reader->ncd_msgid==0)?2:reader->ncd_msgid+1;
  else
    ecmtask[n].idx=idx++;
  ecmtask[n].rc=10;
  cs_debug("---- ecm_task %d, idx %d, sflag=%d, level=%d", 
           n, ecmtask[n].idx, sflag, er->level);

  if( reader->ph.type==MOD_CONN_TCP && reader->tcp_rto )
  {
    int rto = abs(reader->last_s - reader->last_g);
    if (rto >= (reader->tcp_rto*60))
    {
      if (reader->ph.c_idle)
      	reader_do_idle(reader);
      else {
        cs_debug("rto=%d", rto);
        network_tcp_connection_close(reader, client[cs_idx].udp_fd);
      }
    }
  }

  cs_ddump_mask(D_ATR, er->ecm, er->l, "casc ecm:");
  rc=0;
  if (sflag)
  {
    if ((rc=reader->ph.c_send_ecm(&ecmtask[n], buf)))
      casc_check_dcw(reader, n, 0, ecmtask[n].cw);  // simulate "not found"
    else
      last_idx = ecmtask[n].idx;
    reader->last_s = t;   // used for inactive_timeout and reconnect_timeout in TCP reader

    if (!reader->ph.c_multi)
      casc_get_dcw(reader, n);
  }

//cs_log("casc_process_ecm 1: last_s=%d, last_g=%d", reader->last_s, reader->last_g);

  if (idx>0x1ffe) idx=1;
  return(rc);
}

static int reader_store_emm(uchar *emm, uchar type)
{
  static int rotate=0;
  int rc;
  memcpy(emmcache[rotate].emm, emm, emm[2]);
  emmcache[rotate].type=type;
  emmcache[rotate].count=1;
//  cs_debug("EMM stored (index %d)", rotate);
  rc=rotate;
  rotate=(rotate+1) % CS_EMMCACHESIZE;
  return(rc);
}

static void reader_get_ecm(struct s_reader * reader, ECM_REQUEST *er)
{
  //cs_log("hallo idx:%d rc:%d caid:%04X",er->idx,er->rc,er->caid);
  if ((er->rc<10) )
    {
      send_dcw(er);
      return;
    }
  er->ocaid=er->caid;
  if (!chk_bcaid(er, &reader->ctab))
  {
    cs_debug("caid %04X filtered", er->caid);
    er->rcEx=E2_CAID;
    er->rc=0;
    write_ecm_answer(reader, fd_c2m, er);
    return;
  }
  // cache2
  if (check_ecmcache2(er, client[er->cidx].grp))
  {
    er->rc=2;
    write_ecm_answer(reader, fd_c2m, er);
    return;
  }
  if (proxy)
  {
    client[cs_idx].last_srvid=er->srvid;
    client[cs_idx].last_caid=er->caid;
    casc_process_ecm(reader, er);
    return;
  }
  cs_ddump_mask(D_ATR, er->ecm, er->l, "ecm:");
  er->msglog[0] = 0;
  er->rc=reader_ecm(reader, er);
  write_ecm_answer(reader, fd_c2m, er);
  reader_post_process(reader);
  //fixme re-activated code for testing
  if(reader->typ=='r') reader->qlen--;
  //printf("queue: %d\n",reader->qlen);
}

static void reader_send_DCW(ECM_REQUEST *er)
{
  if ((er->rc<10) )
    {
      send_dcw(er);
    }
}

static int reader_do_emm(struct s_reader * reader, EMM_PACKET *ep)
{
  int i, no, rc, ecs;
  char *rtxt[] = { "error", proxy ? "sent" : "written", "skipped", "blocked" };
  char *typedesc[]= { "unknown", "unique", "shared", "global" };
  struct timeb tps, tpe;

  cs_ftime(&tps);

  no=0;
  for (i=ecs=0; (i<CS_EMMCACHESIZE) && (!ecs); i++)
          if (!memcmp(emmcache[i].emm, ep->emm, ep->emm[2]))
          {
                  if (reader->cachemm)
                          ecs=(reader->rewritemm > emmcache[i].count) ? 1 : 2;
                  else
                          ecs=1;
                  no=++emmcache[i].count;
                  i--;
          }

  if ((rc=ecs)<2)
  {
          if (proxy) {
                  cs_debug("network emm reader: %s" ,reader->label);

                  if (reader->ph.c_send_emm) {
                          rc=reader->ph.c_send_emm(ep);
                  } else {
                          cs_debug("send_emm() support missing");
                          rc=0;
                  }
          } else {
                  cs_debug("local emm reader: %s" ,reader->label);
                  rc=reader_emm(reader, ep);
          }

          if (!ecs)
          {
                  i=reader_store_emm(ep->emm, ep->type);
                  no=1;
          }
  }

  if (rc) client[cs_idx].lastemm=time((time_t)0);

#ifdef CS_LED
  if (rc) cs_switch_led(LED3, LED_BLINK_ON);
#endif

  if (reader->logemm & (1 << rc))
  {
    cs_ftime(&tpe);

    cs_log("%s emmtype=%s, len=%d, idx=%d, cnt=%d: %s (%d ms) by %s",
           username(ep->cidx), typedesc[emmcache[i].type], ep->emm[2],
           i, no, rtxt[rc], 1000*(tpe.time-tps.time)+tpe.millitm-tps.millitm, reader->label);
  }

#ifdef WEBIF
  //counting results
  switch(rc){
	  case 0:
		  reader->emmerror[ep->type]++;
		  break;
	  case 1:
		  reader->emmwritten[ep->type]++;
		  break;
	  case 2:
		  reader->emmskipped[ep->type]++;
		  break;
	  case 3:
		  reader->emmblocked[ep->type]++;
		  break;
  }
#endif

  return(rc);
}

static int reader_listen(struct s_reader * reader, int fd1, int fd2)
{
  int fdmax, tcp_toflag, use_tv=(!proxy);
  int is_tcp=(reader->ph.type==MOD_CONN_TCP);
  fd_set fds;
  struct timeval tv;

#ifdef CS_WITH_GBOX 
  if(reader->typ==R_GBOX) {
    struct timeb tpe;
    int x;
    ulong ms;
    cs_ftime(&tpe);
    for(x=0;x<CS_MAXPENDING;x++){
      ms=1000*(tpe.time-ecmtask[x].tps.time)+tpe.millitm-ecmtask[x].tps.millitm;
      if(ecmtask[x].rc == 10 && ms > cfg->ctimeout && reader->ridx == ecmtask[x].gbxRidx) {
        //cs_log("hello rc=%d idx:%d x:%d ridx%d ridx:%d",ecmtask[x].rc,ecmtask[x].idx,x,ridx,ecmtask[x].gbxRidx);
        ecmtask[x].rc=5;
        send_dcw(&ecmtask[x]);
      }
    }
  }
#endif
  if (reader->typ != R_SC8in1) {
    if (master_pid!=getppid()) cs_exit(0);
  }
  tcp_toflag=(fd2 && is_tcp && reader->tcp_ito && reader->tcp_connected);
  tv.tv_sec = 0;
  tv.tv_usec = 100000L;
  if (tcp_toflag)
  {
    tv.tv_sec = reader->tcp_ito*60;
    tv.tv_usec = 0;
    use_tv = 1;
  } 
  FD_ZERO(&fds);
  FD_SET(fd1, &fds);
  if (fd2) FD_SET(fd2, &fds);
  if (logfd) FD_SET(logfd, &fds);
  fdmax=(fd1>fd2) ? fd1 : fd2;
  fdmax=(fdmax>logfd) ? fdmax : logfd;
  if (select(fdmax+1, &fds, 0, 0, (use_tv) ? &tv : 0)<0) return(0);

  if (reader->typ != R_SC8in1) {
    if (master_pid!=getppid()) cs_exit(0);
  }
  if ((logfd) && (FD_ISSET(logfd, &fds)))
  {
    cs_debug("select: log-socket ist set");
    return(3);
  }

  if ((fd2) && (FD_ISSET(fd2, &fds)))
  {
    cs_debug("select: socket is set");
    return(2);
  }

  if (FD_ISSET(fd1, &fds))
  {
    if (tcp_toflag)
    {
      time_t now;
      int time_diff;
      time(&now);
      time_diff = abs(now-reader->last_s);
      if (time_diff>(reader->tcp_ito*60))
      {
        if (reader->ph.c_idle)
          reader_do_idle(reader);
        else {
          cs_debug("%s inactive_timeout (%d), close connection (fd=%d)", 
                  reader->ph.desc, time_diff, fd2);
          network_tcp_connection_close(reader, fd2);
        }
      }
    }
    cs_debug("select: pipe is set");
    return(1);
  }

  if (tcp_toflag)
  {
    if (reader->ph.c_idle)
      reader_do_idle(reader);
    else {
      cs_debug("%s inactive_timeout (%d), close connection (fd=%d)", 
             reader->ph.desc, tv.tv_sec, fd2);
      network_tcp_connection_close(reader, fd2);
    }
    return(0);
  }

  if (!proxy) reader_checkhealth(reader);
  return(0);
}

void reader_ext_card_info(struct s_reader * reader, int *data) {
	//data[0] = ridx, data[1] = dest_cs_idx
	if (reader->ph.c_report_cards)
		reader->ph.c_report_cards(data[1]);
}

static void reader_do_pipe(struct s_reader * reader)
{
  uchar *ptr;
  int pipeCmd = read_from_pipe(client[reader->cs_idx].fd_m2c_c, &ptr, 0);

  /* FIXME: this breaks camd35/newcamd cascading as this modules does not set tcp_connected = 2 */
  //if (reader->typ == R_CCCAM && reader->tcp_connected != 2 && client[cs_idx].typ == 'p')
  //  return;
 
  switch(pipeCmd)
  {
    case PIP_ID_ECM:
      reader_get_ecm(reader, (ECM_REQUEST *)ptr);
      break;
    case PIP_ID_DCW:
      reader_send_DCW((ECM_REQUEST *)ptr);
      break;  
    case PIP_ID_EMM:
      reader_do_emm(reader, (EMM_PACKET *)ptr);
      break;
    case PIP_ID_CIN: 
      reader_card_info(reader); 
      break;
    case PIP_ID_CCC:
      reader_ext_card_info(reader, (int*)ptr);
      break;
  }
}

void reader_do_idle(struct s_reader * reader)
{
  if (reader->ph.c_idle) 
    reader->ph.c_idle();
}

static void reader_main(struct s_reader * reader)
{
  while (1)
  {
    switch(reader_listen(reader, client[reader->cs_idx].fd_m2c_c, pfd))
    {
      case 0: reader_do_idle(reader); break;
      case 1: reader_do_pipe(reader)  ; break;
      case 2: casc_do_sock(reader, 0)   ; break;
      case 3: casc_do_sock_log(reader); break;
    }
  }
}

void * start_cardreader(void * rdr)
{
	struct s_reader * reader = (struct s_reader *) rdr; //FIXME can be made simpler
  cs_ptyp=D_READER;

  if ((proxy=reader->typ & R_IS_CASCADING))
  {
    client[cs_idx].typ='p';
    client[cs_idx].port=reader->r_port;
    strcpy(client[cs_idx].usr, reader->r_usr);
    switch(reader->typ)
    {
      case R_CAMD33  : module_camd33(&reader->ph); break;
      case R_CAMD35  : module_camd35(&reader->ph); break;
      case R_NEWCAMD : module_newcamd(&reader->ph); break;
      case R_RADEGAST: module_radegast(&reader->ph); break;
      case R_SERIAL  : module_oscam_ser(&reader->ph); break;
      case R_CS378X  : module_camd35_tcp(&reader->ph); break;
      case R_CCCAM   : module_cccam(&reader->ph); break;
      case R_CONSTCW : module_constcw(&reader->ph); break;
#ifdef CS_WITH_GBOX
      case R_GBOX    : module_gbox(&reader->ph);strcpy(client[cs_idx].usr, reader->label); break;
#endif
    }
    
    if (!(reader->ph.c_init)) {
      cs_log("FATAL: %s-protocol not supporting cascading", reader->ph.desc);
      cs_sleepms(1000);
      cs_exit(1);
    }
    
    if (reader->ph.c_init()) {
    	 if (reader->ph.cleanup) 
    	 	  reader->ph.cleanup();
    	 if (client[cs_idx].typ != 'p')
          cs_exit(1);
     }
    
    if ((reader->log_port) && (reader->ph.c_init_log))
      reader->ph.c_init_log();
  }
  else
  {
    client[cs_idx].ip=cs_inet_addr("127.0.0.1");
		if (reader->typ != R_SC8in1)
     	while (reader_device_init(reader)==2)
      	cs_sleepms(60000); // wait 60 secs and try again
  }

  emmcache=(struct s_emm *)malloc(CS_EMMCACHESIZE*(sizeof(struct s_emm)));
  if (!emmcache)
  {
    cs_log("Cannot allocate memory (errno=%d)", errno);
    cs_exit(1);
  }
  memset(emmcache, 0, CS_EMMCACHESIZE*(sizeof(struct s_emm)));

  ecmtask=(ECM_REQUEST *)malloc(CS_MAXPENDING*(sizeof(ECM_REQUEST)));
  if (!ecmtask)
  {
    cs_log("Cannot allocate memory (errno=%d)", errno);
    cs_exit(1);
  }
  memset(ecmtask, 0, CS_MAXPENDING*(sizeof(ECM_REQUEST)));
  reader_main(reader);
  cs_exit(0);
	return NULL; //dummy to prevent compiler error
}

