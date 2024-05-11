CC=gcc

CCFLAGS=-c -Wall -fPIC -ggdb -Wall -Werror
LDFLAGS=-shared
OBJDIR=./obj


all: glhooker.so

glhooker.so: ${OBJDIR}/glhooker.o
	@mkdir -p ${dir $@}
	${CC} ${LDFLAGS} -o $@ $^

${OBJDIR}/glhooker.o: gl_hooker.c gl_hooker.h
	@mkdir -p ${dir $@}
	${CC} ${CCFLAGS} -o $@ $<

install: glhooker.so /usr/lib/libglhooker.so /usr/include/gl_hooker.h

/usr/lib/libglhooker.so:
	cp glhooker.so $@
	ldconfig

/usr/include/gl_hooker.h:
	@mkdir -p ${dir $@}
	cp gl_hooker.h $@

uninstall:
	rm -f /usr/lib/libglhooker.so
	rm -f /usr/include/gl_hooker.h
clean: 
	rm -rf ${OBJDIR}
	rm -f ./glhooker.so

