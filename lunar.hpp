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
	static auto init_imp(T *o, lua_State *L, int)
		-> decltype(o->init(L), int()) {
		return o->init(L);
	}

	template <typename T>
	static int init_imp(T *o, lua_State *L, long) {
		return 0;
	}

	template <typename T>
	static int init(T *o, lua_State *L) {
		return init_imp(o, L, 0);
	}

	template <typename T>
	static auto exit_imp(T *o, lua_State *L, int)
		-> decltype(o->exit(L), int()) {
		return o->exit(L);
	}

	template <typename T>
	static auto exit_imp(T *o, lua_State *L, long)
		-> decltype(int()) {
		return 0;
	}

	template <typename T>
	static auto exit(T *o, lua_State *L)
		-> decltype(exit_imp(o, L, 0)) {
		return exit_imp(o, L, 0);
	}
};

template <typename T>
class Lunar {
public:
	static void Register(lua_State *L) {
		luaL_checktype(L, -1, LUA_TTABLE);
		int module = lua_gettop(L);

		lua_newtable(L);                                    // [-0,+1,e]
		int methods = lua_gettop(L);

		luaL_newmetatable(L, T::className);                 // [-0,+1,e]
		int metatable = lua_gettop(L);

		// store method table in module so that
		// scripts can add functions written in Lua.
		lua_pushvalue(L, methods);                          // [-0,+1,-]
		rawsetfield(L, module, T::className);               // [-1,+0,e]

		lua_pushstring(L, T::className);                    // [-0,+1,-]
		rawsetfield(L, metatable, LUAX_STR_TYPENAME);       // [-1,+0,e]

		// hide metatable from Lua getmetatable()
		lua_pushvalue(L, methods);                          // [-0,+1,-]
		rawsetfield(L, metatable, "__metatable");           // [-1,+0,e]

		lua_pushvalue(L, methods);                          // [-0,+1,-]
		rawsetfield(L, metatable, "__index");               // [-1,+0,e]

		lua_pushcfunction(L, newindex_T);                   // [-0,+1,-]
		rawsetfield(L, metatable, "__newindex");            // [-1,+0,e]

		lua_pushcfunction(L, tostring_T);                   // [-0,+1,-]
		rawsetfield(L, metatable, "__tostring");            // [-1,+0,e]

		lua_pushcfunction(L, gc_T);                         // [-0,+1,-]
		rawsetfield(L, metatable, "__gc");                  // [-1,+0,e]

		lua_newtable(L); // $mt (metatable of Class)                  // [-0,+1,e]
		lua_pushvalue(L, methods);                                    // [-0,+1,-]
		lua_pushcclosure(L, new_T, 1);                                // [-1,+1,e]
		lua_pushvalue(L, -1); // dup new_T function                   // [-0,+1,-]
		rawsetfield(L, methods, "new"); // add new_T to method table  // [-1,+0,e]
		rawsetfield(L, -2, "__call"); // $mt.__call = new_T           // [-1,+0,e]
		lua_setmetatable(L, methods);                                 // [-1,+0,-]

		// fill method table with methods from class T
		for (const typename LunarType<T>::Reg *l = T::methods; l->name; l++) {
			lua_pushlightuserdata(L, (void*)l);                // [-0,+1,e]
			lua_pushcclosure(L, thunk, 1);                     // [-1,+1,e]

			if (strncmp(l->name, "__", 2) == 0) {
				rawsetfield(L, metatable, l->name);              // [-1,+0,e]
			}
			else {
				rawsetfield(L, methods, l->name);                // [-1,+0,e]
			}
		}

		lua_pop(L, 2);  // drop metatable and method table
	}

	// call named lua method from userdata method table
	static int call(lua_State *L, const char *method,
					int nargs=0, int nresults=LUA_MULTRET, int errfunc=0)
	{
		int base = lua_gettop(L) - nargs;  // userdata index
		if (!luaX_checkclass(L, base, T::className, "self")) {
			lua_settop(L, base-1);           // drop userdata and args
			lua_pushfstring(L, "not a valid %s userdata", T::className);
			return -1;
		}

		lua_getfield(L, base, method);             // get method from userdata
		if (lua_isnil(L, -1)) {            // no method?
			lua_settop(L, base-1);           // drop userdata and args
			lua_pushfstring(L, "%s missing method '%s'", T::className, method);
			return -1;
		}
		lua_insert(L, base);               // put method under userdata, args

		int status = lua_pcall(L, 1+nargs, nresults, errfunc);  // call method
		if (status != LUA_OK) {
			const char *msg = lua_tostring(L, -1);
			if (msg == NULL) {
				msg = "(error with no message)";
			}

			const char *errtype = luaX_status2str(status);
			lua_pushfstring(L, "%s when calling %s:%s:\n%s\n", errtype, T::className, method, msg);
			lua_remove(L, base);             // remove old message
			return -1;
		}

		return lua_gettop(L) - base + 1;   // number of results
	}

	// push onto the Lua stack a userdata containing a pointer to T object
	static int push(lua_State *L, T *obj, bool gc = false) {
		if (obj == nullptr) {
			lua_pushnil(L);
			return 0;
		}

		luaL_getmetatable(L, T::className);  // lookup metatable in Lua registry
		if (lua_isnil(L, -1)) {
			luaL_error(L, "'%s' missing metatable", T::className);
		}

		int mt = lua_gettop(L);
		subtable(L, mt, "userdata", "v"); // mt.userdata = {}

		// FIXME Why dont we use light/full userdata here? For non-gc instances let Lua allocate and then call placement new on it! Otherwise call plain new and create a light userdata!

		T **ud = static_cast<T**>(pushuserdata(L, obj, sizeof(T*))); // mt.userdata[obj] = ud
		if (ud != nullptr) {
			*ud = obj;  // store pointer to object in userdata
			lua_pushvalue(L, mt);
			lua_setmetatable(L, -2); // ud.__metatable = mt

			if (gc == false) {
				lua_checkstack(L, 3);
				subtable(L, mt, "do not trash", "k"); // mt.do_not_trash = {}
				lua_pushvalue(L, -2); // dup ud
				lua_pushboolean(L, true);
				lua_settable(L, -3); // mt.do_not_trash[ud] = true
				lua_pop(L, 1); // pop(ud)
			}
		}

		// top = ud

		lua_replace(L, mt);
		lua_settop(L, mt);

		return mt;  // index of userdata containing pointer to T object
	}

	// get userdata from Lua stack and return pointer to T object
	static T *check(lua_State *L, int narg, const char *argname = nullptr) {
		return *static_cast<T**>(luaX_checkclass(L, narg, T::className, argname));
	}

	// get userdata from Lua stack and return pointer to T object
	static T *test(lua_State *L, int narg) {
		T** obj = static_cast<T**>(luaX_testclass(L, narg, T::className));
		if (obj == nullptr) {
			return nullptr;
		}

		return *obj;
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

		push(L, obj);
		lua_insert(L, 1); // Push self below arguments for call to init()

		int nresult = LunarWrapper::init(obj, L);
		if (nresult != 0) {
			const char *msg = nullptr;
			int err = lua_gettop(L);
			if (err >= 0) {
				msg = lua_tostring(L, err);
			}
			if (msg == nullptr) {
				msg = "(error with no message)";
			}

			return luaL_error(L, "failed to initialise %s: %s", T::className, msg);
		}

		push(L, obj, true); // gc_T will delete this object

		return 1;           // userdata containing pointer to T object
	}

	// garbage collection metamethod
	static int gc_T(lua_State *L) {
		if (luaL_getmetafield(L, 1, "do not trash")) {
			lua_pushvalue(L, 1);  // dup userdata
			lua_gettable(L, -2);
			if (!lua_isnil(L, -1)) return 0;  // do not delete object
		}
		T *obj = *static_cast<T**>(lua_touserdata(L, 1));
		if (obj) {
			LunarWrapper::exit(obj, L);
			delete obj;  // call destructor for T objects
		}
		return 0;
	}

	/* Clone the class metatable to support overriding methods for one instance only */
	static int newindex_T (lua_State *L) {
		static const int instance = 1, key = 2, value = 3;

		lua_getmetatable(L, instance);                      // [-0,+1,-]
		int class_metatable = lua_gettop(L);

		lua_pushstring(L, "__index");                       // [-0,+1,e]
		lua_rawget(L, class_metatable);                     // [-1,+1,-]
		int class_methods = lua_gettop(L);


		lua_newtable(L);                                    // [-0,+1,e]
		int instance_methods = lua_gettop(L);

		lua_pushvalue(L, key);                              // [-0,+1,e]
		lua_pushvalue(L, value);                            // [-0,+1,e]
		lua_rawset(L, instance_methods);                    // [-2,+0,e]


		lua_newtable(L);                                    // [-0,+1,e]
		int instance_methods_metatable = lua_gettop(L);

		lua_pushvalue(L, class_methods);                    // [-0,+1,-]
		rawsetfield(L, instance_methods_metatable, "__index");// [-1,+0,e]

		lua_setmetatable(L, instance_methods);              // [-1,+0,-]


		lua_newtable(L);                                    // [-0,+1,e]
		int instance_metatable = lua_gettop(L);

		lua_pushvalue(L, class_metatable);                  // [-0,+1,-]
		rawsetfield(L, instance_metatable, LUAX_STR_CLASS); // [-1,+0,e]

		lua_pushvalue(L, instance_methods);                 // [-0,+1,-]
		rawsetfield(L, instance_metatable, "__index");      // [-1,+0,e]

		lua_pushvalue(L, instance_methods);                 // [-0,+1,-]
		rawsetfield(L, instance_metatable, "__newindex");   // [-1,+0,e]

		lua_pushvalue(L, instance);                         // [-0,+1,-]
		rawsetfield(L, instance_metatable, "__metatable");  // [-1,+0,e]

		lua_pushcfunction(L, tostring_T);                   // [-0,+1,-]
		rawsetfield(L, instance_metatable, "__tostring");   // [-1,+0,e]

		lua_pushcfunction(L, gc_T);                         // [-0,+1,-]
		rawsetfield(L, instance_metatable, "__gc");         // [-1,+0,e]

		lua_pushvalue(L, instance_metatable);               // [-0,+1,-]
		lua_setmetatable(L, instance);                      // [-1,+0,-]


		return 0;
	}

	static int tostring_T (lua_State *L) {
		char buff[32];
		T *obj = *static_cast<T**>(lua_touserdata(L, 1));
		sprintf(buff, "%p", obj);
		lua_pushfstring(L, "%s (%s)", T::className, buff);
		return 1;
	}

	static void rawsetfield(lua_State *L, int tindex, const char *key) { //> [-1,+0,e]
		if (tindex < 0) {
			tindex--;
		}

		lua_pushstring(L, key);                           // [-0,+1,e]
		lua_insert(L, -2);  // swap value and key         // [-1,+1,-]
		lua_rawset(L, tindex);                            // [-2,+0,e]
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
		lua_getfield(L, tindex, name);                       // [-0,+1,e]

		if (lua_isnil(L, -1)) {                              // [-0,+0,-]
			lua_pop(L, 1); // pop(nil)                         // [-1,+0,-]
			lua_checkstack(L, 3);                              // [-0,+0,v]
			weaktable(L, mode); // pushes a table              // [-0,+1,e]
			lua_pushvalue(L, -1); // dup                       // [-0,+1,-]
			rawsetfield(L, tindex, name); // t[name] = table   // [-1,+0,e]
		}
	} // leaves table on stack

	static void *pushuserdata(lua_State *L, void *key, size_t sz) {
		void *ud = nullptr;
		lua_pushlightuserdata(L, key);
		lua_gettable(L, -2);     // table[key]
		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);         // drop nil
			lua_checkstack(L, 3);
			ud = lua_newuserdata(L, sz);  // create new userdata
			lua_pushlightuserdata(L, key);
			lua_pushvalue(L, -2);  // dup ud
			lua_settable(L, -4);   // table[key] = ud
		}
		return ud;
	}
};

#endif
