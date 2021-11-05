/*
 * See Licensing and Copyright notice in naev.h
 */
/**
 * @file nlua_system.c
 *
 * @brief Lua system module.
 */
/** @cond */
#include <lauxlib.h>

#include "naev.h"
/** @endcond */

#include "nlua_system.h"

#include "array.h"
#include "land.h"
#include "land_outfits.h"
#include "log.h"
#include "map.h"
#include "map_overlay.h"
#include "nebula.h"
#include "nlua_commodity.h"
#include "nlua_faction.h"
#include "nlua_jump.h"
#include "nlua_planet.h"
#include "nlua_vec2.h"
#include "nluadef.h"
#include "rng.h"
#include "space.h"

/* System metatable methods */
static int systemL_cur( lua_State *L );
static int systemL_get( lua_State *L );
static int systemL_getAll( lua_State *L );
static int systemL_eq( lua_State *L );
static int systemL_name( lua_State *L );
static int systemL_nameRaw( lua_State *L );
static int systemL_faction( lua_State *L );
static int systemL_nebula( lua_State *L );
static int systemL_interference( lua_State *L );
static int systemL_jumpdistance( lua_State *L );
static int systemL_jumpPath( lua_State *L );
static int systemL_adjacent( lua_State *L );
static int systemL_jumps( lua_State *L );
static int systemL_asteroidFields( lua_State *L );
static int systemL_asteroid( lua_State *L );
static int systemL_asteroidPos( lua_State *L );
static int systemL_asteroidDestroyed( lua_State *L );
static int systemL_addGatherable( lua_State *L );
static int systemL_presences( lua_State *L );
static int systemL_planets( lua_State *L );
static int systemL_presence( lua_State *L );
static int systemL_radius( lua_State *L );
static int systemL_isknown( lua_State *L );
static int systemL_setknown( lua_State *L );
static int systemL_hidden( lua_State *L );
static int systemL_setHidden( lua_State *L );
static int systemL_mrkClear( lua_State *L );
static int systemL_mrkAdd( lua_State *L );
static int systemL_mrkRm( lua_State *L );
static int systemL_tags( lua_State *L );
static const luaL_Reg system_methods[] = {
   { "cur", systemL_cur },
   { "get", systemL_get },
   { "getAll", systemL_getAll },
   { "__eq", systemL_eq },
   { "__tostring", systemL_name },
   { "name", systemL_name },
   { "nameRaw", systemL_nameRaw },
   { "faction", systemL_faction },
   { "nebula", systemL_nebula },
   { "interference", systemL_interference },
   { "jumpDist", systemL_jumpdistance },
   { "jumpPath", systemL_jumpPath },
   { "adjacentSystems", systemL_adjacent },
   { "jumps", systemL_jumps },
   { "asteroidFields", systemL_asteroidFields },
   { "asteroid", systemL_asteroid },
   { "asteroidPos", systemL_asteroidPos },
   { "asteroidDestroyed", systemL_asteroidDestroyed },
   { "addGatherable", systemL_addGatherable },
   { "presences", systemL_presences },
   { "planets", systemL_planets },
   { "presence", systemL_presence },
   { "radius", systemL_radius },
   { "known", systemL_isknown },
   { "setKnown", systemL_setknown },
   { "hidden", systemL_hidden },
   { "setHidden", systemL_setHidden },
   { "mrkClear", systemL_mrkClear },
   { "mrkAdd", systemL_mrkAdd },
   { "mrkRm", systemL_mrkRm },
   { "tags", systemL_tags },
   {0,0}
}; /**< System metatable methods. */

/**
 * @brief Loads the system library.
 *
 *    @param env Environment to load system library into.
 *    @return 0 on success.
 */
int nlua_loadSystem( nlua_env env )
{
   nlua_register(env, SYSTEM_METATABLE, system_methods, 1);
   return 0; /* No error */
}

/**
 * @brief Lua system module.
 *
 * This module allows you to use the Star Systems from Lua.
 *
 * Typical example would be something like:
 * @code
 * cur = system.cur() -- Gets current system
 * sys = system.get( "Gamma Polaris" )
 * @endcode
 *
 * @luamod system
 */
/**
 * @brief Gets system at index.
 *
 *    @param L Lua state to get system from.
 *    @param ind Index position of system.
 *    @return The LuaSystem at ind.
 */
