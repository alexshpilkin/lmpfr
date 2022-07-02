#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "gmp.h"
#include "mpfr.h"

enum {
	FRMETA = 1, ZMETA, /* FIXME Q, */ FMETA, NUPP1, NUP = NUPP1 - 1,
};

#if LUA_VERSION_NUM < 502
#define typerror(L, A, T) luaL_typerror((L), (A), (T))
#else
int typerror(lua_State *L, int narg, const char *tname) {
	const char *msg = lua_pushfstring(L, "%s expected, got %s",
	                                  tname, luaL_typename(L, narg));
	return luaL_argerror(L, narg, msg);
}
#endif

enum {
	FR = 0, Z, F, UI, SI, /* FIXME NI, */ D, NIL, STR, UNK,
};

static int type(lua_State *L, int idx) {
	switch (lua_type(L, idx)) {
	case LUA_TUSERDATA: {
		int ret = UNK; lua_getmetatable(L, idx);
		if (lua_rawequal(L, -1, lua_upvalueindex(FRMETA)))
			ret = FR;
		else if (lua_rawequal(L, -1, lua_upvalueindex(ZMETA)))
			ret = Z;
		else if (lua_rawequal(L, -1, lua_upvalueindex(FMETA)))
			ret = F;
		lua_pop(L, 1); return ret; }
	case LUA_TNUMBER: {
		int ok = 1;
#if LUA_VERSION_NUM < 503
		lua_Number n = lua_tonumber(L, idx);
#else
		lua_Integer n = lua_tointegerx(L, idx, &ok);
#endif
		if (ok && 0 <= n && n == (unsigned long)n) /* promotions! */
			return UI;
		if (ok && LONG_MIN <= n && n <= LONG_MAX && n == (long)n)
			return SI;
		return D; }
	case LUA_TNIL:
		return NIL;
	case LUA_TSTRING:
		return STR;
	}
	return UNK;
}

enum {
	FRFR = FR, FRZ = Z, ZFR = UNK + Z, FRF = F, FFR = UNK + F,
	FRUI = UI, UIFR = UNK + UI, FRSI = SI, SIFR = UNK + SI,
	FRD = D, DFR = UNK + D,
	BAD = UNK + UNK,
};

static int twotypes(lua_State *L, int one, int two) {
	int tyone = type(L, one), tytwo = type(L, two);
	if (tyone == FR) return tytwo;
	if (tytwo == FR) return UNK + tyone;
	return BAD;
}

#if LUA_VERSION_NUM < 503
#define toui(L, I) ((unsigned long)lua_tonumber((L), (I)))
#define tosi(L, I)          ((long)lua_tonumber((L), (I)))
#else
#define toui(L, I) ((unsigned long)lua_tointeger((L), (I)))
#define tosi(L, I)          ((long)lua_tointeger((L), (I)))
#endif
#define tod(L, I)  ((double)lua_tonumber((L), (I)))

#define tofr(L, I) (*(mpfr_t *)lua_touserdata((L), (I)))
#define toz(L, I)  (*(mpz_t  *)lua_touserdata((L), (I)))
#define tof(L, I)  (*(mpf_t  *)lua_touserdata((L), (I)))

static int isfr(lua_State *L, int idx) {
	int ret;
	if (lua_type(L, idx) != LUA_TUSERDATA || !lua_getmetatable(L, idx))
		return 0;
	ret = lua_rawequal(L, -1, lua_upvalueindex(FRMETA));
	lua_pop(L, 1); return ret;
}

static mpfr_t *checkfr(lua_State *L, int idx) {
	if (!isfr(L, idx))
		typerror(L, idx, "mpfr");
	return &tofr(L, idx);
}

#define PRD(L, P) do { \
	mpfr_t *self; lua_settop(L, 1); \
	self = checkfr(L, 1); \
	lua_pushboolean(L, mpfr_ ## P ## _p (*self)); return 1; \
} while (0)

static mpfr_prec_t checkprec(lua_State *L, int idx) {
#if LUA_VERSION_NUM < 503
	lua_Number prec = luaL_checknumber(L, idx);
#else
	lua_Integer prec = luaL_checkinteger(L, idx);
#endif
	luaL_argcheck(L, MPFR_PREC_MIN <= prec && prec <= MPFR_PREC_MAX,
	              idx, "precision out of range");
	return prec;
}

static const char opts[] = "AUDYZNF";
static const mpfr_rnd_t rnds[] =
	{MPFR_RNDA, MPFR_RNDU, MPFR_RNDD, MPFR_RNDA, MPFR_RNDZ, MPFR_RNDN, MPFR_RNDF};

static mpfr_rnd_t checkrnd(lua_State *L, int idx) {
	const char *opt, *optp;
	if (lua_isnil(L, idx))
		return mpfr_get_default_rounding_mode();
	opt = luaL_checkstring(L, idx);
	luaL_argcheck(L, opt[0] && !opt[1] && (optp = strchr(opts, toupper((unsigned char)opt[0]))),
	              idx, lua_pushfstring(L, "invalid rounding mode"));
	return rnds[optp - opts];
}

