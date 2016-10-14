LIB=libmpsocket

LIBDIR=.libs
DEPSDIR=.deps

SRCS=http_parser.c mpsock_pool.c mpsock_connection.c mpsock_tcp.c mpsock_socket.c mpsock_collector.c mpsock_interface.c mpsock_dns.c mpsock_buffer.c mpsock_scheduler.c mpsock_scheduler_minimum_throughput_assurance.c mpsock_scheduler_algorithms.c mpsock_misc.c 
HEADERS=$(SRCS:.c=.h) libmpsocket.h

OBJDIR=.obj
_OBJ=$(SRCS:.c=.o)
OBJ = $(patsubst %,$(OBJDIR)/%,$(_OBJ))

CC=gcc
CFLAGS=-g -c -Wall

AR=ar
RM=rm -rf

OPT=-O2 -lpthread

# INFO LOG LEVEL
#LIB_LOG_LVL=4
#OBJ_LOG_LVL=4

# WARN LOG LEVEL
#LIB_LOG_LVL=3
#OBJ_LOG_LVL=3

# ERROR LOG LEVEL
LIB_LOG_LVL=2
OBJ_LOG_LVL=2

# lib build
# ====================================================
all: $(LIBDIR) $(DEPSDIR) $(OBJDIR) $(LIB).so 

$(LIBDIR):
	mkdir $(LIBDIR)

$(OBJDIR):
	mkdir $(OBJDIR)

$(DEPSDIR):
	mkdir $(DEPSDIR)

$(LIB).so: $(LIB).o
	$(CC) -g -shared $(LIBDIR)/$(LIB).o $(OBJ) -ldl -Wl,-soname -Wl,$(LIB).so.0 -o $(LIB).so -lpthread
	$(AR) cr $(LIBDIR)/$(LIB).a $(LIB).o
	ranlib $(LIBDIR)/$(LIB).a

$(LIB).o: $(LIB).c mpsock_http.h uthash.h $(OBJ)
	$(CC) -I. -DLOG_LEVEL=$(LIB_LOG_LVL) -g $(OPT) -MT $(LIB).lo -MD -MP -MF $(DEPSDIR)/$(LIB).Tpo -c $(LIB).c -fPIC -DPIC -o $(LIBDIR)/$(LIB).o
	$(CC) -I. -DLOG_LEVEL=$(LIB_LOG_LVL) -g $(OPT) -MT $(LIB).lo -MD -MP -MF $(DEPSDIR)/$(LIB).Tpo -c $(LIB).c -o $(LIB).o >/dev/null 2>&1
# ====================================================


# single object build
# ===================================================
$(OBJDIR)/%.o: %.c $(HEADERS)
	$(CC) -DHTTP_PARSER_STRICT=0 -DLOG_LEVEL=$(OBJ_LOG_LVL) $(CFLAGS) $(OPT) $< -fPIC -o $@
# ==================================================

# others
# ===========================================================
sanity: $(LIB).so
	@$(RM) 943451_423790771050103_1325449100_n.jpg*
	@echo " ";
	@echo "+----------------------------------------------------------+";
	@echo "| Downloading a file using the legacy WGET ...             |";
	@echo "+----------------------------------------------------------+";
	@wget -q -t 1 --no-cache --no-proxy --no-dns-cache --no-check-certificate http://sphotos-f.ak.fbcdn.net/hphotos-ak-ash3/943451_423790771050103_1325449100_n.jpg
	@md5sum 943451_423790771050103_1325449100_n.jpg
	@echo " ";
	@echo "+----------------------------------------------------------+";
	@echo "| Now, downloading the same file using WGET-over-mHTTP ... |";
	@echo "| (Connection: 1, Interface: 1, chunk size: 10KB)          |";
	@echo "+----------------------------------------------------------+";
	@sudo LD_PRELOAD=./libmpsocket.so CHUNK_SIZE_IN_KB=10 CONNECTIONS=1 INTERFACES=0 IPADDRS=0 wget -q -t 1 --no-cache --no-proxy --no-dns-cache --no-check-certificate http://sphotos-f.ak.fbcdn.net/hphotos-ak-ash3/943451_423790771050103_1325449100_n.jpg
	@md5sum 943451_423790771050103_1325449100_n.jpg.1
	@echo " ";
	@$(RM) 943451_423790771050103_1325449100_n.jpg*

clean:
	$(RM) $(LIB).o
	$(RM) $(LIB).so
	$(RM) $(OBJ)
	$(RM) $(DEPSDIR)/$(LIB).Tpo
	$(RM) $(LIBDIR)/$(LIB).a
	$(RM) $(LIBDIR)/$(LIB).o
	$(RM) $(LIBDIR)/$(LIB).so.0
	$(RM) $(LIBDIR)/$(LIB).la
	$(RM) $(LIBDIR)/$(LIB).so
	$(RM) $(LIBDIR)/$(LIB).so.0.0.0
	$(RM) $(LIBDIR)
	$(RM) $(DEPSDIR)
	$(RM) $(OBJDIR)

.PHONY:
