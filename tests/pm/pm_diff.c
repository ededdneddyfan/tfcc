/*
 * pm_diff - differential test for the shared player movement code.
 *
 * Loads two builds of pm_shared side by side:
 *   A = baseline (upstream tf15-client, unmodified)
 *   B = TFCC
 * and drives both with identical inputs through a synthetic world.
 *
 * Gate 1 (compat):  with no TFCC physinfo keys, B must be BIT-IDENTICAL to A
 *                   for every frame of every fuzzed scenario. This is the
 *                   "a TFCC client on a stock server predicts exactly like the
 *                   stock code" guarantee.
 * Gate 2 (fix):     PM_FlyMove with a duplicate clip plane (the ramp bug).
 *                   A must reproduce the bug at least once (proves the test
 *                   sees it), B with tcrf=1 must never zero the speed, and B
 *                   without the key must match A exactly.
 *
 * Usage: pm_diff <libA.so> <libB.so> [scenarios] [frames]
 */
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mathlib.h"
#include "const.h"
#include "usercmd.h"
#include "pm_defs.h"
#include "pm_movevars.h"

struct hull_s;
#ifndef VectorSet
#define VectorSet( v, a, b, c ) ( ( v )[0] = ( a ), ( v )[1] = ( b ), ( v )[2] = ( c ) )
#endif
static float vlen( const float *v ) { return sqrtf( v[0] * v[0] + v[1] * v[1] + v[2] * v[2] ); }
#define VectorLength vlen

typedef void ( *PM_Init_t )( playermove_t * );
typedef void ( *PM_Move_t )( playermove_t *, int );
typedef int ( *PM_FlyMove_t )( void );

typedef struct
{
	void *h;
	PM_Init_t Init;
	PM_Move_t Move;
	PM_FlyMove_t FlyMove;
	playermove_t **ppmove;
	const char *name;
} pmlib_t;

static int load_lib( pmlib_t *l, const char *path )
{
	l->name = path;
	l->h = dlopen( path, RTLD_NOW | RTLD_LOCAL );
	if ( !l->h )
	{
		fprintf( stderr, "dlopen %s: %s\n", path, dlerror() );
		return 0;
	}
	l->Init = (PM_Init_t)dlsym( l->h, "PM_Init" );
	l->Move = (PM_Move_t)dlsym( l->h, "PM_Move" );
	l->FlyMove = (PM_FlyMove_t)dlsym( l->h, "PM_FlyMove" );
	l->ppmove = (playermove_t **)dlsym( l->h, "pmove" );
	if ( !l->Init || !l->Move || !l->FlyMove || !l->ppmove )
	{
		fprintf( stderr, "%s: missing symbols\n", path );
		return 0;
	}
	return 1;
}

/* ---------------- deterministic RNG (shared by harness and engine stubs) */

static unsigned int g_rng;
static unsigned int rng_next( void )
{
	g_rng = g_rng * 1103515245u + 12345u;
	return g_rng >> 1;
}
static float frand( float lo, float hi ) { return lo + ( hi - lo ) * ( rng_next() % 1000001 ) / 1000000.0f; }

static unsigned int g_engrng;
static int eng_RandomLong( int lo, int hi )
{
	g_engrng = g_engrng * 214013u + 2531011u;
	if ( hi <= lo )
		return lo;
	return lo + (int)( ( g_engrng >> 8 ) % (unsigned)( hi - lo + 1 ) );
}
static float eng_RandomFloat( float lo, float hi )
{
	g_engrng = g_engrng * 214013u + 2531011u;
	return lo + ( hi - lo ) * ( ( g_engrng >> 8 ) & 0xffff ) / 65535.0f;
}

/* ---------------- synthetic world: solid half-spaces */

#define MAX_WPLANES 8
typedef struct
{
	vec3_t n;
	float d;
} wplane_t;

static wplane_t g_world[MAX_WPLANES];
static int g_nworld;
static playermove_t *g_cur; /* pmove of the lib currently running */

/* scripted traces for gate 2 */
static int g_script_on, g_script_pos, g_script_len;
static pmtrace_t g_script[8];