static mpfr_rnd_t settoprnd(lua_State *L, int low, int idx) {
	mpfr_rnd_t rnd;

	int top = lua_gettop(L); lua_settop(L, idx + 1);
	if (low < top && top <= idx && lua_isstring(L, top) && !lua_isnumber(L, top)) {
		lua_pushvalue(L, top);
		lua_remove(L, top);
	}

	rnd = checkrnd(L, idx + 1);
	lua_pop(L, 1); return rnd;
}

#define pushter(L, I) (lua_pushinteger((L), (I)), 2)

/* .1 Initialization functions */

static mpfr_t *newfr(lua_State *L) {
	mpfr_t *p = lua_newuserdata(L, sizeof *p);
	mpfr_init(*p);
	lua_pushvalue(L, lua_upvalueindex(FRMETA));
	lua_setmetatable(L, -2);
	return p;
}

static mpfr_t *checkfropt(lua_State *L, int idx) {
	mpfr_t *p;
	if (!lua_isnil(L, idx))
		return checkfr(L, idx);
	p = newfr(L); lua_replace(L, idx);
	return p;
}

#define UNF(L, F) do { \
	mpfr_rnd_t rnd = settoprnd(L, 0, 2); \
	mpfr_t *self = checkfr(L, 1), *res = checkfropt(L, 2); \
	return pushter(L, mpfr_ ## F (*res, *self, rnd)); \
} while (0)

#define UNF_UI(L, F) do { \
	mpfr_rnd_t rnd = settoprnd(L, 0, 2); \
	mpfr_t *res = checkfropt(L, 2); \
	\
	switch (type(L, 1)) { \
	case FR: return pushter(L, mpfr_ ## F (*res, tofr(L, 1), rnd)); \
	case UI: return pushter(L, mpfr_ ## F ## _ui (*res, toui(L, 1), rnd)); \
	default: return typerror(L, 1, "mpfr or non-negative integer"); \
	} \
} while (0)

#define UNF_SC(L, F) do { \
	mpfr_rnd_t rnd = settoprnd(L, 0, 3); \
	mpfr_t *self = checkfr(L, 1), \
	       *rs = checkfropt(L, 2), \
	       *rc = checkfropt(L, 3); \
	lua_pushinteger(L, mpfr_ ## F (*rs, *rc, *self, rnd)); \
	return 3; \
} while (0)

#define BIF(L, F) do { \
	mpfr_rnd_t rnd = settoprnd(L, 0, 3); \
	mpfr_t *self = checkfr(L, 1), *other = checkfr(L, 2), \
	       *res = checkfropt(L, 3); \
	return pushter(L, mpfr_ ## F (*res, *self, *other, rnd)); \
} while (0)

static int set(lua_State *L);

static int fr(lua_State *L) {
	newfr(L); lua_insert(L, 1);
	set(L); /* FIXME misleading errors */
	lua_pushvalue(L, 1); lua_insert(L, -2);
	return 2;
}

static int meth_gc(lua_State *L) {
	mpfr_t *p = checkfr(L, 1);
	mpfr_clear(*p); return 0;
}

static int set_default_prec(lua_State *L) {
	lua_settop(L, 1);
	mpfr_set_default_prec(checkprec(L, 1));
	return 0;
}

static int get_default_prec(lua_State *L) {
	lua_pushinteger(L, mpfr_get_default_prec()); return 1;
}

static int set_prec(lua_State *L) {
	mpfr_t *self; mpfr_prec_t prec; lua_settop(L, 2);
	self = checkfr(L, 1); prec = checkprec(L, 2);
	mpfr_set_prec(*self, prec);
	return 0;
}

static int get_prec(lua_State *L) {
	mpfr_t *self; lua_settop(L, 1);
	self = checkfr(L, 1);
	lua_pushinteger(L, mpfr_get_prec(*self)); return 1;
}

/* .2 Assignment functions */

static int set(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 2, 3);
	mpfr_t *self = checkfr(L, 1);

	switch (type(L, 2)) {
	case FR:  lua_pushinteger(L, mpfr_set(*self, tofr(L, 2), rnd)); break;
	case Z:   lua_pushinteger(L, mpfr_set_z(*self, toz(L, 2), rnd)); break;
	case F:   lua_pushinteger(L, mpfr_set_f(*self, tof(L, 2), rnd)); break;
	case UI:  lua_pushinteger(L, mpfr_set_ui(*self, toui(L, 2), rnd)); break;
	case SI:  lua_pushinteger(L, mpfr_set_si(*self, tosi(L, 2), rnd)); break;
	case D:   lua_pushinteger(L, mpfr_set_d(*self, tod(L, 2), rnd)); break;
	case NIL: break;
	case STR: {
		const char *s = lua_tostring(L, 2);
		int detect = lua_isnil(L, 3);
#if LUA_VERSION_NUM < 503
		lua_Number n = !detect ? luaL_checknumber(L, 3) : 0;
#else
		lua_Integer n = !detect ? luaL_checkinteger(L, 3) : 0;
#endif
		luaL_argcheck(L, detect || 2 <= n && n <= 62,
		              3, "base out of range");
		lua_pushinteger(L, mpfr_strtofr(*self, s, (char **)&s, n, rnd));
		while (isspace(*s)) s++;
		luaL_argcheck(L, !*s, 2, "invalid floating-point constant");
		break; }
	default:
		return typerror(L, 2, "mpfr, mpf, mpz, number, or string");
	}
	return 1;
}

