#ifndef LUNAR_HPP
#define LUNAR_HPP

#include <cstring>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include "lextlib.h"
}


#define LUNAR_METHOD(class, method) { #method, &class::method }

#define LUNAR_DECLARE(class) \
	static const char className[]; \
	static const typename LunarType<class>::Reg methods[]

#define LUNAR_DEFINE(class) \
	const char class::className[] = #class; \
	const typename LunarType<class>::Reg class::methods[] =


template <typename T>
struct LunarType {
	typedef struct {
		const char *name;
		int(T::*mfunc)(lua_State *);
	} Reg;
};


struct LunarWrapper {
	template <typename T>
	static auto onregister_imp(lua_State *L, int)
		-> decltype(T::onregister(L), int()) {
		return T::onregister(L);
	}

	template <typename T>
	static int onregister_imp(lua_State *L, long) {
		return 0;
	}

	template <typename T>
	static int onregister(lua_State *L) {
		return onregister_imp<T>(L, 0);
	}

	template <typename T>
	static auto oninit_imp(T *o, lua_State *L, int)
		-> decltype(o->oninit(L), int()) {
		return o->oninit(L);
	}

	template <typename T>
	static int oninit_imp(T *o, lua_State *L, long) {
		return 0;
	}

	template <typename T>
	static int oninit(T *o, lua_State *L) {
		return oninit_imp(o, L, 0);
	}

	template <typename T>
	static auto onpush_imp(T *o, lua_State *L, int)
		-> decltype(o->onpush(L), int()) {
		return o->onpush(L);
	}

	template <typename T>
	static int onpush_imp(T *o, lua_State *L, long) {
		return 0;
	}

	template <typename T>
	static int onpush(T *o, lua_State *L) {
		return onpush_imp(o, L, 0);
	}

	template <typename T>
	static auto ongc_imp(T *o, lua_State *L, int)
		-> decltype(o->ongc(L), int()) {
		return o->ongc(L);
	}

	template <typename T>
	static auto ongc_imp(T *o, lua_State *L, long)
		-> decltype(int()) {
		return 0;
	}

	template <typename T>
	static auto ongc(T *o, lua_State *L)
		-> decltype(ongc_imp(o, L, 0)) {
		return ongc_imp(o, L, 0);
	}

	template <typename T>
	static auto onextend_imp(T *o, lua_State *L, int)
		-> decltype(o->onextend(L), int()) {
		return o->onextend(L);
	}

	template <typename T>
	static int onextend_imp(T *o, lua_State *L, long) {
		return 0;
	}

	template <typename T>
	static int onextend(T *o, lua_State *L) {
		return onextend_imp(o, L, 0);
	}
};


template <typename T>
class Lunar {
public:
	static void Register(lua_State *L) {
		luaL_checktype(L, -1, LUA_TTABLE);
		int l_module = lua_gettop(L);

		lua_newtable(L);                                    // [-0,+1,e]
		int l_methods = lua_gettop(L);

		luaL_newmetatable(L, T::className);                 // [-0,+1,e]
		int l_metatable = lua_gettop(L);

		// store method table in module so that
		// scripts can add functions written in Lua.
		lua_pushvalue(L, l_methods);                        // [-0,+1,-]
		rawsetfield(L, l_module, T::className);             // [-1,+0,e]

		lua_pushstring(L, T::className);                    // [-0,+1,-]
		rawsetfield(L, l_metatable, LUAX_STR_TYPENAME);     // [-1,+0,e]

		lua_pushvalue(L, l_methods);                        // [-0,+1,-]
		rawsetfield(L, l_metatable, "__index");             // [-1,+0,e]

		lua_pushvalue(L, l_methods);                        // [-0,+1,-]
		rawsetfield(L, l_metatable, "__newindex");          // [-1,+0,e]

		lua_pushcfunction(L, tostring_T);                   // [-0,+1,-]
		rawsetfield(L, l_metatable, "__tostring");          // [-1,+0,e]

		lua_pushcfunction(L, gc_T);                         // [-0,+1,-]
		rawsetfield(L, l_metatable, "__gc");                // [-1,+0,e]

		lua_newtable(L); // $mt (metatable of method table)           // [-0,+1,e]
		int l_methods_mt = lua_gettop(L);

		lua_pushvalue(L, l_methods);                                  // [-0,+1,-]
		lua_pushcclosure(L, new_T, 1);                                // [-1,+1,e]
		lua_pushvalue(L, -1); // dup new_T function                   // [-0,+1,-]
		rawsetfield(L, l_methods, "new");                             // [-1,+0,e]
		lua_pushcfunction(L, extend);                                 // [-0,+1,e]
		rawsetfield(L, l_methods, "extend");                          // [-1,+0,e]

		rawsetfield(L, l_methods_mt, "__call");                       // [-1,+0,e]
		lua_setmetatable(L, l_methods);                               // [-1,+0,-]

		// fill method table with methods from class T
		for (const typename LunarType<T>::Reg *l = T::methods; l->name; l++) {
			lua_pushlightuserdata(L, (void*)l);                // [-0,+1,e]
			lua_pushcclosure(L, thunk, 1);                     // [-1,+1,e]

			if (strncmp(l->name, "__", 2) == 0) {
				rawsetfield(L, l_metatable, l->name);            // [-1,+0,e]
			}
			else {
				rawsetfield(L, l_methods, l->name);              // [-1,+0,e]
			}
		}

		subtable(L, l_metatable, "userdata", "v"); // mt.userdata = {}

		lua_settop(L, l_metatable);
		LunarWrapper::onregister<T>(L);

		lua_settop(L, l_module); // drop locals
	}

