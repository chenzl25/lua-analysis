/*
** $Id: lgc.h,v 2.58.1.1 2013/04/12 18:48:47 roberto Exp $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#ifndef lgc_h
#define lgc_h


#include "lobject.h"
#include "lstate.h"

/*
** Collectable objects may have one of three colors: white, which
** means the object is not marked; gray, which means the
** object is marked, but its references may be not marked; and
** black, which means that the object and all its references are marked.
** The main invariant of the garbage collector, while marking objects,
** is that a black object can never point to a white one. Moreover,
** any gray object must be in a "gray list" (gray, grayagain, weak,
** allweak, ephemeron) so that it can be visited again before finishing
** the collection cycle. These lists have no meaning when the invariant
** is not being enforced (e.g., sweep phase).
*/

// 垃圾回收GC
// 增量型的标记扫描算法
// lua使用了3种颜色来表示可回收对象的状态
// 白色: 
//      白色又分为两种，一种是current white，另外一种是other white
//		分别代表没用没有标记到和不参与本轮GC的新增的对象
// 灰色:
// 		灰色代表当前对象标记了，但是它所引用的对象尚未标记
// 黑色:
// 		黑色代表当前对象和引用的对象都标记了
// 
// 这个标记过程中的不变量是白色的对象不会指向黑色对象

/* how much to allocate before next GC step */
#if !defined(GCSTEPSIZE)
/* ~100 small strings */
#define GCSTEPSIZE	(cast_int(100 * sizeof(TString)))
#endif


/*
** Possible states of the Garbage Collector
*/
// GC过程中的可能状态
#define GCSpropagate	0 	// GC在增量标记中
#define GCSatomic	1		// GC在一致性标记中
#define GCSsweepstring	2	// GC在扫描字符串中
#define GCSsweepudata	3	// GC在扫描userdata中
#define GCSsweep	4		// GC在增量扫描中
#define GCSpause	5		// GC结束，待开始

// GC是否在扫描阶段
#define issweepphase(g)  \
	(GCSsweepstring <= (g)->gcstate && (g)->gcstate <= GCSsweep)
// GC是分代式的
#define isgenerational(g)	((g)->gckind == KGC_GEN)

/*
** macros to tell when main invariant (white objects cannot point to black
** ones) must be kept. During a non-generational collection, the sweep
** phase may break the invariant, as objects turned white may point to
** still-black objects. The invariant is restored when sweep ends and
** all objects are white again. During a generational collection, the
** invariant must be kept all times.
*/
// GC不变量是否成立
// 当GC是增量型标记扫描时，只有在标记阶段成立，而扫描阶段会打破这规则
// 而GC是分代式的时候，不变量恒成立
#define keepinvariant(g)	(isgenerational(g) || g->gcstate <= GCSatomic)


/*
** Outside the collector, the state in generational mode is kept in
** 'propagate', so 'keepinvariant' is always true.
*/
// GC不变量是否不成立
#define keepinvariantout(g)  \
  check_exp(g->gcstate == GCSpropagate || !isgenerational(g),  \
            g->gcstate <= GCSatomic)


/*
** some useful bit tricks
*/
#define resetbits(x,m)		((x) &= cast(lu_byte, ~(m)))
#define setbits(x,m)		((x) |= (m))
#define testbits(x,m)		((x) & (m))
#define bitmask(b)		(1<<(b))
#define bit2mask(b1,b2)		(bitmask(b1) | bitmask(b2))
#define l_setbit(x,b)		setbits(x, bitmask(b))
#define resetbit(x,b)		resetbits(x, bitmask(b))
#define testbit(x,b)		testbits(x, bitmask(b))

// GC对象marked的各个位表示
/* Layout for bit use in `marked' field: */
#define WHITE0BIT	0  /* object is white (type 0) */
#define WHITE1BIT	1  /* object is white (type 1) */
#define BLACKBIT	2  /* object is black */
#define FINALIZEDBIT	3  /* object has been separated for finalization */
#define SEPARATED	4  /* object is in 'finobj' list or in 'tobefnz' */
#define FIXEDBIT	5  /* object is fixed (should not be collected) */
#define OLDBIT		6  /* object is old (only in generational mode) */
/* bit 7 is currently used by tests (luaL_checkmemory) */