/* .4 Conversion functions */

static int get_d(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 1);
	mpfr_t *self = checkfr(L, 1);
	lua_pushnumber(L, mpfr_get_d(*self, rnd)); return 1;
}

/* FIXME get_[us][ij] ? */

static int get_d_2exp(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 1);
	mpfr_t *self = checkfr(L, 1); long exp;
	lua_pushnumber(L, mpfr_get_d_2exp(&exp, *self, rnd));
#if LUA_VERSION_NUM < 503
	lua_pushnumber(L, exp);
#else
	lua_pushinteger(L, exp);
#endif
	return 2;
}

static int get_str(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 3);
	mpfr_t *self = checkfr(L, 1); int autosize = lua_isnil(L, 3);
#if LUA_VERSION_NUM < 503
	lua_Number  base = luaL_optnumber(L, 2, 10),
	            size = !autosize ? luaL_checknumber(L, 3) : 0;
#else
	lua_Integer base = luaL_optinteger(L, 2, 10),
	            size = !autosize ? luaL_checkinteger(L, 3) : 0;
#endif
	char *res; mpfr_exp_t exp;

	luaL_argcheck(L, -36 <= base && base <= -2 || 2 <= base && base <= 62,
	              2, "base out of range");
	luaL_argcheck(L, autosize || 1 <= size && size <= SIZE_MAX,
	              3, "size out of range");

	res = mpfr_get_str(NULL, &exp, base, size, *self, rnd);
	lua_pushstring(L, res); mpfr_free_str(res);
#if LUA_VERSION_NUM < 503
	lua_pushnumber(L, exp);
#else
	lua_pushinteger(L, exp);
#endif
	return 2;
}

#define FIT(L, T) do { \
	mpfr_rnd_t rnd = settoprnd(L, 0, 1); \
	mpfr_t *self = checkfr(L, 1); \
	lua_pushboolean(L, mpfr_fits_ ## T ## _p (*self, rnd)); return 1; \
} while (0)

static int fits_ulong   (lua_State *L) { FIT(L, ulong); }
static int fits_slong   (lua_State *L) { FIT(L, slong); }
static int fits_uint    (lua_State *L) { FIT(L, uint); }
static int fits_sint    (lua_State *L) { FIT(L, sint); }
static int fits_ushort  (lua_State *L) { FIT(L, ushort); }
static int fits_sshort  (lua_State *L) { FIT(L, sshort); }
static int fits_uintmax (lua_State *L) { FIT(L, uintmax); }
static int fits_intmax  (lua_State *L) { FIT(L, intmax); }

/* .5 Arithmetic functions */

static int add(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 3);
	mpfr_t *res = checkfropt(L, 3);
	int i, j;

	if (isfr(L, 1)) i = 1, j = 2; else
	if (isfr(L, 2)) i = 2, j = 1; else
	return luaL_error(L, "bad arguments (neither is mpfr)");

	switch (type(L, j)) {
	case FR: return pushter(L, mpfr_add(*res, tofr(L, i), tofr(L, j), rnd));
	case Z:  return pushter(L, mpfr_add_z(*res, tofr(L, i), toz(L, j), rnd));
	case UI: return pushter(L, mpfr_add_ui(*res, tofr(L, i), toui(L, j), rnd));
	case SI: return pushter(L, mpfr_add_si(*res, tofr(L, i), tosi(L, j), rnd));
	case D:  return pushter(L, mpfr_add_d(*res, tofr(L, i), tod(L, j), rnd));
	default: return typerror(L, j, "mpfr, mpz, or number");
	}
}

static int sub(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 3);
	mpfr_t *res = checkfropt(L, 3);

	switch (twotypes(L, 1, 2)) {
	case FR:   return pushter(L, mpfr_sub(*res, tofr(L, 1), tofr(L, 2), rnd));
	case FRZ:  return pushter(L, mpfr_sub_z(*res, tofr(L, 1), toz(L, 2), rnd));
	case ZFR:  return pushter(L, mpfr_z_sub(*res, toz(L, 1), tofr(L, 2), rnd));
	case FRUI: return pushter(L, mpfr_sub_ui(*res, tofr(L, 1), toui(L, 2), rnd));
	case UIFR: return pushter(L, mpfr_ui_sub(*res, toui(L, 1), tofr(L, 2), rnd));
	case FRSI: return pushter(L, mpfr_sub_si(*res, tofr(L, 1), tosi(L, 2), rnd));
	case SIFR: return pushter(L, mpfr_si_sub(*res, tosi(L, 1), tofr(L, 2), rnd));
	case FRD:  return pushter(L, mpfr_sub_d(*res, tofr(L, 1), tod(L, 2), rnd));
	case DFR:  return pushter(L, mpfr_d_sub(*res, tod(L, 1), tofr(L, 2), rnd));
	case BAD:  return luaL_error(L, "bad arguments (neither is mpfr)");
	default:   return typerror(L, isfr(L, 1) ? 2 : 1, "mpfr, mpz, or number");
	}
}

