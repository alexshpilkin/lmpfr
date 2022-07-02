# LMPFR

Wim Couwenberg's [LGMP][LGM] Lua binding to [GMP][] is excellent and widely
used, but the repertoire of floating-point routines in GMP is very limited.
Better arbitrary-precision floats in a GMP-compatible package are available in
[MPFR][MPF], but Harmut Henkel's [lmpfrlib][LMP] binding to it has a subtly
incompatible (and sometimes awkward) API and can't interoperate with LGMP's
types.

This is binding to MPFR that is API-compatible with LGMP and (if the latter is
found at load time) accepts its userdata objects wherever the C library accepts
GMP ones.  Unfortunately, this means it must be incompatible with lmpfrlib.

No documentation is currently available, but that for LGMP can be used for the
functions it provides, except that the versions here sometimes accept more
combinations of arguments (either because MPFR itself more, or because
arbitrary limitations of the binding have not been carried over).  Most of the
rest is transcendental functions with a straightforward interface.

Additionally, all functions that perform rounding accept the rounding mode as
an optional last argument (defaulting to the one set with
`mpfr.set_default_rounding_mode`) and return &minus;1, 0, or 1 as the last
result when the returned value is less than, equal to, or greater than the
exact one.  Accepted rounding modes are (case insensitive) `U` (towards
+&infin;), `D` (towards &minus;&infin;), `A` or `Y` (away from 0), `Z` (towards
0), `N` (to nearest, ties to even), and `F` (faithful).  Care should be taken
to trim the extra result when a call to such a function serves as the last
argument of a further function call.

Finally, unlike GMP floats, MPFR floats support NaNs.  Lua's equality semantics
make it impossible for those NaNs be unequal to themselves, as required by IEEE
754 and implemented in MPFR's comparisons, so while

```lua
print(mpfr.fr 'nan' == mpfr.fr 'nan')
```

prints `false`,

```lua
local nan = mpfr.fr 'nan'; print(nan == nan)
```

prints `true`.  As an awkward workaround,

```lua
local nan = mpfr.fr 'nan'; print(nan <= nan and nan >= nan)
```

does print `false`.

[LGM]: https://github.com/ImagicTheCat/lgmp
[GMP]: https://gmplib.org/
[MPF]: https://www.mpfr.org/
[LMP]: http://www.circuitwizard.de/lmpfrlib/
