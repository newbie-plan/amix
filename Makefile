MAIN := main
SRC := ./*.c

INC := -I ./include/
INC += -I /usr/local/ffmpeg/include/
LIB := -L /usr/local/ffmpeg/lib/ -lavfilter -lavutil

${MAIN} : ${SRC}
	gcc $^ -o $@ ${INC} ${LIB}
clean:
	rm ${MAIN}
