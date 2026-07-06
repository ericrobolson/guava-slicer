# Guava Slicer

Copyright (C) 2026 Eric Robert Olson

Open-source support preparation tool for FDM 3D printing.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

## Features

- **Layer-by-layer slice inspection** — step through each layer, visualize cross-section contours, catch problems before printing
- **Island detection** — automatically identify unsupported regions and highlight them for repair
- **Support generation** — generate FDM-compatible tree/column supports with manual placement and auto-generation
- **Raft generation** — configurable raft geometry under model and supports
- **Separate mesh export** — export model and supports as independent STLs for different print settings in Bambu Studio
- **Full undo/redo** — every operation is undoable
- **Project save/load** — persist your work across sessions

## Documentation

- [Project rules](CLAUDE.md) — invariants, conventions, verification
- [Design overview](DESIGN.md) — pillars, tech stack, workflows
- [Architecture](ARCHITECTURE.md) — directory structure, layer rules, dependency flow
- [Roadmap](ROADMAP.md) — phased implementation plan

## Build

```bash
make -s build    # compile backend + frontend
make -s run      # launch app
make -s test     # run all tests
make -s clean    # remove build artifacts
make -s cross    # cross-compile backend (macOS ARM/Intel + Windows x86_64)
```

## Headless Support Testing

A standalone CLI for testing support generation without the GUI. Runs the full pipeline (load STL → overhang analysis → slicing → island detection → support sampling → mesh generation) and exports the result as STL files.

```bash
# Build the CLI
make -s -C backend support-test

# Run against a model
./backend/zig-out/bin/support-test input.stl supports.stl              # supports only
./backend/zig-out/bin/support-test input.stl supports.stl combined.stl  # model + supports
```

Output includes per-stage stats (overhang count, island count, pre/post-dedup point counts, support mesh triangle count) for tuning the generation algorithm. Open the combined STL in any 3D viewer to visually inspect results.

## License

See [LICENSE](LICENSE).