static int rsub(lua_State *L) {
	if (lua_gettop(L) < 2) lua_settop(L, 2);
	lua_pushvalue(L, 1); lua_pushvalue(L, 2);
	lua_replace(L, 1); lua_replace(L, 2);
	return sub(L); /* FIXME misleading errors */
}

static int mul(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 3);
	mpfr_t *res = checkfropt(L, 3);
	int i, j;

	if (isfr(L, 1)) i = 1, j = 2; else
	if (isfr(L, 2)) i = 2, j = 1; else
	return luaL_error(L, "bad arguments (neither is mpfr)");

	switch (type(L, j)) {
	case FR: return pushter(L, mpfr_mul(*res, tofr(L, i), tofr(L, j), rnd));
	case Z:  return pushter(L, mpfr_mul_z(*res, tofr(L, i), toz(L, j), rnd));
	case UI: return pushter(L, mpfr_mul_ui(*res, tofr(L, i), toui(L, j), rnd));
	case SI: return pushter(L, mpfr_mul_si(*res, tofr(L, i), tosi(L, j), rnd));
	case D:  return pushter(L, mpfr_mul_d(*res, tofr(L, i), tod(L, j), rnd));
	default: return typerror(L, j, "mpfr, mpz, or number");
	}
}

static int div(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 3);
	mpfr_t *res = checkfropt(L, 3);

	switch (twotypes(L, 1, 2)) {
	case FR:   return pushter(L, mpfr_div(*res, tofr(L, 1), tofr(L, 2), rnd));
	case FRZ:  return pushter(L, mpfr_div_z(*res, tofr(L, 1), toz(L, 2), rnd));
	case FRUI: return pushter(L, mpfr_div_ui(*res, tofr(L, 1), toui(L, 2), rnd));
	case UIFR: return pushter(L, mpfr_ui_div(*res, toui(L, 1), tofr(L, 2), rnd));
	case FRSI: return pushter(L, mpfr_div_si(*res, tofr(L, 1), tosi(L, 2), rnd));
	case SIFR: return pushter(L, mpfr_si_div(*res, tosi(L, 1), tofr(L, 2), rnd));
	case FRD:  return pushter(L, mpfr_div_d(*res, tofr(L, 1), tod(L, 2), rnd));
	case DFR:  return pushter(L, mpfr_d_div(*res, tod(L, 1), tofr(L, 2), rnd));
	case BAD:  return luaL_error(L, "bad arguments (neither is mpfr)");
	default:
		if (isfr(L, 1)) return typerror(L, 2, "mpfr, mpz, or number");
		else return typerror(L, 1, "mpfr or number");
	}
}

static int rdiv(lua_State *L) {
	if (lua_gettop(L) < 2) lua_settop(L, 2);
	lua_pushvalue(L, 1); lua_pushvalue(L, 2);
	lua_replace(L, 1); lua_replace(L, 2);
	return div(L); /* FIXME misleading errors */
}

static int sqrt_(lua_State *L) { UNF_UI(L, sqrt); }
static int rec_sqrt(lua_State *L) { UNF(L, rec_sqrt); }
static int cbrt_(lua_State *L) { UNF(L, cbrt); }

static int rootn(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 3);
	mpfr_t *self = checkfr(L, 1), *res = checkfropt(L, 3);
#if LUA_VERSION_NUM < 503
	lua_Number n = luaL_checknumber(L, 2);
#else
	lua_Integer n = luaL_checkinteger(L, 2);
#endif
	luaL_argcheck(L, 0 <= n && n <= ULONG_MAX,
	              2, "root degree out of range");
	return pushter(L, mpfr_rootn_ui(*res, *self, n, rnd));
}

static int neg(lua_State *L) { UNF(L, neg); }

static int meth_unm(lua_State *L) {
	lua_settop(L, 1);
	return neg(L);
}

static int abs_(lua_State *L) { UNF(L, abs); }

static int mul_2exp(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 3);
	mpfr_t *self = checkfr(L, 1), *res = checkfropt(L, 3);
#if LUA_VERSION_NUM < 503
	lua_Number n = luaL_checknumber(L, 2);
#else
	lua_Integer n = luaL_checkinteger(L, 2);
#endif
	if (0 <= n && n <= ULONG_MAX)
		return pushter(L, mpfr_mul_2ui(*res, *self, n, rnd));
	if (LONG_MIN <= n && n <= LONG_MAX)
		return pushter(L, mpfr_mul_2si(*res, *self, n, rnd));
	return luaL_argerror(L, 2, "exponent out of range");
}

static int div_2exp(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 3);
	mpfr_t *self = checkfr(L, 1), *res = checkfropt(L, 3);
#if LUA_VERSION_NUM < 503
	lua_Number n = luaL_checknumber(L, 2);
#else
	lua_Integer n = luaL_checkinteger(L, 2);
#endif
	if (0 <= n && n <= ULONG_MAX)
		return pushter(L, mpfr_div_2ui(*res, *self, n, rnd));
	if (LONG_MIN <= n && n <= LONG_MAX)
		return pushter(L, mpfr_div_2si(*res, *self, n, rnd));
	return luaL_argerror(L, 2, "exponent out of range");
}

