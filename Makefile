EMACS_SRC = /home/emartenson/src/emacs

CC = cc
CFLAGS = -g -Wall -I$(EMACS_SRC)/src -fPIC 
LDFLAGS = -g -lgssapi_krb5

MODULE = emacs-gssapi.so
OBJS = gssapi.o

all: $(MODULE)

$(MODULE): $(OBJS)
	$(CC) -shared $(LDFLAGS) -o $@ $<

clean:
	rm -f $(MODULE) $(OBJS)
