# Project:   SFColours

# Tools
CC = clang
Link = gcc



# Toolflags:
CCCommonFlags = -DUSE_OPTIONAL -c -Wall -Wextra -Wsign-compare -pedantic -std=c99 -MMD -MP
CCFlags = $(CCCommonFlags) -DNDEBUG -O3 -MF $*.d
CCDebugFlags = $(CCCommonFlags) -g -DDEBUG_OUTPUT -DFORTIFY -MF $*D.d
LinkCommonFlags = -o $@
LinkFlags = $(LinkCommonFlags) $(addprefix -l,$(ReleaseLibs))
LinkDebugFlags = $(LinkCommonFlags) $(addprefix -l,$(DebugLibs))

include MakeCommon

DebugList = $(addsuffix .debug,$(ObjectList))
ReleaseList = $(addsuffix .o,$(ObjectList))
LibList = toolbox event wimp
DebugLibs =    $(LibList)
ReleaseLibs = flex CBLib CBOS Stream CBUtil GKey $(LibList)
TestObjects = Tests.debug $(DebugList) $(DebugLibs)
DebugObjects = Main.debug $(DebugList) $(DebugLibs)
ReleaseObjects = Main.o $(ReleaseList) $(ReleaseLibs)

# Final targets:
all: RunImageD

RunImageD: $(DebugObjects) Main.debug
	$(Link) $(DebugObjects) Main.debug

# User-editable dependencies:
.SUFFIXES: .o .c .debug
.c.debug:
	$(CC) $(CCDebugFlags) -o $@ $<
.c.o:
	$(CC) $(CCFlags) -o $@ $<

# Static dependencies:

# Dynamic dependencies:
