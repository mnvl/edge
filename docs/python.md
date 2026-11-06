# pyedge, the Python bindings

`pyedge` is built with the library (`EDGE_PYTHON`, on by default) for the interpreter CMake finds, or the one given:

```bash
cmake -S . -B build -DPython_EXECUTABLE=$PWD/.venv/bin/python
cmake --build build --target pyedge
```

Its tests are the ctest test `pyedge_py` (`pytest test -k pyedge`, with `PYTHONPATH`, `EDGE_PYTHON_BINDINGS_DIR` and
`EDGE_TEST_SHADER_DIR`, the compiled test shaders, set by ctest).

The calls that wait or read at length let other Python threads run (they release the GIL): `begin_frame`, `finish_frame`,
`submit_frame`, `wait_frames`, `batch_download_textures`, `Texture.download`, a model's `load_from_gltf` and `load_from_file`,
`ModelCache.load`, `build_octree`.  A renderer and what it made are still of one thread at a time (Threads): another thread may run
Python meanwhile, not use the same renderer.