static float plane_offset( const wplane_t *p, const float *mins, const float *maxs )
{
	float o = 0;
	for ( int i = 0; i < 3; i++ )
		o += p->n[i] * ( p->n[i] > 0 ? mins[i] : maxs[i] );
	return o;
}

static pmtrace_t world_trace( float *start, float *end, const float *mins, const float *maxs )
{
	pmtrace_t tr;
	memset( &tr, 0, sizeof( tr ) );
	tr.fraction = 1.0f;
	tr.ent = -1;
	for ( int k = 0; k < g_nworld; k++ )
	{
		const wplane_t *p = &g_world[k];
		float off = plane_offset( p, mins, maxs );
		float s0 = DotProduct( p->n, start ) + off - p->d;
		float s1 = DotProduct( p->n, end ) + off - p->d;
		if ( s0 < 0 )
		{
			tr.startsolid = 1;
			if ( s1 < 0 )
				tr.allsolid = 1;
			tr.fraction = 0;
			VectorCopy( p->n, tr.plane.normal );
			tr.plane.dist = p->d;
			tr.ent = 0;
			continue;
		}
		if ( s1 >= 0 )
			continue;
		float f = ( s0 - 0.03125f ) / ( s0 - s1 );
		if ( f < 0 )
			f = 0;
		if ( f < tr.fraction )
		{
			tr.fraction = f;
			VectorCopy( p->n, tr.plane.normal );
			tr.plane.dist = p->d;
			tr.ent = 0;
		}
	}
	for ( int i = 0; i < 3; i++ )
		tr.endpos[i] = start[i] + tr.fraction * ( end[i] - start[i] );
	return tr;
}

static pmtrace_t eng_PlayerTrace( float *start, float *end, int flags, int ignore )
{
	if ( g_script_on )
	{
		pmtrace_t tr = g_script[g_script_pos < g_script_len ? g_script_pos : g_script_len - 1];
		g_script_pos++;
		for ( int i = 0; i < 3; i++ )
			tr.endpos[i] = start[i] + tr.fraction * ( end[i] - start[i] );
		return tr;
	}
	int h = g_cur->usehull;
	return world_trace( start, end, g_cur->player_mins[h], g_cur->player_maxs[h] );
}

static pmtrace_t eng_PlayerTraceEx( float *start, float *end, int flags, int ( *ign )( physent_t * ) )
{
	return eng_PlayerTrace( start, end, flags, -1 );
}

static pmtrace_t g_tlres;
static pmtrace_t *eng_TraceLine( float *start, float *end, int flags, int usehull, int ignore )
{
	g_tlres = world_trace( start, end, g_cur->player_mins[usehull], g_cur->player_maxs[usehull] );
	return &g_tlres;
}
static pmtrace_t *eng_TraceLineEx( float *start, float *end, int flags, int usehull, int ( *ign )( physent_t * ) )
{
	return eng_TraceLine( start, end, flags, usehull, -1 );
}

static int eng_TestPlayerPosition( float *pos, pmtrace_t *ptrace )
{
	pmtrace_t tr = world_trace( pos, pos, g_cur->player_mins[g_cur->usehull], g_cur->player_maxs[g_cur->usehull] );
	if ( ptrace )
		*ptrace = tr;
	return tr.startsolid ? 0 : -1;
}
static int eng_TestPlayerPositionEx( float *pos, pmtrace_t *ptrace, int ( *ign )( physent_t * ) )
{
	return eng_TestPlayerPosition( pos, ptrace );
}

static int eng_PointContents( float *p, int *truecontents )
{
	static const float z[3] = { 0, 0, 0 };
	pmtrace_t tr = world_trace( p, p, z, z );
	int c = tr.startsolid ? CONTENTS_SOLID : CONTENTS_EMPTY;
	if ( truecontents )
		*truecontents = c;
	return c;
}
static int eng_TruePointContents( float *p ) { return eng_PointContents( p, NULL ); }
static int eng_HullPointContents( struct hull_s *h, int n, float *p ) { return CONTENTS_EMPTY; }

