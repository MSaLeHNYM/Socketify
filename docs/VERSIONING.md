# Versioning

Socketify uses [Semantic Versioning](https://semver.org/) with a single source of
truth: the root [`VERSION`](../VERSION) file (one line, `MAJOR.MINOR.PATCH`).

CMake reads `VERSION` for `project(... VERSION ...)`, generates
`socketify/version.h`, and writes `SocketifyConfigVersion.cmake` so
`find_package(Socketify 0.2)` can resolve the installed package.

## When to bump

| Kind | Form | Use for |
|------|------|---------|
| **patch** | `0.0.x` | Small fix / small commit |
| **minor** | `0.x.0` | Feature / “big” commit |
| **major** | `x.0.0` | Breaking / “revolution” change — **confirm explicitly** |

Do not auto-bump on every local WIP commit. When you land a shippable change,
bump before or with that commit:

```bash
./scripts/bump-version.sh patch   # or minor / major
```

Major bumps print a warning and require typing `yes`.

## Consumers

```cmake
find_package(Socketify 0.2 REQUIRED)
# Socketify_VERSION, Socketify_VERSION_MAJOR/MINOR/PATCH are set
```

```cpp
#include <socketify/version.h>
// SOCKETIFY_VERSION_STRING, socketify::version_string()
```

```bash
socketify --version
```