/* .6 Comparison functions */

/* unlike the C version, propagates NaNs to output */
static int cmp(lua_State *L) {
	int i, j, res; lua_settop(L, 2);

	if (isfr(L, 1)) i = 1, j = 2; else
	if (isfr(L, 2)) i = 2, j = 1; else
	return luaL_error(L, "bad arguments (neither is mpfr)");

	switch (type(L, j)) {
	case FR: res = mpfr_cmp(tofr(L, i), tofr(L, j)); break;
	case Z:  res = mpfr_cmp_z(tofr(L, i), toz(L, j)); break;
	case F:  res = mpfr_cmp_f(tofr(L, i), tof(L, j)); break;
	case UI: res = mpfr_cmp_ui(tofr(L, i), toui(L, j)); break;
	case SI: res = mpfr_cmp_si(tofr(L, i), tosi(L, j)); break;
	case D:  res = mpfr_cmp_d(tofr(L, i), tod(L, j)); break;
	default: return typerror(L, j, "mpfr, mpz, mpf, or number");
	}

	lua_pushinteger(L, res);
	if (mpfr_erangeflag_p()) {
		if (mpfr_nan_p(tofr(L, i)))
			lua_pushvalue(L, i);
		else if (isfr(L, j) && mpfr_nan_p(tofr(L, j)))
			lua_pushvalue(L, j);
	}
	return 1;
}

static int nan_(lua_State *L)    { PRD(L, nan); }
static int inf(lua_State *L)     { PRD(L, inf); }
static int number(lua_State *L)  { PRD(L, number); }
static int zero(lua_State *L)    { PRD(L, zero); }
static int regular(lua_State *L) { PRD(L, regular); }

/* also propagates NaNs */
static int sgn(lua_State *L) {
	mpfr_t *self; lua_settop(L, 1);
	self = checkfr(L, 1);
	lua_pushinteger(L, mpfr_sgn(*self));
	if (mpfr_erangeflag_p() && mpfr_nan_p(*self))
		lua_pushvalue(L, 1);
	return 1;
}

#define REL(L, P) do { \
	mpfr_t *self, *other; lua_settop(L, 2); \
	self = checkfr(L, 1); other = checkfr(L, 2); \
	lua_pushboolean(L, mpfr_ ## P ## _p (*self, *other)); return 1; \
} while (0)

/* FIXME nan == nan */
static int lt(lua_State *L) { REL(L, less); }
static int le(lua_State *L) { REL(L, lessequal); }
static int eq(lua_State *L) { REL(L, equal); }
static int ge(lua_State *L) { REL(L, greaterequal); }
static int gt(lua_State *L) { REL(L, greater); }

/* .7 Transcendental functions */

static int log_  (lua_State *L) { UNF_UI(L, log); }
static int log2_ (lua_State *L) { UNF(L, log2); }
static int log10_(lua_State *L) { UNF(L, log10); }
static int log1p_(lua_State *L) { UNF(L, log1p); }
static int exp_  (lua_State *L) { UNF(L, exp); }
static int exp2_ (lua_State *L) { UNF(L, exp2); }
static int exp10_(lua_State *L) { UNF(L, exp10); }
static int expm1_(lua_State *L) { UNF(L, expm1); }

static int pow_(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 3);
	mpfr_t *res = checkfropt(L, 3);

	switch (twotypes(L, 1, 2)) {
	case FR:   return pushter(L, mpfr_pow(*res, tofr(L, 1), tofr(L, 2), rnd));
	case FRZ:  return pushter(L, mpfr_pow_z(*res, tofr(L, 1), toz(L, 2), rnd));
	case FRUI: return pushter(L, mpfr_pow_ui(*res, tofr(L, 1), toui(L, 2), rnd));
	case UIFR: return pushter(L, mpfr_ui_pow(*res, toui(L, 1), tofr(L, 2), rnd));
	case FRSI: return pushter(L, mpfr_pow_si(*res, tofr(L, 1), tosi(L, 2), rnd));
	case BAD:
		if (type(L, 1) == UI && type(L, 2) == UI)
			return pushter(L, mpfr_ui_pow_ui(*res, toui(L, 1), toui(L, 2), rnd));
		/* FIXME misleading error */
		return luaL_error(L, "bad arguments (neither is mpfr)");
	default:
		if (isfr(L, 1)) return typerror(L, 2, "mpfr, mpz, or integer");
		else return typerror(L, 1, "mpfr or non-negative integer");
	}
}

static int rpow(lua_State *L) {
	if (lua_gettop(L) < 2) lua_settop(L, 2);
	lua_pushvalue(L, 1); lua_pushvalue(L, 2);
	lua_replace(L, 1); lua_replace(L, 2);
	return pow_(L); /* FIXME misleading errors */
}

static int cos_(lua_State *L) { UNF(L, cos); }
static int sin_(lua_State *L) { UNF(L, sin); }
static int tan_(lua_State *L) { UNF(L, tan); }

static int sin_cos(lua_State *L) { UNF_SC(L, sin_cos); }

