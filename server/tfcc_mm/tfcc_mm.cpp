/*
 * tfcc_mm - TFC Competitive Edition server companion (metamod-p plugin)
 *
 * The TFCC client only changes behaviour when the server says a matching
 * server-side fix is active, so a TFCC client on a stock server behaves
 * exactly like the stock client. This plugin is the "server says" half.
 *
 * It publishes per-player physinfo keys, the same channel TFC already uses
 * for its own "tfc" key and Half-Life uses for "slj". Physinfo is sent to the
 * client with every clientdata update and read by the shared movement code
 * (pm_shared) on both sides. Stock clients ignore keys they do not know.
 *
 * It does not change any game behaviour itself: it hooks nothing but
 * StartFrame/ClientPutInServer and never supersedes a call.
 *
 *   key   meaning                          set while
 *   tcrf  ramp fix in client prediction    otfc_mf_enable != 0 && otfc_mf_rampfix != 0
 *                                          (OTFC movefix metamod plugin loaded)
 *
 * When a condition is false the key is removed (set to ""), so the physinfo
 * string is byte-identical to stock.
 *
 * Cvars:
 *   tfcc_mm_publish 1   0 = never publish keys (client behaves stock)
 *   tfcc_version        read-only, FCVAR_SERVER (shows in server rules)
 */

#include <extdll.h>
#include <meta_api.h>

#include <string.h>

#define TFCC_MM_VERSION "0.1.0"

enginefuncs_t g_engfuncs;
globalvars_t *gpGlobals;

meta_globals_t *gpMetaGlobals;
gamedll_funcs_t *gpGamedllFuncs;
mutil_funcs_t *gpMetaUtilFuncs;

plugin_info_t Plugin_info = {
	META_INTERFACE_VERSION,
	"TFCC companion",
	TFCC_MM_VERSION,
	__DATE__,
	"TFCC project",
	"https://github.com/ededdneddyfan/tfcc",
	"TFCC",
	PT_ANYTIME,
	PT_ANYPAUSE,
};

static cvar_t cv_publish = { (char *)"tfcc_mm_publish", (char *)"1", 0, 1.0f, NULL };
static cvar_t cv_version = { (char *)"tfcc_version", (char *)TFCC_MM_VERSION, FCVAR_SERVER | FCVAR_SPONLY, 0.0f, NULL };

static cvar_t *cv_publish_ptr;

// ---------------------------------------------------------------------------
// Published keys. Add a row per client-predicted server fix.
// ---------------------------------------------------------------------------

static float CvarValue( const char *name, float fallback )
{
	cvar_t *c = CVAR_GET_POINTER( name );
	return c ? c->value : fallback;
}

static bool Cond_RampFix( void )
{
	// Both cvars are registered by the OTFC movefix plugin. If it is not
	// loaded they do not exist and the fix is off.
	if ( !CVAR_GET_POINTER( "otfc_mf_rampfix" ) )
		return false;
	return CvarValue( "otfc_mf_enable", 1.0f ) != 0.0f && CvarValue( "otfc_mf_rampfix", 0.0f ) != 0.0f;
}

struct tfcc_key_t
{
	const char *key;
	bool ( *cond )( void );
};

static const tfcc_key_t g_keys[] = {
	{ "tcrf", Cond_RampFix },
};

static const int NUM_KEYS = sizeof( g_keys ) / sizeof( g_keys[0] );

static void SyncPlayer( edict_t *ed, const bool *want )
{

	for ( int k = 0; k < NUM_KEYS; k++ )
	{
		const char *cur = g_engfuncs.pfnGetPhysicsKeyValue( ed, g_keys[k].key );
		bool have = cur && cur[0] == '1' && cur[1] == '\0';
		if ( have == want[k] )
			continue;
		g_engfuncs.pfnSetPhysicsKeyValue( ed, g_keys[k].key, want[k] ? "1" : "" );
	}
}

static void SyncAll( void )
{
	bool want[NUM_KEYS];
	bool publish = !cv_publish_ptr || cv_publish_ptr->value != 0.0f;

	for ( int k = 0; k < NUM_KEYS; k++ )
		want[k] = publish && g_keys[k].cond();

	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		edict_t *ed = INDEXENT( i );
		if ( !ed || ed->free || !( ed->v.flags & FL_CLIENT ) )
			continue;
		SyncPlayer( ed, want );
	}
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

static float g_nextSync;

static void StartFrame( void )
{
	// Cvars can change at any time (rcon, AMXX, map configs). Re-check a few
	// times per second; SyncPlayer only writes when a value actually changes.
	if ( gpGlobals->time >= g_nextSync || gpGlobals->time < g_nextSync - 1.0f )
	{
		g_nextSync = gpGlobals->time + 0.25f;
		SyncAll();
	}
	RETURN_META( MRES_IGNORED );
}

