# Lunar C++ wrapper template

Lunar allows to wrap C++ classes for use in Lua by automating large parts of the code creation through templates.

## Special methods

Lunar handles con- and destruction through several special methods (of which only `T(L)` and `~T()` are required).

<table>
  <thead>
    <tr><td><strong>Function</strong></td><td><strong>When</strong></td><td><strong>Available data</strong></td></tr>
  </thead>
  <tbody>
    <tr><td>`T(L)`</td><td>upon construction</td><td>Lua state (C++ object)</td></tr>
    <tr><td>`oninit(L)`</td><td>upon calling `new()` in Lua, after calling the constructor</td><td>Lua state (Lua and C++ object)</td></tr>
     <tr><td>`onpush(L)`</td><td>on pushing objects into Lua from C++ and when calling `new()` in Lua<L/td><td>Lua state (Lua and C++ object)</td></tr>
    <tr><td>`ongc(L)`</td><td>upon garbage collection, before calling the destructor</td><td>Lua state (Lua and C++ object)</td></tr>
    <tr><td>`~T()`</td><td>code that needs just the C object</td><td>just C++ object</td></tr>
  </tbody>
</table>