LuaSystem lua_tosystem( lua_State *L, int ind )
{
   return *((LuaSystem*) lua_touserdata(L,ind));
}
/**
 * @brief Gets system at index raising an error if type doesn't match.
 *
 *    @param L Lua state to get system from.
 *    @param ind Index position of system.
 *    @return The LuaSystem at ind.
 */
LuaSystem luaL_checksystem( lua_State *L, int ind )
{
   if (lua_issystem(L,ind))
      return lua_tosystem(L,ind);
   luaL_typerror(L, ind, SYSTEM_METATABLE);
   return 0;
}

/**
 * @brief Gets system (or system name) at index raising an error if type doesn't match.
 *
 *    @param L Lua state to get system from.
 *    @param ind Index position of system.
 *    @return The System at ind.
 */
StarSystem* luaL_validsystem( lua_State *L, int ind )
{
   LuaSystem ls;
   StarSystem *s;

   if (lua_issystem(L, ind)) {
      ls = luaL_checksystem(L, ind);
      s = system_getIndex( ls );
   }
   else if (lua_isstring(L, ind))
      s = system_get( lua_tostring(L, ind) );
   else {
      luaL_typerror(L, ind, SYSTEM_METATABLE);
      return NULL;
   }

   if (s == NULL)
      NLUA_ERROR(L, _("System is invalid"));

   return s;
}

/**
 * @brief Pushes a system on the stack.
 *
 *    @param L Lua state to push system onto.
 *    @param sys System to push.
 *    @return System just pushed.
 */
LuaSystem* lua_pushsystem( lua_State *L, LuaSystem sys )
{
   LuaSystem *s = (LuaSystem*) lua_newuserdata(L, sizeof(LuaSystem));
   *s = sys;
   luaL_getmetatable(L, SYSTEM_METATABLE);
   lua_setmetatable(L, -2);
   return s;
}

/**
 * @brief Checks to see if ind is a system.
 *
 *    @param L Lua state to check.
 *    @param ind Index position to check.
 *    @return 1 if there is a system at index position.
 */
int lua_issystem( lua_State *L, int ind )
{
   int ret;

   if (lua_getmetatable(L,ind)==0)
      return 0;
   lua_getfield(L, LUA_REGISTRYINDEX, SYSTEM_METATABLE);

   ret = 0;
   if (lua_rawequal(L, -1, -2))  /* does it have the correct mt? */
      ret = 1;

   lua_pop(L, 2);  /* remove both metatables */
   return ret;
}

/**
 * @brief Gets the current system.
 *
 * @usage sys = system.cur() -- Gets the current system
 *
 *    @luatreturn System Current system.
 * @luafunc cur
 */
static int systemL_cur( lua_State *L )
{
   lua_pushsystem(L,system_index( cur_system ));
   return 1;
}

/**
 * @brief Gets a system.
 *
 * Behaves differently depending on what you pass as param: <br/>
 *    - string : Gets the system by raw (untranslated) name. <br/>
 *    - planet : Gets the system by planet. <br/>
 *
 * @usage sys = system.get( p ) -- Gets system where planet 'p' is located.
 * @usage sys = system.get( "Gamma Polaris" ) -- Gets the system by name.
 *
 *    @luatparam string|Planet param Read description for details.
 *    @luatreturn System System matching param.
 * @luafunc get
 */
static int systemL_get( lua_State *L )
{
   StarSystem *ss;
   Planet *pnt;

   /* Passing a string (systemname) */
   if (lua_isstring(L,1)) {
      ss = system_get( lua_tostring(L,1) );
   }
   /* Passing a planet */
   else if (lua_isplanet(L,1)) {
      pnt = luaL_validplanet(L,1);
      ss = system_get( planet_getSystem( pnt->name ) );
   }
   else NLUA_INVALID_PARAMETER(L);

   /* Error checking. */
   if (ss == NULL) {
      NLUA_ERROR(L, _("No matching systems found."));
      return 0;
   }

   /* return the system */
   lua_pushsystem(L,system_index(ss));
   return 1;
}

/**
 * @brief Gets all the systems.
 *    @luatreturn {System,...} A list of all the systems.
 * @luafunc getAll
 */
static int systemL_getAll( lua_State *L )
{
   StarSystem *sys = system_getAll();
   lua_newtable(L);
   for (int i=0; i<array_size(sys); i++) {
      lua_pushsystem( L, system_index( &sys[i] ) );
      lua_rawseti( L, -2, i+1 );
   }
   return 1;
}