/* Quake-style "\key\value" lookup */
static const char *eng_Info_ValueForKey( const char *s, const char *key )
{
	static char value[4][256];
	static int vi;
	char pkey[256];
	vi = ( vi + 1 ) & 3;
	if ( *s == '\\' )
		s++;
	while ( 1 )
	{
		char *o = pkey;
		while ( *s != '\\' )
		{
			if ( !*s )
				return "";
			*o++ = *s++;
		}
		*o = 0;
		s++;
		o = value[vi];
		while ( *s != '\\' && *s )
			*o++ = *s++;
		*o = 0;
		if ( !strcmp( key, pkey ) )
			return value[vi];
		if ( !*s )
			return "";
		s++;
	}
}

static void eng_noop_printf( char *fmt, ... ) {}
static void eng_noop_nprintf( int idx, char *fmt, ... ) {}
static double eng_FloatTime( void ) { return 0.0; }
static void eng_StuckTouch( int hitent, pmtrace_t *tr ) {}
static void eng_Particle( float *o, int c, float l, int zp, int zv ) {}
static int eng_GetModelType( struct model_s *m ) { return 0; /* mod_brush */ }
static void eng_GetModelBounds( struct model_s *m, float *mins, float *maxs )
{
	VectorClear( mins );
	VectorClear( maxs );
}
static void *eng_HullForBsp( physent_t *pe, float *off ) { return NULL; }
static float eng_TraceModel( physent_t *pe, float *s, float *e, trace_t *tr ) { return 1.0f; }
static int eng_FileSize( char *f ) { return -1; }
static byte *eng_LoadFile( char *p, int h, int *len )
{
	if ( len )
		*len = 0;
	return NULL;
}
static void eng_FreeFile( void *b ) {}
static char *eng_memfgets( byte *m, int sz, int *pos, char *buf, int bsz ) { return NULL; }
static void eng_PlaySound( int ch, const char *s, float v, float a, int f, int p ) {}
static const char *eng_TraceTexture( int g, float *s, float *e ) { return NULL; }
static void eng_PlaybackEvent( int flags, int ci, unsigned short ev, float d, float *o, float *a, float f1, float f2, int i1, int i2, int b1, int b2 ) {}
static struct msurface_s *eng_TraceSurface( int g, float *s, float *e ) { return NULL; }

static struct movevars_s g_mv;
static struct model_s *g_worldmodel;

static void setup_pmove( playermove_t *pm )
{
	memset( pm, 0, sizeof( *pm ) );
	pm->movevars = &g_mv;
	pm->multiplayer = 1;
	pm->runfuncs = 1;
	pm->numphysent = 1;
	pm->physents[0].model = g_worldmodel;
	pm->physents[0].solid = SOLID_BSP;
	VectorSet( pm->player_mins[0], -16, -16, -36 );
	VectorSet( pm->player_maxs[0], 16, 16, 36 );
	VectorSet( pm->player_mins[1], -16, -16, -18 );
	VectorSet( pm->player_maxs[1], 16, 16, 18 );
	VectorSet( pm->player_mins[2], 0, 0, 0 );
	VectorSet( pm->player_maxs[2], 0, 0, 0 );
	VectorSet( pm->player_mins[3], -32, -32, -32 );
	VectorSet( pm->player_maxs[3], 32, 32, 32 );
	pm->PM_Info_ValueForKey = eng_Info_ValueForKey;
	pm->PM_Particle = eng_Particle;
	pm->PM_TestPlayerPosition = eng_TestPlayerPosition;
	pm->Con_NPrintf = eng_noop_nprintf;
	pm->Con_DPrintf = eng_noop_printf;
	pm->Con_Printf = eng_noop_printf;
	pm->Sys_FloatTime = eng_FloatTime;
	pm->PM_StuckTouch = eng_StuckTouch;
	pm->PM_PointContents = eng_PointContents;
	pm->PM_TruePointContents = eng_TruePointContents;
	pm->PM_HullPointContents = eng_HullPointContents;
	pm->PM_PlayerTrace = eng_PlayerTrace;
	pm->PM_TraceLine = eng_TraceLine;
	pm->RandomLong = eng_RandomLong;
	pm->RandomFloat = eng_RandomFloat;
	pm->PM_GetModelType = eng_GetModelType;
	pm->PM_GetModelBounds = eng_GetModelBounds;
	pm->PM_HullForBsp = eng_HullForBsp;
	pm->PM_TraceModel = eng_TraceModel;
	pm->COM_FileSize = eng_FileSize;
	pm->COM_LoadFile = eng_LoadFile;
	pm->COM_FreeFile = eng_FreeFile;
	pm->memfgets = eng_memfgets;
	pm->PM_PlaySound = eng_PlaySound;
	pm->PM_TraceTexture = eng_TraceTexture;
	pm->PM_PlaybackEventFull = eng_PlaybackEvent;
	pm->PM_PlayerTraceEx = eng_PlayerTraceEx;
	pm->PM_TestPlayerPositionEx = eng_TestPlayerPositionEx;
	pm->PM_TraceLineEx = eng_TraceLineEx;
	pm->PM_TraceSurface = eng_TraceSurface;
}

