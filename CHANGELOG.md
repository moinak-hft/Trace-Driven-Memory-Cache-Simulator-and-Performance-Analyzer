# Project upgrade

The existing project was revised into **CacheLab: Trace-Driven Data Cache Simulation
with Machine-Learned Reuse Prediction**.

The original source and root files were backed up under
`backups/before-ml-cea4f4fd/` before changes. The bundled Pin SDK and its archive,
existing trace and historical submission PDFs are retained. The stale original
compiled simulator was removed from the root after verifying its backed-up copy;
Linux `make` builds the current version from source.

The upgrade replaces the simulator, tests, benchmark and tracer, restores an actual
workload executable source, and adds trained-model inference, causal feature export,
training/validation/test separation, automated reports, build files and documentation.
The updated project idea is recorded in `PROJECT_PROPOSAL.md`.

See `docs/EXPERIMENTS.md` for measured demonstration results and
`docs/VERIFICATION.md` for what was executed and the remaining Linux/Pin checks.