	// call named lua method from userdata method table
	static int call(lua_State *L, const char *method,
					int nargs=0, int nresults=LUA_MULTRET, int errfunc=0)
	{
		int l_base = lua_gettop(L) - nargs;  // userdata index
		if (!luaX_checkclass(L, l_base, T::className, "self")) {
			lua_settop(L, l_base-1);           // drop userdata and args
			lua_pushfstring(L, "not a valid %s userdata", T::className);
			return -1;
		}

		lua_getfield(L, l_base, method);   // get method from userdata
		if (lua_isnil(L, -1)) {            // no method?
			lua_settop(L, l_base-1);         // drop userdata and args
			lua_pushfstring(L, "%s missing method '%s'", T::className, method);
			return -1;
		}
		lua_insert(L, l_base);             // put method under userdata, args

		int status = lua_pcall(L, 1+nargs, nresults, errfunc);  // call method
		if (status != LUA_OK) {
			const char *msg = lua_tostring(L, -1);
			if (msg == nullptr) {
				msg = "(unknown error)";
			}

			const char *errtype = luaX_status2str(status);
			lua_pushfstring(L, "%s when calling %s:%s:\n%s\n", errtype, T::className, method, msg);
			lua_remove(L, l_base);           // remove old message
			return -1;
		}

		return lua_gettop(L) - l_base + 1; // number of results
	}

	// push onto the Lua stack a userdata containing a pointer to T object
	static int push(lua_State *L, T *obj, bool gc = false) {
		if (obj == nullptr) {
			return 0;
		}

		luaL_getmetatable(L, T::className); // [-0,+1,-]
		if (lua_isnil(L, -1)) {
			return luaL_error(L, "'%s' missing metatable", T::className);
		}
		int l_mt = lua_gettop(L);

		/* Was this pointer pushed already? Then pushuserdata pushed the existing copy */
		if (!pushuserdata(L, obj)) {
			return 1;
		}
		int l_ud = lua_gettop(L);

		lua_pushvalue(L, l_mt); // dup(mt)
		lua_setmetatable(L, l_ud); // mt(ud)=mt [-1,+0,-]

		if (!gc) {
			subtable(L, l_mt, "do not trash", "k");
			lua_pushvalue(L, l_ud);
			lua_pushboolean(L, true);
			lua_rawset(L, -3);
		}

		lua_settop(L, l_ud);
		lua_replace(L, l_mt);

		int nresult = LunarWrapper::onpush(obj, L);
		if (nresult != 0) {
			const char *msg = nullptr;
			int l_err = lua_gettop(L);
			if (l_err >= 0) {
				msg = lua_tostring(L, l_err);
			}
			if (msg == nullptr) {
				msg = "(unknown error)";
			}

			return luaL_error(L, "'%s' failed to prepare for push: %s", T::className, msg);
		}

		return 1;
	}

	// get userdata from Lua stack and return pointer to T object
	static T *check(lua_State *L, int narg, const char *argname = nullptr) {
		T **ud = static_cast<T**>(luaX_checkclass(L, narg, T::className, argname));
		if (ud == nullptr) {
			return nullptr;
		}
		return *ud;
	}

