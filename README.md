# EtwDemo

This is a demo meant to sample callstacks using ETW within its own process space, using example threads, and validate this behaviour in the main program.

## Building

This project targets Windows and requires the Windows SDK. Build with a command similar to:

```
cl /EHsc src\\main.cpp dbghelp.lib tdhl.lib evntrace.lib
```

Alternatively, open `EtwDemo.sln` in Visual Studio 2022 and build the `EtwDemo` project.

## Running

Run the resulting executable on Windows. The program launches worker threads, collects sampled profile events for every thread, aggregates stack traces into a binary tree, symbolicates them using `DbgHelp`, and asserts that at least one stack was captured.

