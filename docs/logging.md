# Logging

Loaders, meshes, textures, shaders and the renderer report through one
spdlog logger (`src/log.h`, `EDGE_LOG_INFO(...)` and friends, fmt-style
arguments). pyedge points it at the file `$EDGE_LOG` names as its binding is loaded, before the first renderer or model
(rotating, 5 MB x 5; an empty `EDGE_LOG` is an error at `import pyedge`); without it nothing
is written to disk. Warnings and errors go to stderr either way. A C++ program that never calls
`edge::init_logging()` gets warnings and errors on stderr and nothing on
disk.

```python
pyedge.init_logging("logs/edge.log", level="debug")  # per-mesh vertex/index/bounds lines
pyedge.init_logging(None)                             # stderr only
```

`level` is `debug`, `info`, `warn` or `error`; anything else is a
`ValueError`, not a fallback. Debug lines are compiled in
(`SPDLOG_ACTIVE_LEVEL`), so turning them on needs no rebuild. spdlog is
fetched by CMake (`FetchContent`; a parent project that declares spdlog first shares its copy).