/**
 * @brief Check systems for equality.
 *
 * Allows you to use the '==' operator in Lua with systems.
 *
 * @usage if sys == system.get( "Draygar" ) then -- Do something
 *
 *    @luatparam System s System comparing.
 *    @luatparam System comp System to compare against.
 *    @luatreturn boolean true if both systems are the same.
 * @luafunc __eq
 */
static int systemL_eq( lua_State *L )
{
   LuaSystem a, b;
   a = luaL_checksystem(L,1);
   b = luaL_checksystem(L,2);
   if (a == b)
      lua_pushboolean(L,1);
   else
      lua_pushboolean(L,0);
   return 1;
}

/**
 * @brief Returns the system's translated name.
 *
 * This translated name should be used for display purposes (e.g.
 * messages). It cannot be used as an identifier for the system; for
 * that, use system.nameRaw() instead.
 *
 * @usage name = sys:name() -- Equivalent to `_(sys:nameRaw())`
 *
 *    @luatparam System s System to get the translated name of.
 *    @luatreturn string The translated name of the system.
 * @luafunc name
 */
static int systemL_name( lua_State *L )
{
   StarSystem *sys = luaL_validsystem(L,1);
   lua_pushstring(L, _(sys->name));
   return 1;
}

/**
 * @brief Returns the system's raw (untranslated) name.
 *
 * This untranslated name should be used for identification purposes
 * (e.g. can be passed to system.get()). It should not be used directly
 * for display purposes without manually translating it with _().
 *
 * @usage name = sys:nameRaw()
 *
 *    @luatparam System s System to get the raw name of.
 *    @luatreturn string The raw name of the system.
 * @luafunc nameRaw
 */
static int systemL_nameRaw( lua_State *L )
{
   StarSystem *sys = luaL_validsystem(L,1);
   lua_pushstring(L, sys->name);
   return 1;
}

/**
 * @brief Gets system faction.
 *
 *    @luatparam System s System to get the faction of.
 *    @luatreturn Faction The dominant faction in the system.
 * @luafunc faction
 */
static int systemL_faction( lua_State *L )
{
   StarSystem *s = luaL_validsystem(L,1);
   if (s->faction == -1)
      return 0;
   lua_pushfaction(L,s->faction);
   return 1;

}

/**
 * @brief Gets the system's nebula parameters.
 *
 * @usage density, volatility, damage = sys:nebula()
 *
 *    @luatparam System s System to get nebula parameters from.
 *    @luatreturn number The density of the system.
 *    @luatreturn number The volatility of the system.
 *    @luatreturn number The amount of nebula damage done per second.
 * @luafunc nebula
 */
static int systemL_nebula( lua_State *L )
{
   StarSystem *s = luaL_validsystem(L,1);
   /* Push the density and volatility. */
   lua_pushnumber(L, s->nebu_density);
   lua_pushnumber(L, s->nebu_volatility);
   return 2;
}

/**
 * @brief Gets the system's interference level.
 *
 *    @luatparam System s System to get interference of.
 *    @luatreturn number The amount of interference (in percent).
 * @luafunc interference
 */
static int systemL_interference( lua_State *L )
{
   StarSystem *s = luaL_validsystem(L,1);
   lua_pushnumber( L, s->interference );
   return 1;
}

/**
 * @brief Gets jump distance from current system, or to another.
 *
 * Does different things depending on the parameter type:
 *    - nil : Gets distance to current system.
 *    - string : Gets distance to system matching name.
 *    - system : Gets distance to system
 *
 * @usage d = sys:jumpDist() -- Distance the current system to sys.
 * @usage d = sys:jumpDist( "Draygar" ) -- Distance from sys to system Draygar.
 * @usage d = sys:jumpDist( another_sys ) -- Distance from sys to another_sys.
 *
 *    @luatparam System s Starting system.
 *    @luatparam nil|string|Goal system param See description.
 *    @luatparam[opt=false] boolean hidden Whether or not to consider hidden jumps.
 *    @luatparam[opt=false] boolean known Whether or not to consider only jumps known by the player.
 *    @luatreturn number Number of jumps to system or math.huge if no path found.
 * @luafunc jumpDist
 */
static int systemL_jumpdistance( lua_State *L )
{
   StarSystem *sys;
   StarSystem **s;
   const char *start, *goal;
   int h, k;

   sys = luaL_validsystem(L,1);
   start = sys->name;
   h   = lua_toboolean(L,3);
   k   = !lua_toboolean(L,4);

   if (lua_gettop(L) > 1) {
      if (lua_isstring(L,2))
         goal = lua_tostring(L,2);
      else if (lua_issystem(L,2)) {
         StarSystem *sysp = luaL_validsystem(L,2);
         goal = sysp->name;
      }
      else
         NLUA_INVALID_PARAMETER(L);
   }
   else {
      goal  = sys->name;
      start = cur_system->name;
   }

   /* Trivial case same system. */
   if (strcmp(start,goal)==0) {
      lua_pushnumber(L, 0.);
      return 1;
   }

   s = map_getJumpPath( start, goal, k, h, NULL );
   if (s==NULL) {
      lua_pushnumber(L, HUGE_VAL);
      return 1;
   }

   lua_pushnumber(L,array_size(s));
   array_free(s);
   return 1;
}

/**
 * @brief Gets jump path from current system, or to another.
 *
 * Does different things depending on the parameter type:
 * <ul>
 *    <li>nil : Gets path to current system.</li>
 *    <li>string : Gets path to system with the given raw (untranslated) name.</li>
 *    <li>system : Gets path to system</li>
 * </ul>
 *
 * @usage jumps = sys:jumpPath( system.cur() ) -- Path from sys to current system.
 * @usage jumps = sys:jumpPath( "Draygar" ) -- Path from sys to Draygar.
 * @usage jumps = system.jumpPath( "Draygar", another_sys ) -- Path from Draygar to another_sys.
 *
 *    @luatparam System s Starting system.
 *    @luatparam System goal Goal system param See description.
 *    @luatparam[opt=false] boolean hidden Whether or not to consider hidden jumps.
 *    @luatreturn {Jump,...} Table of jumps.
 * @luafunc jumpPath
 */
static int systemL_jumpPath( lua_State *L )
{
   LuaJump lj;
   StarSystem *sys, *sysp;
   StarSystem **s;
   int sid, pushed, h;
   const char *start, *goal;

   h   = lua_toboolean(L,3);

   /* Foo to Bar */
   sys   = luaL_validsystem(L,1);
   start = sys->name;
   sid   = sys->id;
   sysp  = luaL_validsystem(L,2);
   goal  = sysp->name;

   s = map_getJumpPath( start, goal, 1, h, NULL );
   if (s == NULL)
      return 0;

   /* Create the jump table. */
   lua_newtable(L);
   pushed = 0;

   /* Map path doesn't contain the start system, push it manually. */
   lj.srcid  = sid;
   lj.destid = s[0]->id;

   lua_pushjump(L, lj);         /* value. */
   lua_rawseti(L, -2, ++pushed);

   for (int i=0; i<array_size(s)-1; i++) {
      lj.srcid  = s[i]->id;
      lj.destid = s[i+1]->id;

      lua_pushjump(L, lj);         /* value. */
      lua_rawseti(L, -2, ++pushed);
   }
   array_free(s);

   return 1;
}

/**
 * @brief Gets all the adjacent systems to a system.
 *
 * @usage for i, s in ipairs( sys:adjacentSystems() ) do -- Iterate over adjacent systems.
 *
 *    @luatparam System s System to get adjacent systems of.
 *    @luatparam[opt=false] boolean hidden Whether or not to show hidden jumps also.
 *    @luatreturn {System,...} An ordered table with all the adjacent systems.
 * @luafunc adjacentSystems
 */
static int systemL_adjacent( lua_State *L )
{
   int id, h;
   StarSystem *s;

   id = 1;
   s  = luaL_validsystem(L,1);
   h  = lua_toboolean(L,2);

   /* Push all adjacent systems. */
   lua_newtable(L);
   for (int i=0; i<array_size(s->jumps); i++) {
      LuaSystem sysp;
      if (jp_isFlag(&s->jumps[i], JP_EXITONLY ))
         continue;
      if (!h && jp_isFlag(&s->jumps[i], JP_HIDDEN))
         continue;
      sysp = system_index( s->jumps[i].target );
      lua_pushsystem(L, sysp); /* value. */
      lua_rawseti(L,-2,id++);
   }

   return 1;
}