static int sec   (lua_State *L) { UNF(L, sec); }
static int csc   (lua_State *L) { UNF(L, csc); }
static int cot   (lua_State *L) { UNF(L, cot); }
static int acos_ (lua_State *L) { UNF(L, acos); }
static int asin_ (lua_State *L) { UNF(L, asin); }
static int atan_ (lua_State *L) { UNF(L, atan); }
static int atan2_(lua_State *L) { BIF(L, atan2); }

static int cosh_(lua_State *L) { UNF(L, cosh); }
static int sinh_(lua_State *L) { UNF(L, sinh); }
static int tanh_(lua_State *L) { UNF(L, tanh); }

static int sinh_cosh(lua_State *L) { UNF_SC(L, sinh_cosh); }

static int sech  (lua_State *L) { UNF(L, sech); }
static int csch  (lua_State *L) { UNF(L, csch); }
static int coth  (lua_State *L) { UNF(L, coth); }
static int acosh_(lua_State *L) { UNF(L, acosh); }
static int asinh_(lua_State *L) { UNF(L, asinh); }
static int atanh_(lua_State *L) { UNF(L, atanh); }

static int eint    (lua_State *L) { UNF(L, eint); }
static int li2     (lua_State *L) { UNF(L, li2); }
static int gamma_  (lua_State *L) { UNF(L, gamma); }
static int lngamma (lua_State *L) { UNF(L, lngamma); }

static int lgamma_(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 2);
	mpfr_t *self = checkfr(L, 1), *res = checkfropt(L, 2);
	int sign, ter = mpfr_lgamma(*res, &sign, *self, rnd);
	lua_pushinteger(L, sign); lua_pushinteger(L, ter);
	return 3;
}

static int digamma (lua_State *L) { UNF(L, digamma); }
static int beta    (lua_State *L) { BIF(L, beta); }
static int zeta    (lua_State *L) { UNF_UI(L, zeta); }
static int erf_    (lua_State *L) { UNF(L, erf); }
static int erfc_   (lua_State *L) { UNF(L, erfc); }

static int j0_(lua_State *L) { UNF(L, j0); }
static int j1_(lua_State *L) { UNF(L, j1); }

static int jn_(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 3);
	int isself = lua_isuserdata(L, 1),
	    selfidx = isself ? 1 : 2, nidx = isself ? 2 : 1;
#if LUA_VERSION_NUM < 503
	lua_Number n = luaL_checknumber(L, nidx);
#else
	lua_Integer n = luaL_checkinteger(L, nidx);
#endif
	mpfr_t *self = checkfr(L, selfidx), *res = checkfropt(L, 3);
	luaL_argcheck(L, LONG_MIN <= n && n <= LONG_MAX,
	              nidx, "index out of range");
	return pushter(L, mpfr_jn(*res, n, *self, rnd));
}

static int y0_(lua_State *L) { UNF(L, y0); }
static int y1_(lua_State *L) { UNF(L, y1); }

static int yn_(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 3);
	int isself = lua_isuserdata(L, 1),
	    selfidx = isself ? 1 : 2, nidx = isself ? 2 : 1;
#if LUA_VERSION_NUM < 503
	lua_Number n = luaL_checknumber(L, nidx);
#else
	lua_Integer n = luaL_checkinteger(L, nidx);
#endif
	mpfr_t *self = checkfr(L, selfidx), *res = checkfropt(L, 3);
	luaL_argcheck(L, LONG_MIN <= n && n <= LONG_MAX,
	              nidx, "index out of range");
	return pushter(L, mpfr_yn(*res, n, *self, rnd));
}

static int ai (lua_State *L) { UNF(L, ai); }
static int agm(lua_State *L) { BIF(L, agm); }

/* .9 Formatted output functions */

static int format(lua_State *L) {
	mpfr_t *p = checkfr(L, 1);
	const char *r = luaL_checkstring(L, 2);
	char *fmt, *w, *optp, *s;
	int idx = 2; int width = -1, prec = -1; mpfr_rnd_t rnd;
	lua_settop(L, 5);

	fmt = w = lua_newuserdata(L, strlen(r) + sizeof "%R*");
	*w++ = '%'; if (*r == '%') r++;
	while (*r == '0' || *r == '=' || *r == '+' || *r == ' ')
		*w++ = *r++;
	if (*r == '*') {
#if LUA_VERSION_NUM < 503
		lua_Number n = luaL_checknumber(L, ++idx);
#else
		lua_Integer n = luaL_checkinteger(L, ++idx);
#endif
		luaL_argcheck(L, 0 <= n && n <= INT_MAX,
		              idx, "width out of range");
		*w++ = *r++; width = n;
	} else while ('0' <= *r && *r <= '9')
		*w++ = *r++;
	if (*r == '.') {
		*w++ = *r++;
		if (*r == '*') {
#if LUA_VERSION_NUM < 503
			lua_Number n = luaL_checknumber(L, ++idx);
#else
			lua_Integer n = luaL_checkinteger(L, ++idx);
#endif
			luaL_argcheck(L, 0 <= n && n <= INT_MAX && n <= MPFR_PREC_MAX,
			              idx, "precision out of range");
			*w++ = *r++; prec = n;
		} else while ('0' <= *r && *r <= '9')
			*w++ = *r++;
	}
	*w++ = 'R'; if (*r == 'R') r++;
	*w++ = '*';
	if (*r && (optp = strchr(opts + 1, (unsigned char)*r))) {
		rnd = rnds[optp - opts]; r++;
	} else {
		rnd = checkrnd(L, ++idx);
		if (*r == '*') r++;
	}
	if (*r != 'A' && *r != 'a' && *r != 'b' && *r != 'E' && *r != 'e' &&
	    *r != 'F' && *r != 'f' && *r != 'G' && *r != 'g' ||
	    *(r + 1))
	{
		return luaL_argerror(L, 2, "invalid format specification");
	}
	*w++ = *r++; *w++ = 0;

	if (width != -1 && prec != -1)
		mpfr_asprintf(&s, fmt, width, prec, rnd, *p);
	else if (width != -1)
		mpfr_asprintf(&s, fmt, width, rnd, *p);
	else if (prec != -1)
		mpfr_asprintf(&s, fmt, prec, rnd, *p);
	else
		mpfr_asprintf(&s, fmt, rnd, *p);
	lua_pushstring(L, s); mpfr_free_str(s);
	return 1;
}