static void ClientPutInServer_Post( edict_t *ed )
{
	g_nextSync = 0.0f; // sync on the next frame
	RETURN_META( MRES_IGNORED );
}

static DLL_FUNCTIONS g_dllFuncs;
static DLL_FUNCTIONS g_dllFuncsPost;

C_DLLEXPORT int GetEntityAPI2( DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion )
{
	if ( !pFunctionTable || *interfaceVersion != INTERFACE_VERSION )
	{
		if ( interfaceVersion )
			*interfaceVersion = INTERFACE_VERSION;
		return FALSE;
	}
	memset( &g_dllFuncs, 0, sizeof( g_dllFuncs ) );
	g_dllFuncs.pfnStartFrame = StartFrame;
	memcpy( pFunctionTable, &g_dllFuncs, sizeof( DLL_FUNCTIONS ) );
	return TRUE;
}

C_DLLEXPORT int GetEntityAPI2_Post( DLL_FUNCTIONS *pFunctionTable, int *interfaceVersion )
{
	if ( !pFunctionTable || *interfaceVersion != INTERFACE_VERSION )
	{
		if ( interfaceVersion )
			*interfaceVersion = INTERFACE_VERSION;
		return FALSE;
	}
	memset( &g_dllFuncsPost, 0, sizeof( g_dllFuncsPost ) );
	g_dllFuncsPost.pfnClientPutInServer = ClientPutInServer_Post;
	memcpy( pFunctionTable, &g_dllFuncsPost, sizeof( DLL_FUNCTIONS ) );
	return TRUE;
}

// ---------------------------------------------------------------------------
// Metamod / engine entry points
// ---------------------------------------------------------------------------

static META_FUNCTIONS gMetaFunctionTable = {
	NULL,               // pfnGetEntityAPI
	NULL,               // pfnGetEntityAPI_Post
	GetEntityAPI2,      // pfnGetEntityAPI2
	GetEntityAPI2_Post, // pfnGetEntityAPI2_Post
	NULL,               // pfnGetNewDLLFunctions
	NULL,               // pfnGetNewDLLFunctions_Post
	NULL,               // pfnGetEngineFunctions
	NULL,               // pfnGetEngineFunctions_Post
};

C_DLLEXPORT int Meta_Query( char *ifvers, plugin_info_t **pPlugInfo, mutil_funcs_t *pMetaUtilFuncs )
{
	*pPlugInfo = &Plugin_info;
	gpMetaUtilFuncs = pMetaUtilFuncs;
	return TRUE;
}

C_DLLEXPORT int Meta_Attach( PLUG_LOADTIME now, META_FUNCTIONS *pFunctionTable, meta_globals_t *pMGlobals, gamedll_funcs_t *pGamedllFuncs )
{
	if ( !pMGlobals || !pFunctionTable )
		return FALSE;
	gpMetaGlobals = pMGlobals;
	gpGamedllFuncs = pGamedllFuncs;
	memcpy( pFunctionTable, &gMetaFunctionTable, sizeof( META_FUNCTIONS ) );

	CVAR_REGISTER( &cv_publish );
	CVAR_REGISTER( &cv_version );
	cv_publish_ptr = CVAR_GET_POINTER( "tfcc_mm_publish" );
	g_nextSync = 0.0f;
	return TRUE;
}

C_DLLEXPORT int Meta_Detach( PLUG_LOADTIME now, PL_UNLOAD_REASON reason )
{
	// Leave the physinfo exactly as stock: remove every key we may have set.
	if ( gpGlobals )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; i++ )
		{
			edict_t *ed = INDEXENT( i );
			if ( !ed || ed->free || !( ed->v.flags & FL_CLIENT ) )
				continue;
			for ( int k = 0; k < NUM_KEYS; k++ )
				g_engfuncs.pfnSetPhysicsKeyValue( ed, g_keys[k].key, "" );
		}
	}
	return TRUE;
}

#ifdef _WIN32
#define GIVEFN_EXPORT extern "C" __declspec( dllexport ) void __stdcall
#else
#define GIVEFN_EXPORT extern "C" __attribute__( ( visibility( "default" ) ) ) void
#endif

GIVEFN_EXPORT GiveFnptrsToDll( enginefuncs_t *pengfuncsFromEngine, globalvars_t *pGlobals )
{
	memcpy( &g_engfuncs, pengfuncsFromEngine, sizeof( enginefuncs_t ) );
	gpGlobals = pGlobals;
}