	// get userdata from Lua stack and return pointer to T object
	static T *test(lua_State *L, int narg) {
		T **ud = static_cast<T**>(luaX_testclass(L, narg, T::className));
		if (ud == nullptr) {
			return nullptr;
		}
		return *ud;
	}

	/* Detach an object from its class, allowing to override methods */
	static int extend(lua_State *L) { //> [-0,+0,e]
		const int self = lua_absindex(L, -1);

		if (!lua_getmetatable(L, self)) {                         // [-0,+(1|0),-]
			return luaL_error(L, "While extending: Expected '%s' to have a metatable", T::className);
		}
		int class_metatable = lua_gettop(L);

		/* New metatable(self) */
		lua_newtable(L);                                          // [-0,+1,e]
		int self_metatable = lua_gettop(L);

		/* Set connection to parent for LuaX type checks */
		lua_pushvalue(L, class_metatable);                        // [-0,+1,-]
		lua_setfield(L, self_metatable, LUAX_STR_CLASS);          // [-1,+0,e]

		/* Clone class_metatable into instance_metatable */
		lua_pushnil(L);                                           // [-0,+1,e]
		while (lua_next(L, class_metatable) != 0) {               // [-1,+(2|0),e]
			lua_pushvalue(L, -2);                                   // [-0,+1,-]
			lua_insert(L, -2);                                      // [-1,+1,-]
			lua_rawset(L, self_metatable);                          // [-2,+0,e]
		}

		/* New metatable(self).__index */
		lua_newtable(L);                                          // [-0,+1,e]
		int self_metatable_index = lua_gettop(L);

		/* New metatable(metatable(self).__index) */
		lua_newtable(L);                                          // [-0,+1,e]
		int self_metatable_index_metatable = lua_gettop(L);

		/* Set metatable(metatable(self).__index).__index */
		lua_getfield(L, class_metatable, "__index");              // [-0,+1,e]
		lua_setfield(L, self_metatable_index_metatable, "__index"); // [-1,+0,e]

		/* Set metatable(metatable(self).__index) */
		assert(lua_gettop(L) == self_metatable_index_metatable);
		lua_setmetatable(L, self_metatable_index);                // [-1,+0,-]

		/* Set metatable(self).__index */
		assert(lua_gettop(L) == self_metatable_index);
		lua_pushvalue(L, self_metatable_index);                   // [-0,+1,-]
		lua_setfield(L, self_metatable, "__index");               // [-1,+0,e]

		/* Set metatable(self).__newindex */
		assert(lua_gettop(L) == self_metatable_index);
		lua_setfield(L, self_metatable, "__newindex");            // [-1,+0,e]

		/* Set metatable(self) */
		assert(lua_gettop(L) == self_metatable);
		lua_setmetatable(L, self);                                // [-1,+0,-]

		/* Pop class metatable */
		assert(lua_gettop(L) == class_metatable);
		lua_pop(L, 1);                                            // [-1,+0,-]

		T *obj = check(L, self, "self");
		LunarWrapper::onextend(obj, L);

		assert(self == lua_gettop(L));

		return 1;
	}

private:
	Lunar();  // hide default constructor

	// member function dispatcher
	static int thunk(lua_State *L) {
		// stack has userdata, followed by method args
		T *obj = check(L, 1, "self");  // get 'self', or if you prefer, 'this'
		// FIXME lua_remove(L, 1);  // remove self so member function args start at index 1
		// get member function from upvalue
		typename LunarType<T>::Reg *l = static_cast<typename LunarType<T>::Reg*>(lua_touserdata(L, lua_upvalueindex(1)));
		return (obj->*(l->mfunc))(L);  // call member function
	}

	// create a new T object and
	// push onto the Lua stack a userdata containing a pointer to T object
	static int new_T(lua_State *L) {
		if (lua_gettop(L) > 0 && lua_compare(L, 1, lua_upvalueindex(1), LUA_OPEQ)) {
			lua_remove(L, 1); // use classname:new(), instead of classname.new()
		} // do nothing if argument 1 is an instance

		T *obj = new T(L);  // call constructor for T objects

		push(L, obj, true);
		lua_insert(L, 1); // Push self below arguments for call to oninit()

		int nresult = LunarWrapper::oninit(obj, L);
		if (nresult != 0) {
			const char *msg = nullptr;
			int l_err = lua_gettop(L);
			if (l_err >= 0) {
				msg = lua_tostring(L, l_err);
			}
			if (msg == nullptr) {
				msg = "(unknown error)";
			}

			return luaL_error(L, "'%s' failed to initialise: %s", T::className, msg);
		}

		lua_settop(L, 1);

		return 1;           // userdata containing pointer to T object
	}