#define WHITEBITS	bit2mask(WHITE0BIT, WHITE1BIT)

#define iswhite(x)      testbits((x)->gch.marked, WHITEBITS)
#define isblack(x)      testbit((x)->gch.marked, BLACKBIT)
// 白色黑色都没标记就代表灰色
#define isgray(x)  /* neither white nor black */  \
	(!testbits((x)->gch.marked, WHITEBITS | bitmask(BLACKBIT)))

#define isold(x)	testbit((x)->gch.marked, OLDBIT)

/* MOVE OLD rule: whenever an object is moved to the beginning of
   a GC list, its old bit must be cleared */
// 每当对象被重新放回gc链的开头时，old标志位就重置
#define resetoldbit(o)	resetbit((o)->gch.marked, OLDBIT)
// 其它白色并非dead，currentwhite才是
#define otherwhite(g)	(g->currentwhite ^ WHITEBITS)
#define isdeadm(ow,m)	(!(((m) ^ WHITEBITS) & (ow)))
#define isdead(g,v)	isdeadm(otherwhite(g), (v)->gch.marked)

#define changewhite(x)	((x)->gch.marked ^= WHITEBITS)
#define gray2black(x)	l_setbit((x)->gch.marked, BLACKBIT)

#define valiswhite(x)	(iscollectable(x) && iswhite(gcvalue(x)))

#define luaC_white(g)	cast(lu_byte, (g)->currentwhite & WHITEBITS)

#define luaC_condGC(L,c) \
	{if (G(L)->GCdebt > 0) {c;}; condchangemem(L);}
// 在适当的时候驱动GC一步步运行
#define luaC_checkGC(L)		luaC_condGC(L, luaC_step(L);)

// barrier分为前向和后向
#define luaC_barrier(L,p,v) { if (valiswhite(v) && isblack(obj2gco(p)))  \
	luaC_barrier_(L,obj2gco(p),gcvalue(v)); }

#define luaC_barrierback(L,p,v) { if (valiswhite(v) && isblack(obj2gco(p)))  \
	luaC_barrierback_(L,p); }

#define luaC_objbarrier(L,p,o)  \
	{ if (iswhite(obj2gco(o)) && isblack(obj2gco(p))) \
		luaC_barrier_(L,obj2gco(p),obj2gco(o)); }

#define luaC_objbarrierback(L,p,o)  \
   { if (iswhite(obj2gco(o)) && isblack(obj2gco(p))) luaC_barrierback_(L,p); }

#define luaC_barrierproto(L,p,c) \
   { if (isblack(obj2gco(p))) luaC_barrierproto_(L,p,c); }

LUAI_FUNC void luaC_freeallobjects (lua_State *L);
LUAI_FUNC void luaC_step (lua_State *L);
LUAI_FUNC void luaC_forcestep (lua_State *L);
LUAI_FUNC void luaC_runtilstate (lua_State *L, int statesmask);
LUAI_FUNC void luaC_fullgc (lua_State *L, int isemergency);
LUAI_FUNC GCObject *luaC_newobj (lua_State *L, int tt, size_t sz,
                                 GCObject **list, int offset);
LUAI_FUNC void luaC_barrier_ (lua_State *L, GCObject *o, GCObject *v);
LUAI_FUNC void luaC_barrierback_ (lua_State *L, GCObject *o);
LUAI_FUNC void luaC_barrierproto_ (lua_State *L, Proto *p, Closure *c);
LUAI_FUNC void luaC_checkfinalizer (lua_State *L, GCObject *o, Table *mt);
LUAI_FUNC void luaC_checkupvalcolor (global_State *g, UpVal *uv);
LUAI_FUNC void luaC_changemode (lua_State *L, int mode);

#endif
