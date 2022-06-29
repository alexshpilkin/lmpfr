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

enum {
	NIL = 0, FR, Z, F, UI, SI, /* FIXME NI, */ D, STR, UNK,
};

#if LUA_VERSION_NUM < 502
#define typerror(L, A, T) luaL_typerror((L), (A), (T))
#else
int typerror(lua_State *L, int narg, const char *tname) {
	static const char fmt[] = "%s expected, got %s";
	char msg[sizeof fmt + (sizeof "lightuserdata" - 3) * 2];
	sprintf(msg, fmt, tname, lua_typename(L, lua_type(L, narg)));
	return luaL_argerror(L, narg, msg);
}
#endif

static int type(lua_State *L, int idx) {
	switch (lua_type(L, idx)) {
	case LUA_TNIL:
		return NIL;
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
	case LUA_TSTRING:
		return STR;
	}
	return UNK;
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

static mpfr_t *checkfropt(lua_State *L, int idx) {
	mpfr_t *p;

	if (!lua_isnil(L, idx))
		return checkfr(L, idx);

	p = lua_newuserdata(L, sizeof *p); mpfr_init(*p);
	lua_pushvalue(L, lua_upvalueindex(FRMETA));
	lua_setmetatable(L, -2);
	lua_replace(L, idx);
	return p;
}

static int fr(lua_State *L) {
	mpfr_t *p;
	lua_settop(L, 2);

	p = lua_newuserdata(L, sizeof *p);
	lua_pushvalue(L, lua_upvalueindex(FRMETA));
	lua_setmetatable(L, -2);

	switch (type(L, 1)) {
	case NIL:
		mpfr_init(*p);
		return 1;
	case FR:
		lua_pushnumber(L, mpfr_init_set(*p, tofr(L, 1), MPFR_RNDN));
		return 2;
	case Z:
		lua_pushnumber(L, mpfr_init_set_z(*p, toz(L, 1), MPFR_RNDN));
		return 2;
	case F:
		lua_pushnumber(L, mpfr_init_set_f(*p, tof(L, 1), MPFR_RNDN));
		return 2;
	case UI:
		lua_pushnumber(L, mpfr_init_set_ui(*p, toui(L, 1), MPFR_RNDN));
		return 2;
	case SI:
		lua_pushnumber(L, mpfr_init_set_si(*p, tosi(L, 1), MPFR_RNDN));
		return 2;
	case D:
		lua_pushnumber(L, mpfr_init_set_d(*p, tod(L, 1), MPFR_RNDN));
		return 2;
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
		return luaL_error(L, "cannot initialize mpfr from %s",
		                  lua_typename(L, lua_type(L, 1)));
	}
}

static int add(lua_State *L) {
	mpfr_t *self, *res;

	lua_settop(L, 3);
	self = checkfr(L, 1); res = checkfropt(L, 3);

	switch (type(L, 2)) {
	case FR:
		lua_pushnumber(L, mpfr_add(*res, *self, tofr(L, 2), MPFR_RNDN));
		return 2;
	case UI:
		lua_pushnumber(L, mpfr_add_ui(*res, *self, toui(L, 2), MPFR_RNDN));
		return 2;
	case SI:
		lua_pushnumber(L, mpfr_add_si(*res, *self, tosi(L, 2), MPFR_RNDN));
		return 2;
	case D:
		lua_pushnumber(L, mpfr_add_d(*res, *self, tod(L, 2), MPFR_RNDN));
		return 2;
	case Z:
		lua_pushnumber(L, mpfr_add_z(*res, *self, toz(L, 2), MPFR_RNDN));
		return 2;
	default:
		return luaL_error(L, "unsupported type");
	}
}

static int meth_add(lua_State *L) {
	lua_settop(L, 2);
	if (!isfr(L, 1)) lua_insert(L, 1);
	return add(L);
}

static int format(lua_State *L) {
	mpfr_t *p; const char *r; mpfr_prec_t prec = -1;
	char *fmt, *w, *s;

	lua_settop(L, 3);
	p = checkfr(L, 1);
	r = luaL_checkstring(L, 2);

	fmt = w = lua_newuserdata(L, strlen(r) + 3);
	*w++ = '%'; if (*r == '%') r++;
	while (*r == '0' || *r == '=' || *r == '+' || *r == ' ')
		*w++ = *r++;
	while ('0' <= *r && *r <= '9')
		*w++ = *r++;
	if (*r == '.') {
		*w++ = *r++;
		if (*r == '*') {
#if LUA_VERSION_NUM < 503
			lua_Number n = luaL_checknumber(L, 3);
#else
			lua_Integer n = luaL_checkinteger(L, 3);
#endif
			*w++ = *r++; prec = n;
			luaL_argcheck(L, 0 <= prec && prec <= MPFR_PREC_MAX,
			              3, "precision out of range");
		} else {
			while ('0' <= *r && *r <= '9') *w++ = *r++;
			luaL_checktype(L, 3, LUA_TNIL);
		}
	}
	*w++ = 'R'; if (*r == 'R') r++;
	if (*r == 'U' || *r == 'D' || *r == 'Y' || *r == 'Z' || *r == 'N')
		*w++ = *r++;
	if (*r != 'A' && *r != 'a' && *r != 'b' && *r != 'E' && *r != 'e' &&
	    *r != 'F' && *r != 'f' && *r != 'G' && *r != 'g' ||
	    *(r + 1))
	{
		return luaL_argerror(L, 2, "invalid format specification");
	}
	*w++ = *r++; *w++ = 0;

	if (prec == -1)
		mpfr_asprintf(&s, fmt, *p);
	else
		mpfr_asprintf(&s, fmt, prec, *p);
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
	{"__add",      meth_add},
	{"__tostring", meth_tostring},
	{"add",        add},
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