static int meth_tostring(lua_State *L) {
	lua_settop(L, 1);
	lua_pushstring(L, "g");
	return format(L);
}

static int meth_concat(lua_State *L) {
	int first; lua_settop(L, 2);

	if ( !(first = isfr(L, 1)) ) lua_insert(L, 1);
	lua_pushstring(L, "g"); lua_insert(L, 2);
	lua_pushnil(L); lua_insert(L, 3);
	/* mpfr "g" nil arg */
	format(L);
	/* ? ? ? arg ... str */
	lua_pushvalue(L, 4);
	if (!first) lua_insert(L, -2);

	lua_concat(L, 2); return 1;
}

/* .10 Integer and remainder related functions */

static int rint_(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 2);
	mpfr_t *self = checkfr(L, 1), *res = checkfropt(L, 2);
	return pushter(L, mpfr_rint(*res, *self, rnd));
}

#define RND(L, F) do { \
	mpfr_t *self, *res; lua_settop(L, 2); \
	self = checkfr(L, 1); res = checkfropt(L, 2); \
	return pushter(L, mpfr_ ## F (*res, *self)); \
} while (0)

static int ceil_     (lua_State *L) { RND(L, ceil); }
static int floor_    (lua_State *L) { RND(L, floor); }
static int round_    (lua_State *L) { RND(L, round); }
static int roundeven_(lua_State *L) { RND(L, roundeven); }
static int trunc_    (lua_State *L) { RND(L, trunc); }

static int integer(lua_State *L) { PRD(L, integer); }

/* .11 Rounding-related functions */

static int set_default_rounding_mode(lua_State *L) {
	mpfr_rnd_t rnd = checkrnd(L, 1);
	mpfr_set_default_rounding_mode(rnd);
	return 0;
}

static int get_default_rounding_mode(lua_State *L) {
	char mode;
	switch (mpfr_get_default_rounding_mode()) {
	case MPFR_RNDU: mode = 'U'; break;
	case MPFR_RNDD: mode = 'D'; break;
	case MPFR_RNDA: mode = 'A'; break;
	case MPFR_RNDZ: mode = 'Z'; break;
	case MPFR_RNDN: mode = 'N'; break;
	case MPFR_RNDF: mode = 'F'; break;
	}
	lua_pushlstring(L, &mode, 1); return 1;
}

static int prec_round(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 0, 2);
	mpfr_t *self = checkfr(L, 1); mpfr_prec_t prec = checkprec(L, 2);
	lua_pushinteger(L, mpfr_prec_round(*self, prec, rnd));
	return 1;
}

static void setfuncs(lua_State *L, int idx, const luaL_Reg *l, int nup) {
	lua_pushvalue(L, idx);
	for (; l->name; l++) {
		int i;
		for (i = 0; i < nup; i++) lua_pushvalue(L, -(nup + 1));
		lua_pushcclosure(L, l->func, nup);
		lua_setfield(L, -2, l->name);
	}
	lua_pop(L, 1);
}

static const struct luaL_Reg mod[] = {
	{"fr", fr},
	{"set_default_prec", set_default_prec},
	{"get_default_prec", get_default_prec},
	{"set_default_rounding_mode", set_default_rounding_mode},
	{"get_default_rounding_mode", get_default_rounding_mode},
	{"sqrt", sqrt_},
	{"log", log_},
	{"pow", pow_},
	{"atan2", atan2_},
	{"beta", beta},
	{"zeta", zeta},
	{"jn", jn_},
	{"yn", yn_},
	{"agm", agm},
	{0},
};

static const struct luaL_Reg met[] = {
	{"__gc",       meth_gc},
	{"__add",      add},
	{"__sub",      sub},
	{"__mul",      mul},
	{"__div",      div},
	/* FIXME __mod with quotient to -inf */
	{"__pow",      pow_},
	{"__unm",      meth_unm},
	{"__concat",   meth_concat},
	{"__lt",       lt},
	{"__le",       le},
	{"__eq",       eq},
	{"__ge",       ge},
	{"__gt",       gt},
	{"__tostring", meth_tostring},
	/* .1 Initialization functions */
	{"set_prec",   set_prec},
	{"get_prec",   get_prec},
	/* .2 Assignment functions */
	{"set",        set},
	/* .4 Conversion functions */
	{"get_d",      get_d},
	{"get_d_2exp", get_d_2exp},
	{"get_str",    get_str},
	{"fits_ulong",   fits_ulong},
	{"fits_slong",   fits_slong},
	{"fits_uint",    fits_uint},
	{"fits_sint",    fits_sint},
	{"fits_ushort",  fits_ushort},
	{"fits_sshort",  fits_sshort},
	{"fits_uintmax", fits_uintmax},
	{"fits_intmax",  fits_intmax},
	/* .5 Arithmetic functions */
	{"add",        add},
	{"sub",        sub},
	{"rsub",       rsub},
	{"mul",        mul},
	{"div",        div},
	{"rdiv",       rdiv},
	{"sqrt",       sqrt_},
	{"rsqrt",      rec_sqrt}, /* more common name */
	{"rec_sqrt",   rec_sqrt},
	{"cbrt",       cbrt_},
	{"rootn",      rootn},
	{"neg",        neg},
	{"abs",        abs_},
	{"mul_2exp",   mul_2exp},
	{"div_2exp",   div_2exp},
	/* .6 Comparison functions */
	{"cmp",        cmp},
	{"nan",        nan_},
	{"inf",        inf},
	{"number",     number},
	{"zero",       zero},
	{"regular",    regular},
	{"sgn",        sgn},
	/* .7 Transcendental functions */
	{"log",        log_},
	{"log2",       log2_},
	{"log10",      log10_},
	{"log1p",      log1p_},
	{"exp",        exp_},
	{"exp2",       exp2_},
	{"exp10",      exp10_},
	{"expm1",      expm1_},
	{"pow",        pow_},
	{"rpow",       rpow},
	{"cos",        cos_},
	{"sin",        sin_},
	{"tan",        tan_},
	{"sincos",     sin_cos}, /* more common name */
	{"sin_cos",    sin_cos},
	{"sec",        sec},
	{"csc",        csc},
	{"cot",        cot},
	{"acos",       acos_},
	{"asin",       asin_},
	{"atan",       atan_},
	{"atan2",      atan2_},
	{"cosh",       cosh_},
	{"sinh",       sinh_},
	{"tanh",       tanh_},
	{"sincosh",    sinh_cosh}, /* by analogy with sincos() */
	{"sinh_cosh",  sinh_cosh},
	{"sech",       sech},
	{"csch",       csch},
	{"coth",       coth},
	{"acosh",      acosh_},
	{"asinh",      asinh_},
	{"atanh",      atanh_},
	{"eint",       eint},
	{"li2",        li2},
	{"gamma",      gamma_},
	{"tgamma",     gamma_}, /* C99 name */
	{"lngamma",    lngamma},
	{"lgamma",     lgamma_},
	{"digamma",    digamma},
	{"beta",       beta},
	{"zeta",       zeta},
	{"erf",        erf_},
	{"erfc",       erfc_},
	{"j0",         j0_},
	{"j1",         j1_},
	{"jn",         jn_},
	{"y0",         y0_},
	{"y1",         y1_},
	{"yn",         yn_},
	{"ai",         ai},
	{"agm",        agm},
	/* .9 Formatted output functions */
	{"format",     format},
	/* .10 Integer and remainder related functions */
	{"rint",       rint_},
	{"ceil",       ceil_},
	{"floor",      floor_},
	{"round",      round_},
	{"roundeven",  roundeven_},
	{"trunc",      trunc_},
	{"integer",    integer},
	/* .11 Rounding-related functions */
	{"prec_round", prec_round},
	{0},
};

static void loadgmp(lua_State *L) {
	int frmeta = lua_gettop(L), gmp = frmeta + 1;

	lua_getglobal(L, "require");
	lua_pushstring(L, "gmp");
	if (lua_pcall(L, 1, 1, 0)) {
		lua_pop(L, 1); lua_newtable(L);
	}

	lua_getfield(L, gmp, "z");
	if (lua_pcall(L, 0, 1, 0) || !lua_isuserdata(L, -1) || !lua_getmetatable(L, -1))
		lua_pushvalue(L, frmeta);
	lua_remove(L, -2);

	lua_getfield(L, gmp, "f");
	if (lua_pcall(L, 0, 1, 0) || !lua_isuserdata(L, -1) || !lua_getmetatable(L, -1))
		lua_pushvalue(L, frmeta);
	lua_remove(L, -2);

	lua_remove(L, gmp);
}

#ifdef _WIN32
__declspec(dllexport)
#endif
int luaopen_mpfr(lua_State *L) {
	lua_settop(L, 0);

	lua_createtable(L, 0, sizeof mod / sizeof mod[0] - 1);

	lua_createtable(L, 0, sizeof met / sizeof met[0] - 1);
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	lua_pushvalue(L, -1); /* FRMETA */
	loadgmp(L); /* ZMETA, FMETA */

	setfuncs(L, 1, mod, NUP);
	setfuncs(L, 2, met, NUP);

	lua_settop(L, 1);
	return 1;
}
