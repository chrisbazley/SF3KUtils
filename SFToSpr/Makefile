# Project:   SFToSpr


# Toolflags:
CCCommonFlags = -c -depend !Depend -IC: -throwback -apcs 3/32/fpe2/swst/fp/nofpr -memaccess -L22-S22-L41
CCflags = $(CCCommonFlags) -DNDEBUG -Otime 
CCDebugFlags = $(CCCommonFlags) -g -DDEBUG_OUTPUT -DFORTIFY
Linkflags = -aif
LinkDebugFlags = $(Linkflags) -d
Squeezeflags = -v

include MakeCommon

DebugList = $(addprefix debug.,$(ObjectList))
ReleaseList = $(addprefix o.,$(ObjectList))
LibList = C:o.toolboxlib C:o.eventlib C:o.wimplib
DebugLibs = C:debug.CBLib C:debug.CBOSLib C:debug.StreamLib C:debug.GKeyLib C:o.CBDebugLib C:debug.CBUtilLib $(LibList) Fortify:o.fortify C:o.stubs
ReleaseLibs = C:o.flexlib C:o.CBLib C:o.CBOSLib C:o.StreamLib C:o.GKeyLib C:o.CBUtilLib $(LibList) C:o.stubsG
TestObjects = @.debug.Tests $(DebugList) $(DebugLibs)
DebugObjects = @.debug.Main $(DebugList) $(DebugLibs)
ReleaseObjects = @.o.Main $(ReleaseList) $(ReleaseLibs)

# Final targets:
all: @.!RunImage @.!RunImageD Tests

@.!RunImage: $(ReleaseObjects)
        link $(linkflags) -o $@ $(ReleaseObjects)
        Squeeze $(Squeezeflags) -o $@ @.!RunImage

@.!RunImageD: $(DebugObjects)
        link $(LinkDebugFlags) -o $@ $(DebugObjects)

@.Tests: $(TestObjects)
        link $(LinkDebugFlags) -o $@ $(TestObjects)

# User-editable dependencies:
.SUFFIXES: .o .c .debug
.c.debug:; cc $(CCDebugFlags) -o $@ $<
.c.o:; cc $(CCFlags) -o $@ $<

# Static dependencies:

# Dynamic dependencies:
