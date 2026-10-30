# SF3KUtils
A collection of utilities for editing game files in RISC OS. Includes SFToSpr, FednetCmp, SFSkyEdit and SFColours. 

The CMake option `PER_VIEW_SELECT` controls whether SFColours and SFSkyEdit
keep an independent selection in each view (`ON`, the existing default)
or share a selection between views of the same file (`OFF`). For example:

```
cmake -S . -B build -DPER_VIEW_SELECT=OFF
```

For native builds, define the preprocessor macro `PER_VIEW_SELECT` to `0`
or `1`; it defaults to `1` when omitted. CI compiles both modes with GCC
and the _Optional checker and runs the host core test suites. The RISC OS
application tests are compiled on hosts but require RISC OS to run.