/**
 * @brief Gets all the jumps in a system.
 *
 * @usage for i, s in ipairs( sys:jumps() ) do -- Iterate over jumps.
 *
 *    @luatparam System s System to get the jumps of.
 *    @luatparam[opt=false] boolean exitonly Whether to exclude exit-only jumps.
 *    @luatreturn {Jump,...} An ordered table with all the jumps.
 * @luafunc jumps
 */
static int systemL_jumps( lua_State *L )
{
   int exitonly, pushed;
   StarSystem *s;

   s = luaL_validsystem(L,1);
   exitonly = lua_toboolean(L,2);
   pushed = 0;

   /* Push all jumps. */
   lua_newtable(L);
   for (int i=0; i<array_size(s->jumps); i++) {
      LuaJump lj;
      /* Skip exit-only jumps if requested. */
      if ((exitonly) && (jp_isFlag( &s->jumps[i],  JP_EXITONLY)))
            continue;

      lj.srcid  = s->id;
      lj.destid = s->jumps[i].targetid;
      lua_pushjump(L,lj); /* value. */
      lua_rawseti(L,-2, ++pushed);
   }

   return 1;
}

/**
 * @brief Gets all the asteroid fields in a system.
 *
 * @usage for i, s in ipairs( sys:asteroidFields() ) do -- Iterate over asteroid fields
 *
 *    @luatparam System s System to get the asteroid fields of.
 *    @luatreturn {Table,...} An ordered table with all the asteroid fields.
 * @luafunc asteroidFields
 */
static int systemL_asteroidFields( lua_State *L )
{
   StarSystem *s = luaL_validsystem(L,1);
   /* Push all jumps. */
   lua_newtable(L);
   for (int i=0; i<array_size(s->asteroids); i++) {
      lua_newtable(L);              /* key, t */

      lua_pushstring(L,"pos");      /* key, t, k */
      lua_pushvector(L,s->asteroids[i].pos); /* key, t, k, v */
      lua_rawset(L,-3);

      lua_pushstring(L,"density");      /* key, t, k */
      lua_pushnumber(L,s->asteroids[i].density); /* key, t, k, v */
      lua_rawset(L,-3);

      lua_pushstring(L,"radius");      /* key, t, k */
      lua_pushnumber(L,s->asteroids[i].radius); /* key, t, k, v */
      lua_rawset(L,-3);

      lua_rawseti(L,-2,i+1);
   }
   return 1;
}

/**
 * @brief Gets a random asteroid in the current system
 *
 * @usage anchor, ast = system.asteroid()
 *
 *    @luatreturn int anchor Id of an asteroid anchor.
 *    @luatreturn int asteroid Id of an asteroid of this anchor.
 * @luafunc asteroid
 */
static int systemL_asteroid( lua_State *L )
{
   if (array_size(cur_system->asteroids) > 0) {
      int field = RNG(0,array_size(cur_system->asteroids)-1);
      int ast   = RNG(0,cur_system->asteroids[field].nb-1);
      int bad_asteroid = 0;
      if ( (cur_system->asteroids[field].asteroids[ast].appearing == ASTEROID_INVISIBLE) ||
            (cur_system->asteroids[field].asteroids[ast].appearing == ASTEROID_INIT)) {
         /* Switch to next index until we find a valid one, or until we come full-circle. */
         bad_asteroid = 1;
         for (int i=0; i<cur_system->asteroids[field].nb; i++) {
            ast = (ast + 1) % cur_system->asteroids[field].nb;
            if ( (cur_system->asteroids[field].asteroids[ast].appearing != ASTEROID_INVISIBLE) &&
                  (cur_system->asteroids[field].asteroids[ast].appearing != ASTEROID_INIT)) {
               bad_asteroid = 0;
               break;
            }
         }
      }

      if (bad_asteroid) {
         WARN("Failed to get a valid asteroid in field %d.", field);
         return 0;
      }

      lua_pushnumber(L,field);
      lua_pushnumber(L,ast);
      return 2;
   }

   return 0;
}

/**
 * @brief Gets the position and velocity of an asteroid
 *
 * @usage pos = system.asteroidPos( anchor, ast )
 *
 *    @luatparam int anchor Id of the asteroid anchor.
 *    @luatparam int asteroid Id of the asteroid of this anchor.
 *    @luatreturn Vec2 pos position of the asteroid.
 *    @luatreturn Vec2 vel velocity of the asteroid.
 * @luafunc asteroidPos
 */
static int systemL_asteroidPos( lua_State *L )
{
   int field = luaL_checkint(L,1);
   int ast   = luaL_checkint(L,2);

   if ( field >= array_size(cur_system->asteroids) ) {
      WARN(_("field index %d too high"), field);
      return 0;
   }

   if ( ast >= cur_system->asteroids[field].nb ) {
      WARN(_("asteroid index too high"));
      return 0;
   }

   lua_pushvector(L, cur_system->asteroids[field].asteroids[ast].pos);
   lua_pushvector(L, cur_system->asteroids[field].asteroids[ast].vel);
   return 2;
}

/**
 * @brief Sees if a given asteroid has been destroyed recently
 *
 * @usage i = system.asteroidDestroyed( anchor, ast )
 *
 *    @luatparam int anchor Id of the asteroid anchor.
 *    @luatparam int asteroid Id of the asteroid of this anchor.
 *    @luatreturn bool i true if the asteroid was destroyed.
 * @luafunc asteroidDestroyed
 */
static int systemL_asteroidDestroyed( lua_State *L )
{
   int isdestroyed;
   int field = luaL_checkint(L,1);
   int ast   = luaL_checkint(L,2);

   if ( field >= array_size(cur_system->asteroids) ) {
      WARN(_("field index %d too high"), field);
      return 0;
   }

   if ( ast >= cur_system->asteroids[field].nb ) {
      WARN(_("asteroid index too high"));
      return 0;
   }

   /* If the asteroid is re-appearing, it was destroyed recently. */
   isdestroyed = (cur_system->asteroids[field].asteroids[ast].appearing == ASTEROID_GROWING);
   lua_pushboolean(L, isdestroyed);
   return 1;
}

/**
 * @brief Adds a gatherable object
 *
 * @usage i = system.addGatherable( "Gold", 5, vec2.new(0,0), vec2.new(0,0) ) -- creates 5 tons of gold at the origin
 *
 *    @luatparam string commodity name of the commodity.
 *    @luatparam int nb quantity of commodity in the gatherable .
 *    @luatparam Vec2 pos position of the gatherable.
 *    @luatparam Vec2 vel velocity of the gatherable.
 *    @luatparam[opt] number lifelength Lifelength of the gatherable in seconds.
 *    @luatreturn int i Id of the created gatherable object.
 * @luafunc addGatherable
 */
static int systemL_addGatherable( lua_State *L )
{
   int nb;
   Commodity *commodity;
   Vector2d *pos, *vel;
   double lifelength;

   /* Handle parameters. */
   commodity = luaL_validcommodity( L, 1 );
   nb = luaL_checkint(L,2);
   pos = luaL_checkvector(L,3);
   vel = luaL_checkvector(L,4);
   if (lua_gettop(L) > 4)
      lifelength = luaL_checknumber(L,5);
   else
      lifelength = -1.; /* This means random life length. */

   lua_pushnumber( L, gatherable_init( commodity, *pos, *vel, lifelength, nb ) );
   return 1;
}

/**
 * @brief Returns the factions that have presence in a system and their respective presence values.
 *        Faction names are internal -- can be passed to other functions as a faction identifier, but
 *        should not be shown to the user without being translated by _().
 *
 *  @usage if sys:presences()["Empire"] then -- Checks to see if Empire has ships in the system
 *  @usage if sys:presences()[faction.get("Dvaered"):nameRaw()] then -- Checks to see if the Dvaered have ships in the system
 *
 *    @luatparam System s System to get the factional presences of.
 *    @luatreturn {Faction,...} A table with the factions that have presence in the system.
 * @luafunc presences
 */
static int systemL_presences( lua_State *L )
{
   StarSystem *s = luaL_validsystem(L,1);
   /* Return result in table */
   lua_newtable(L);
   for (int i=0; i<array_size(s->presence); i++) {
      /* Only return positive presences. */
      if (s->presence[i].value <= 0)
         continue;

      lua_pushstring( L, faction_name(s->presence[i].faction) ); /* t, k */
      lua_pushnumber(L,s->presence[i].value); /* t, k, v */
      lua_settable(L,-3);  /* t */
      /* allows syntax foo = system.presences(); if foo["bar"] then ... end */
   }
   return 1;
}

/**
 * @brief Gets the planets in a system.
 *
 * @usage for key, planet in ipairs( sys:planets() ) do -- Iterate over planets in system
 * @usage if \#sys:planets() > 0 then -- System has planets
 *
 *    @luatparam System s System to get planets of
 *    @luatreturn {Planet,...} A table with all the planets
 * @luafunc planets
 */
static int systemL_planets( lua_State *L )
{
   StarSystem *s = luaL_validsystem(L,1);
   /* Push all planets. */
   lua_newtable(L);
   for (int i=0; i<array_size(s->planets); i++) {
      lua_pushplanet(L,planet_index( s->planets[i] )); /* value */
      lua_rawseti(L,-2,i+1);
   }
   return 1;
}

/**
 * @brief Gets the presence in the system.
 *
 * Possible parameters are besides a faction:<br/>
 *  - "all": Gets the sum of all the presences.<br />
 *  - "friendly": Gets the sum of all the friendly presences.<br />
 *  - "hostile": Gets the sum of all the hostile presences.<br />
 *  - "neutral": Gets the sum of all the neutral presences.<br />
 *
 * @usage p = sys:presence( f ) -- Gets the presence of a faction f
 * @usage p = sys:presence( "all" ) -- Gets the sum of all the presences
 * @usage if sys:presence("friendly") > sys:presence("hostile") then -- Checks to see if the system is dominantly friendly
 *
 *    @luatparam System s System to get presence level of.
 *    @luatreturn number The presence level in sys (absolute value).
 * @luafunc presence
 */
static int systemL_presence( lua_State *L )
{
   StarSystem *sys;
   int *fct;
   double presence;
   int used;

   /* Get parameters. */
   sys = luaL_validsystem(L, 1);

   /* Allow fall-through. */
   used = 0;
   fct  = NULL;

   /* Get the second parameter. */
   if (lua_isstring(L, 2)) {
      /* A string command has been given. */
      const char *cmd = lua_tostring(L, 2);
      used = 1;

      /* Check the command string and get the appropriate faction group.*/
      if (strcmp(cmd, "all") == 0)
         fct = faction_getGroup(0);
      else if (strcmp(cmd, "friendly") == 0)
         fct = faction_getGroup(1);
      else if (strcmp(cmd, "hostile") == 0)
         fct = faction_getGroup(3);
      else if (strcmp(cmd, "neutral") == 0)
         fct = faction_getGroup(2);
      else /* Invalid command string. */
         used = 0;
   }

   if (!used) {
      /* A faction id was given. */
      int f  = luaL_validfaction(L, 2);
      fct    = array_create(int);
      array_push_back(&fct, f);
   }

   /* Add up the presence values. */
   presence = 0;
   for (int i=0; i<array_size(fct); i++) {
      /* Only count positive presences. */
      double v = system_getPresence( sys, fct[i] );
      if (v > 0)
         presence += v;
   }

   /* Clean up after ourselves. */
   array_free(fct);

   /* Push it back to Lua. */
   lua_pushnumber(L, presence);
   return 1;
}

/**
 * @brief Gets the radius of the system.
 *
 * This is the radius of the circle which all the default jumps will be on.
 *
 * @usage r = s:radius()
 *
 *    @luatparam System s System to get the radius of.
 *    @luatreturn number The radius of the system.
 * @luafunc radius
 */
static int systemL_radius( lua_State *L )
{
   StarSystem *sys = luaL_validsystem(L, 1);
   lua_pushnumber( L, sys->radius );
   return 1;
}

/**
 * @brief Checks to see if a system is known by the player.
 *
 * @usage b = s:known()
 *
 *    @luatparam System s System to check if the player knows.
 *    @luatreturn boolean true if the player knows the system.
 * @luafunc known
 */
static int systemL_isknown( lua_State *L )
{
   StarSystem *sys = luaL_validsystem(L, 1);
   lua_pushboolean(L, sys_isKnown(sys));
   return 1;
}

/**
 * @brief Sets a system's known state.
 *
 * @usage s:setKnown( false ) -- Makes system unknown.
 *    @luatparam System  s System to set known.
 *    @luatparam[opt=false] boolean b Whether or not to set as known.
 *    @luatparam[opt=false] boolean r Whether or not to iterate over the system's assets and jump points.
 * @luafunc setKnown
 */
static int systemL_setknown( lua_State *L )
{
   int b, r;
   StarSystem *sys;

   NLUA_CHECKRW(L);

   r   = 0;
   sys = luaL_validsystem(L, 1);
   b   = lua_toboolean(L, 2);
   if (lua_gettop(L) > 2)
      r   = lua_toboolean(L, 3);

   if (b)
      sys_setFlag( sys, SYSTEM_KNOWN );
   else
      sys_rmFlag( sys, SYSTEM_KNOWN );

   if (r) {
      if (b) {
         for (int i=0; i < array_size(sys->planets); i++)
            planet_setKnown( sys->planets[i] );
         for (int i=0; i < array_size(sys->jumps); i++)
            jp_setFlag( &sys->jumps[i], JP_KNOWN );
     }
     else {
         for (int i=0; i < array_size(sys->planets); i++)
            planet_rmFlag( sys->planets[i], PLANET_KNOWN );
         for (int i=0; i < array_size(sys->jumps); i++)
            jp_rmFlag( &sys->jumps[i], JP_KNOWN );
     }
   }

   /* Update outfits image array. */
   outfits_updateEquipmentOutfits();

   return 0;
}

/**
 * @brief Checks to see if a system is hidden by the player.
 *
 * @usage b = s:hidden()
 *
 *    @luatparam System s System to check if the player knows.
 *    @luatreturn boolean true if the player knows the system.
 * @luafunc hidden
 */
static int systemL_hidden( lua_State *L )
{
   StarSystem *sys = luaL_validsystem(L, 1);
   lua_pushboolean(L, sys_isFlag( sys, SYSTEM_HIDDEN ));
   return 1;
}

/**
 * @brief Sets a system to be hidden to the player.
 *
 * @usage s:setHidden( true )
 *
 *    @luatparam System s System to check if the player knows.
 *    @luatparam boolean hide Whether or not to hide the system.
 * @luafunc hidden
 */
static int systemL_setHidden( lua_State *L )
{
   StarSystem *sys = luaL_validsystem(L, 1);
   int b = lua_toboolean(L,2);
   if (b)
      sys_setFlag( sys, SYSTEM_HIDDEN );
   else
      sys_rmFlag( sys, SYSTEM_HIDDEN );
   return 0;
}

/**
 * @brief Clears the system markers.
 *
 * This can be dangerous and clash with other missions, do not try this at home kids.
 *
 * @usage system.mrkClear()
 *
 * @luafunc mrkClear
 */
static int systemL_mrkClear( lua_State *L )
{
   (void) L;
   NLUA_CHECKRW(L);
   ovr_mrkClear();
   return 0;
}

/**
 * @brief Adds a system marker.
 *
 * @usage mrk_id = system.mrkAdd( vec2.new( 50, 30 ), "Hello" ) -- Creates a marker at (50,30)
 *
 *    @luatparam Vec2 v Position to display marker at.
 *    @luatparam[opt] string str String to display next to marker.
 *    @luatreturn number The id of the marker.
 * @luafunc mrkAdd
 */
static int systemL_mrkAdd( lua_State *L )
{
   const char *str;
   Vector2d *vec;
   unsigned int id;

   NLUA_CHECKRW(L);

   /* Handle parameters. */
   vec = luaL_checkvector( L, 1 );
   str = luaL_optstring( L, 2, NULL );

   /* Create marker. */
   id  = ovr_mrkAddPoint( str, vec->x, vec->y );
   lua_pushnumber( L, id );
   return 1;
}

/**
 * @brief Removes a system marker.
 *
 * @usage system.mrkRm( mrk_id ) -- Removes a marker by mrk_id
 *
 *    @luatparam number id ID of the marker to remove.
 * @luafunc mrkRm
 */
static int systemL_mrkRm( lua_State *L )
{
   NLUA_CHECKRW(L);
   unsigned int id = luaL_checklong( L, 1 );
   ovr_mrkRm( id );
   return 0;
}

/**
 * @brief Gets the system tags.
 *
 * @usage if system.cur():tags["fancy"] then -- Has "fancy" tag
 *
 *    @luatparam System s System to get tags of.
 *    @luatreturn table Table of tags where the name is the key and true is the value.
 * @luafunc tags
 */
static int systemL_tags( lua_State *L )
{
   StarSystem *s = luaL_validsystem(L,1);
   lua_newtable(L);
   for (int i=0; i<array_size(s->tags); i++) {
      lua_pushstring(L,s->tags[i]);
      lua_pushboolean(L,1);
      lua_rawset(L,-3);
   }
   return 1;
}