static void random_world( void )
{
	g_nworld = 0;
	/* floor */
	VectorSet( g_world[g_nworld].n, 0, 0, 1 );
	g_world[g_nworld++].d = 0;
	int extra = 1 + rng_next() % 5;
	for ( int i = 0; i < extra && g_nworld < MAX_WPLANES; i++ )
	{
		float yaw = frand( 0, 2 * (float)M_PI );
		/* mix of walkable slopes, surf ramps (z<0.7), walls and ceilings */
		float nz;
		switch ( rng_next() % 4 )
		{
		case 0: nz = frand( 0.71f, 0.99f ); break;
		case 1: nz = frand( 0.2f, 0.69f ); break;
		case 2: nz = 0.0f; break;
		default: nz = frand( -1.0f, -0.3f ); break;
		}
		float h = sqrtf( 1 - nz * nz );
		wplane_t *p = &g_world[g_nworld++];
		VectorSet( p->n, cosf( yaw ) * h, sinf( yaw ) * h, nz );
		/* place the plane so the spawn point (0,0,40) is outside it */
		float dist = frand( 40, 400 );
		vec3_t spawn = { 0, 0, 40 };
		p->d = DotProduct( p->n, spawn ) - dist;
	}
}

static const char *g_physinfo_variants_compat[] = {
	"",
	"\\tfc\\1",
	"\\tfc\\1\\tcrf\\0",
	"\\slj\\1\\tfc\\1",
};

