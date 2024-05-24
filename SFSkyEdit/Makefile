# Project:   SFSkyEdit


# Toolflags:
CCCommonFlags = -c -depend !Depend -IC: -throwback -apcs 3/32/fpe2/swst/fp/nofpr -memaccess -L22-S22-L41
CCflags = $(CCCommonFlags) -DNDEBUG -Otime 
CCDebugFlags = $(CCCommonFlags) -g -DDEBUG_OUTPUT -DFORTIFY
Linkflags = -aif
LinkDebugFlags = $(Linkflags) -d
ObjAsmflags = -throwback -NoCache -depend !Depend
Squeezeflags = -v
LibFileFlags = -c -o $@

include MakeCommon

DebugList = $(addprefix debug.,$(ObjectList))
ReleaseList = $(addprefix o.,$(ObjectList))
LibList = C:o.toolboxlib C:o.eventlib C:o.wimplib
DebugLibs = @.debug.SFSkyEdLib C:debug.CBLib C:debug.CBOSLib C:debug.CBUtilLib C:debug.SF3KLib C:debug.StreamLib C:debug.GKeyLib C:o.CBDebugLib $(LibList) Fortify:o.fortify C:o.stubs
ReleaseLibs = C:o.flexlib C:o.CBLib C:o.CBOSLib C:o.CBUtilLib C:o.SF3KLib C:o.StreamLib C:o.GKeyLib $(LibList) C:o.stubsG

# Final targets:
all: @.!RunImage @.!RunImageD @.debug.SFSkyEdLib

@.!RunImage: @.o.Main $(ReleaseList) $(ReleaseLibs)
        link $(linkflags) -o $@ @.o.Main $(ReleaseList) $(ReleaseLibs)
        Squeeze $(Squeezeflags) -o $@ @.!RunImage

@.debug.SFSkyEdLib: $(DebugList)
        libfile $(LibFileFlags) $(DebugList)

@.!RunImageD: @.debug.Main $(DebugLibs)
        link $(LinkDebugFlags) -o $@ @.debug.Main $(DebugLibs)

# User-editable dependencies:
.SUFFIXES: .o .c .debug .s
.c.debug:; cc $(CCDebugFlags) -o $@ $<
.c.o:; cc $(CCFlags) -o $@ $<
.s.debug:; objasm $(ObjAsmFlags) -from $< -to $@
.s.o:; objasm $(ObjAsmFlags) -from $< -to $@

# Static dependencies:

# Dynamic dependencies:
