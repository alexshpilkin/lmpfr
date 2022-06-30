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

static mpfr_t *checkfropt(lua_State *L, int idx) {
	mpfr_t *p;

	if (!lua_isnil(L, idx))
		return checkfr(L, idx);

	p = lua_newuserdata(L, sizeof *p);
	lua_pushvalue(L, lua_upvalueindex(FRMETA));
	lua_setmetatable(L, -2);
	lua_replace(L, idx);
	mpfr_init(*p); return p;
}

static int fr(lua_State *L) {
	mpfr_rnd_t rnd = settoprnd(L, 1, 3);

	mpfr_t *p = lua_newuserdata(L, sizeof *p);
	lua_pushvalue(L, lua_upvalueindex(FRMETA));
	lua_setmetatable(L, -2);

	switch (type(L, 1)) {
	case FR:  return pushter(L, mpfr_init_set(*p, tofr(L, 1), rnd));
	case Z:   return pushter(L, mpfr_init_set_z(*p, toz(L, 1), rnd));
	case F:   return pushter(L, mpfr_init_set_f(*p, tof(L, 1), rnd));
	case UI:  return pushter(L, mpfr_init_set_ui(*p, toui(L, 1), rnd));
	case SI:  return pushter(L, mpfr_init_set_si(*p, tosi(L, 1), rnd));
	case D:   return pushter(L, mpfr_init_set_d(*p, tod(L, 1), rnd));
	case NIL:
		mpfr_init(*p); return 1;
	case STR:
		mpfr_init(*p); {
		const char *s = lua_tostring(L, 1);
		int detect = lua_isnil(L, 2);
#if LUA_VERSION_NUM < 503
		lua_Number n = !detect ? luaL_checknumber(L, 2) : 0;
#else
		lua_Integer n = !detect ? luaL_checkinteger(L, 2) : 0;
#endif
		luaL_argcheck(L, detect || 2 <= n && n <= 62,
		              2, "base out of range");
		lua_pushnumber(L, mpfr_strtofr(*p, s, (char **)&s, n, rnd));
		while (isspace(*s)) s++;
		luaL_argcheck(L, !*s, 1, "invalid floating-point constant");
		return 2; }
	default:
		mpfr_init(*p); /* for later cleanup */
		return typerror(L, 1, "mpfr, mpf, mpz, number, or string");
	}
}

static int meth_gc(lua_State *L) {
	mpfr_t *p = checkfr(L, 1);
	mpfr_clear(*p); return 0;
}

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
	{0},
};

static const struct luaL_Reg met[] = {
	{"__gc",       meth_gc},
	{"__add",      add},
	{"__sub",      sub},
	{"__tostring", meth_tostring},
	{"add",        add},
	{"sub",        sub},
	{"rsub",       rsub},
	{"format",     format},
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