/* run identical scenario on both libs, return number of diverging frames */
static int run_scenario( pmlib_t *A, pmlib_t *B, unsigned seed, int frames, const char *physinfo, int verbose )
{
	static playermove_t pa, pb;
	pmlib_t *libs[2] = { A, B };
	playermove_t *pms[2] = { &pa, &pb };
	int diverged = 0;

	g_rng = seed;
	random_world();
	unsigned cmdseed = rng_next();

	for ( int w = 0; w < 2; w++ )
	{
		setup_pmove( pms[w] );
		strncpy( pms[w]->physinfo, physinfo, MAX_PHYSINFO_STRING - 1 );
		g_cur = pms[w];
		VectorSet( pms[w]->origin, 0, 0, 40 );
	}

	g_rng = cmdseed;
	vec3_t v0 = { frand( -600, 600 ), frand( -600, 600 ), frand( -300, 500 ) };
	g_mv.gravity = 800;
	g_mv.stopspeed = 100;
	g_mv.maxspeed = 500;
	g_mv.spectatormaxspeed = 500;
	g_mv.accelerate = 10;
	g_mv.airaccelerate = 10;
	g_mv.wateraccelerate = 10;
	g_mv.friction = 4;
	g_mv.edgefriction = 2;
	g_mv.waterfriction = 1;
	g_mv.entgravity = 1;
	g_mv.bounce = 1;
	g_mv.stepsize = 18;
	g_mv.maxvelocity = 2000;
	g_mv.footsteps = 0;

	for ( int w = 0; w < 2; w++ )
	{
		VectorCopy( v0, pms[w]->velocity );
		pms[w]->movetype = MOVETYPE_WALK;
		pms[w]->gravity = 1;
		pms[w]->friction = 1;
		pms[w]->maxspeed = 400;
		pms[w]->clientmaxspeed = 400;
		pms[w]->onground = -1;
		pms[w]->player_index = 0;
		pms[w]->flags = 0;
	}

	for ( int f = 0; f < frames; f++ )
	{
		usercmd_t cmd;
		memset( &cmd, 0, sizeof( cmd ) );
		cmd.msec = 1 + rng_next() % 50;
		cmd.forwardmove = frand( -400, 400 );
		cmd.sidemove = frand( -400, 400 );
		cmd.upmove = ( rng_next() % 8 == 0 ) ? frand( -400, 400 ) : 0;
		cmd.viewangles[0] = frand( -89, 89 );
		cmd.viewangles[1] = frand( -180, 180 );
		unsigned b = rng_next();
		if ( b % 4 == 0 )
			cmd.buttons |= IN_JUMP;
		if ( b % 11 == 0 )
			cmd.buttons |= IN_DUCK;
		cmd.buttons |= ( b & 0x30 ) ? IN_FORWARD : 0;
		unsigned engseed = rng_next();

		for ( int w = 0; w < 2; w++ )
		{
			playermove_t *pm = pms[w];
			pm->cmd = cmd;
			pm->frametime = cmd.msec * 0.001f;
			pm->time += cmd.msec;
			VectorCopy( cmd.viewangles, pm->angles );
			pm->numtouch = 0;
			g_engrng = engseed;
			g_cur = pm;
			libs[w]->Move( pm, 0 );
			pm->oldbuttons = cmd.buttons;
		}

		if ( memcmp( pa.origin, pb.origin, sizeof( vec3_t ) ) || memcmp( pa.velocity, pb.velocity, sizeof( vec3_t ) ) || pa.onground != pb.onground || pa.flags != pb.flags || pa.usehull != pb.usehull || memcmp( pa.view_ofs, pb.view_ofs, sizeof( vec3_t ) ) )
		{
			if ( verbose && diverged == 0 )
				fprintf( stderr, "  seed %u frame %d: A org(%.6f %.6f %.6f) vel(%.6f %.6f %.6f) | B org(%.6f %.6f %.6f) vel(%.6f %.6f %.6f)\n", seed, f, pa.origin[0], pa.origin[1], pa.origin[2], pa.velocity[0], pa.velocity[1], pa.velocity[2], pb.origin[0], pb.origin[1], pb.origin[2], pb.velocity[0], pb.velocity[1], pb.velocity[2] );
			diverged++;
			/* resync so later frames are still compared meaningfully */
			pb = pa;
			pb.movevars = &g_mv;
		}
	}
	return diverged;
}

/* Gate 2: PM_FlyMove with a duplicated clip plane */
static int flymove_case( pmlib_t *L, unsigned seed, const char *physinfo, vec3_t outvel )
{
	static playermove_t pm;
	setup_pmove( &pm );
	strncpy( pm.physinfo, physinfo, MAX_PHYSINFO_STRING - 1 );
	g_cur = &pm;
	*L->ppmove = &pm;

	g_rng = seed;
	pm.movetype = MOVETYPE_WALK;
	pm.onground = 0; /* on ground, friction 1 -> the non-reflect branch */
	pm.friction = 1;
	pm.frametime = 0.01f;
	g_mv.bounce = 1;

	float yaw = frand( 0, 2 * (float)M_PI );
	float nz = frand( 0.05f, 0.95f );
	float h = sqrtf( 1 - nz * nz );
	vec3_t n = { -cosf( yaw ) * h, -sinf( yaw ) * h, nz };
	float speed = frand( 100, 1500 );
	float vyaw = yaw + frand( -1.2f, 1.2f );
	VectorSet( pm.velocity, cosf( vyaw ) * speed, sinf( vyaw ) * speed, frand( -50, 50 ) );
	VectorSet( pm.origin, frand( -1000, 1000 ), frand( -1000, 1000 ), frand( -1000, 1000 ) );

	memset( g_script, 0, sizeof( g_script ) );
	/* bump 1: hit the ramp part-way; bump 2: same ramp again at fraction 0;
	   bump 3: free */
	g_script[0].fraction = frand( 0.05f, 0.95f );
	VectorCopy( n, g_script[0].plane.normal );
	g_script[0].ent = 0;
	g_script[1].fraction = 0.0f;
	VectorCopy( n, g_script[1].plane.normal );
	g_script[1].ent = 0;
	g_script[2].fraction = 1.0f;
	g_script[2].ent = -1;
	g_script_len = 3;
	g_script_pos = 0;
	g_script_on = 1;

	int blocked = L->FlyMove();
	g_script_on = 0;
	VectorCopy( pm.velocity, outvel );
	return blocked;
}

