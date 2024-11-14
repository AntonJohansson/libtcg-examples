# libtcg-examples

Example multi-architecture static analysis tools using [libtcg](https://github.com/AntonJohansson/qemu-libtcg).

## Building

Firstly run
```
git submodule update --init
```
to fetch the `libtcg` submodule. Next, run
```
make libtcg
```
to build `libtcg` for all supported targets. This will produce a set of `build/libtcg-${target}.so` shared libraries, one for each architecture, that implement the Tinycode lifter for that architecture. Additionally, a `build/libtcg/libtcg-loader.so` library will be produced which simplifies `dlopen`ing different target-specific lifters from the same process.

Lastly,
```
make
```
will build `dump-ir`, linking against `libtcg-loader.so` for lifting, containing the example analyses.
