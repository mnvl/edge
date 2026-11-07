# Logging

Loaders, meshes, textures, shaders and the renderer report through one log (`src/log.h`, `EDGE_LOG_INFO(...)` and friends,
`std::format` arguments). Its lines at or above the level go to `std::cerr`:

```
[warning static_mesh.cc:136] mesh 'hull': vertex components not initialized: ...
```

The level is `warn` until it is set; a line below it formats nothing and does not evaluate its arguments, so debug lines need no
rebuild to turn on.

```cpp
edge::set_log_level(edge::LogLevel::debug);  // per-mesh vertex/index/bounds lines

// edge's lines into the program's own log, instead of std::cerr (nullptr gives them back)
edge::set_log_sink([](edge::LogLevel level, std::string_view message, char const *file, int line) {
	my_log(level, message, file, line);
});
```

The sink is called on whichever thread logs, perhaps on several at once; it must not log through edge, and must not throw. `file`
is the source file's name without its directory and lives as long as the program. edge writes no log file: a program that wants
one (on Android, where stderr reaches nowhere `adb` reads) gives a sink that writes it.

```python
pyedge.set_log_level("debug")  # 'debug', 'info', 'warn' or 'error'; anything else is a ValueError
```