	// garbage collection metamethod
	static int gc_T(lua_State *L) {
		if (luaL_getmetafield(L, 1, "do not trash")) {
			lua_pushvalue(L, 1);
			lua_rawget(L, -2);
			if (!lua_isnil(L, -1)) {
				return 0;
			}
			lua_pop(L, 2);
		}

		T **ud = static_cast<T**>(lua_touserdata(L, 1));
		if (ud == nullptr) {
			return 0;
		}

		T *obj = *ud;
		if (obj == nullptr) {
			return 0;
		}

		LunarWrapper::ongc(obj, L);
		// FIXME: Weird, sometimes we try to delete a pointer to somewhere *inside* the T object here...
		delete obj;

		luaL_getmetatable(L, T::className); // [-0,+1,-]
		if (lua_isnil(L, -1)) {
			return luaL_error(L, "'%s' missing metatable", T::className);
		}

		lua_getfield(L, -1, "userdata"); // [-0,+1,e]
		lua_pushlightuserdata(L, obj); // [-0,+1,-]
		lua_pushnil(L); // [-0,+1,-]
		lua_rawset(L, -3); // [-2,+0,e]

		return 0;
	}

	static int tostring_T(lua_State *L) {
		char buff[32];
		T **ud = static_cast<T**>(lua_touserdata(L, 1));
		if (ud == nullptr) {
			return luaL_argerror(L, 1, "userdata expected");
		}
		T *obj = *ud;
		sprintf(buff, "%p", obj);
		lua_pushfstring(L, "%s (%s)", T::className, buff);
		return 1;
	}

	static void rawsetfield(lua_State *L, int tindex, const char *key) { //> [-1,+0,e]
		int abs_tindex = lua_absindex(L, tindex);
		lua_pushstring(L, key);                           // [-0,+1,e]
		lua_insert(L, -2);  // swap value and key         // [-1,+1,-]
		lua_rawset(L, abs_tindex);                            // [-2,+0,e]
	}

	/*! Create a weak table on top of stack */
	static void weaktable(lua_State *L, const char *mode) { //> [-0,+1,e]
		lua_newtable(L);                                       // [-0,+1,e]
		lua_pushvalue(L, -1); // dup                           // [-0,+1,-]
		lua_setmetatable(L, -2); // table.__metatable = table  // [-1,+0,-]
		lua_pushstring(L, mode);                               // [-0,+1,e]
		rawsetfield(L, -2, "__mode"); // table.__mode = mode   // [-1,+0,e]
	} // leaves table on stack

	/*! Create table "name" within table at index tindex */
	static void subtable(lua_State *L, int tindex, const char *name, const char *mode) { //> [-0,+1,e]
		int abs_tindex = lua_absindex(L, tindex);
		lua_getfield(L, abs_tindex, name);                       // [-0,+1,e]

		if (lua_isnil(L, -1)) {                              // [-0,+0,-]
			lua_pop(L, 1); // pop(nil)                         // [-1,+0,-]
			lua_checkstack(L, 3);                              // [-0,+0,v]
			weaktable(L, mode); // pushes a table              // [-0,+1,e]
			lua_pushvalue(L, -1); // dup                       // [-0,+1,-]
			rawsetfield(L, abs_tindex, name); // t[name] = table   // [-1,+0,e]
		}
	} // leaves table on stack

	static bool pushuserdata(lua_State *L, T *obj) {
		lua_getfield(L, -1, "userdata");

		lua_pushlightuserdata(L, obj);
		lua_gettable(L, -2);     // table[obj]
		if (!lua_isnil(L, -1)) {
			lua_replace(L, -2); // drop(table.userdata)
			return false;
		}

		lua_pop(L, 1);         // drop nil
		lua_checkstack(L, 3);

		T **ud = static_cast<T**>(lua_newuserdata(L, sizeof(T*)));  // create new userdata
		if (ud == nullptr) {
			return luaL_error(L, "Failed to allocate '%s'", T::className);
		}

		*ud = obj;  // store pointer to object in userdata

		lua_pushlightuserdata(L, obj);
		lua_pushvalue(L, -2);  // dup ud
		lua_settable(L, -4);   // table[obj] = ud

		lua_replace(L, -2); // drop(table.userdata)

		return true;
	}
};

#endif