int main( int argc, char **argv )
{
	pmlib_t A, B;
	int scenarios = argc > 3 ? atoi( argv[3] ) : 2000;
	int frames = argc > 4 ? atoi( argv[4] ) : 300;
	int fail = 0;

	if ( argc < 3 )
	{
		fprintf( stderr, "usage: %s libA.so libB.so [scenarios] [frames]\n", argv[0] );
		return 2;
	}
	g_worldmodel = calloc( 1, 4096 );
	if ( !load_lib( &A, argv[1] ) || !load_lib( &B, argv[2] ) )
		return 2;
	{
		/* PM_Init may run once per library (builds the stuck table) */
		static playermove_t pia, pib;
		setup_pmove( &pia );
		setup_pmove( &pib );
		g_cur = &pia;
		A.Init( &pia );
		g_cur = &pib;
		B.Init( &pib );
	}

	/* ---- gate 1 */
	int nvar = sizeof( g_physinfo_variants_compat ) / sizeof( g_physinfo_variants_compat[0] );
	for ( int v = 0; v < nvar; v++ )
	{
		long total = 0, bad = 0;
		for ( int s = 0; s < scenarios; s++ )
		{
			int d = run_scenario( &A, &B, 1000u + s, frames, g_physinfo_variants_compat[v], 1 );
			total += frames;
			if ( d )
				bad += d;
		}
		printf( "gate1 physinfo=\"%s\": %ld frames, %ld diverged\n", g_physinfo_variants_compat[v], total, bad );
		if ( bad )
			fail = 1;
	}

	/* informational: how often the ramp fix changes movement in the fuzz world */
	{
		long bad = 0;
		for ( int s = 0; s < scenarios; s++ )
			bad += run_scenario( &A, &B, 1000u + s, frames, "\\tfc\\1\\tcrf\\1", 0 );
		printf( "info  physinfo=\"\\tfc\\1\\tcrf\\1\": %ld frames differ from stock (expected >= 0)\n", bad );
	}

	/* ---- gate 2 */
	int cases = scenarios * 10;
	int a_zero = 0, b_zero_fix = 0, b_mismatch_nofix = 0;
	for ( int c = 0; c < cases; c++ )
	{
		vec3_t va, vb, vb2;
		unsigned seed = 77777u + c;
		int ra = flymove_case( &A, seed, "\\tfc\\1", va );
		int rb = flymove_case( &B, seed, "\\tfc\\1", vb );
		flymove_case( &B, seed, "\\tfc\\1\\tcrf\\1", vb2 );
		if ( ra != rb || memcmp( va, vb, sizeof( vec3_t ) ) )
			b_mismatch_nofix++;
		if ( VectorLength( va ) == 0.0f )
			a_zero++;
		if ( VectorLength( vb2 ) == 0.0f )
			b_zero_fix++;
	}
	printf( "gate2 duplicate-plane FlyMove, %d cases: stock zeroed speed %d times; "
	        "TFCC(tcrf=1) zeroed %d times; TFCC(no key) mismatched stock %d times\n",
	    cases, a_zero, b_zero_fix, b_mismatch_nofix );
	if ( a_zero == 0 )
	{
		printf( "gate2 FAIL: harness did not reproduce the ramp bug on stock code\n" );
		fail = 1;
	}
	if ( b_zero_fix != 0 || b_mismatch_nofix != 0 )
		fail = 1;

	printf( "%s\n", fail ? "RESULT: FAIL" : "RESULT: PASS" );
	return fail;
}
