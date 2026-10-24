target=$(shell ls -d */ | sed "s;/;;g" | grep -v "ai_common")
target_clean=$(addsuffix _clean,$(target))
target_cleanall=$(addsuffix _cleanall,$(target))

.PHONY:all clean cleanall $(target) $(target_clean) target_cleanall ai_common ai_common_clean

all:$(target)
clean:$(target_clean)
cleanall:$(target_cleanall)

ai_common:
	@cd ai_common && make
ai_common_clean:
	@cd ai_common && make clean

$(target): ai_common
	@cd $@ && make
$(target_clean): ai_common_clean
	@cd $(subst _clean,,$@) && make clean
$(target_cleanall): ai_common_clean
	@cd $(subst _cleanall,,$@) && make clean && make cleanstream