param_file=$(realpath $(dir $(firstword $(MAKEFILE_LIST))))/../Makefile.param
include $(param_file)

AI_COMM_SRC=$(wildcard *.c)
AI_COMM_OBJ := $(AI_COMM_SRC:%.c=%.o)

.PHONY : clean all
all:$(AI_COMM_OBJ)
	
clean:
	@rm $(AI_COMM_OBJ) -f

%.o:%.c
	@$(CC) $(COMM_INC) $(CFLAGS) -c $< -o $@
	
