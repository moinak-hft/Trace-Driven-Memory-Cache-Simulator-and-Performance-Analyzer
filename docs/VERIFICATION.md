# Verification performed during the upgrade

## Executed

- Compiled the C++17 simulator with GCC 15.1.0 and `-O3 -Wall -Wextra -Wpedantic`.
- Compiled the real workload executable and checked equal ordinary/tiled matrix results.
- Ran the 23-test regression suite successfully.
- Configured and built the CMake/Ninja path with the available native compiler.
- Ran the complete 35-epoch model-training and 84-run held-out synthetic experiment.
- Checked the supplied legacy trace against the original LRU counts.
- Verified the original executable's backup hash before removing the stale root binary.
- Preserved the original trace, original PDF submissions, existing Pin kit and Git history.

## Environment limitation

The editing host is Windows. Invoking WSL from this sandbox returned
`Wsl/E_ACCESSDENIED`; invoking MSYS Bash was also blocked by host access controls.
The executed tests therefore used the available native MinGW GCC toolchain and
Python 3.14.4. The C++ source, Makefile, CMake build and capture helper target Linux.

Linux/WSL execution and building/running the Pin tool remain unverified on this host.
The Pin API calls were checked against the bundled kit's headers. The added Linux CI
workflow has not been run remotely. Execute `make test` and the capture helper in a
Linux environment to complete those environment-specific checks.

No project was published and no Git commit was created.
