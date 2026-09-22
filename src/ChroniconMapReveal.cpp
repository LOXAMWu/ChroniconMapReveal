// ChroniconMapReveal —— Chronicon 地图全开 Mod  (这是实际生效并交付的版本)
//
// 加载: Aurie Framework      挂钩: MinHook (随 Mod 一起用 MinGW 编译)
//
// 两条互补的机制:
//   A) 反复调用游戏自带的 "完全开启地图" 脚本 gml_Script_minimapExplore
//      (物品 "芬利的魔法地图" item_699 的效果: ds_grid_clear(mapexplore, 1) + minimapRefresh())
//      触发点: minimapSetArea / minimapUpdate / minimapRefresh / world_gen_step(每 30 帧)
//   B) 运行时把 minimapRefresh 绘制循环中 "未探索就跳过" 的判定改成不跳过
//      (VA 0x141290B82 的 je -> 6 个 NOP), 使所有格子都被绘制。
//      A + B 同时开启才能稳定生效 (只靠 A 时地图仍显示未探索样式)。
//
// 快捷键: F5 = 开关机制 A, F6 = 输出运行状态, F7 = 开关机制 B
//
// 所有目标函数在挂钩/改写前都会校验机器码签名, 版本不符则跳过 (不破坏游戏)。

#include <windows.h>

#include <cstdint>
#include <cstring>

#include <MinHook.h>
#include <Aurie/shared.hpp>

using namespace Aurie;

// YYC 脚本调用约定: RValue& fn(CInstance* Self, CInstance* Other, RValue& Result, int argc, RValue** args)
using GmlScriptFn = void* (*)(void* self, void* other, void* result, int argument_count, void** arguments);

static const uintptr_t RVA_MINIMAP_INI      = 0x128AD00;
static const uintptr_t RVA_MINIMAP_EXPLORE  = 0x128AA30;
static const uintptr_t RVA_MINIMAP_REFRESH  = 0x128FDE0;
static const uintptr_t RVA_MINIMAP_SETAREA  = 0x1297570;
static const uintptr_t RVA_MINIMAP_UPDATE   = 0x1299860;
static const uintptr_t RVA_WORLD_GEN_STEP   = 0x1B78D10;

// minimapRefresh 绘制循环中 "if (mapexplore[#x,y])" 之后的跳转指令
static const uintptr_t RVA_DRAW_GATE        = 0x1290B82;
static const unsigned char DRAW_GATE_ORIGINAL[] = { 0x0F, 0x84, 0xD6, 0x01, 0x00, 0x00 };  // je
static const unsigned char DRAW_GATE_PATCHED[]  = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };  // nop x6

static const unsigned char PROLOGUE_INI[] = {
	0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20, 0x4C, 0x89, 0x40, 0x18, 0x48, 0x89, 0x50, 0x10, 0x48
};
static const unsigned char PROLOGUE_EXPLORE[] = {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55
};
static const unsigned char PROLOGUE_REFRESH[] = {
	0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20, 0x4C, 0x89, 0x40, 0x18, 0x48, 0x89, 0x50, 0x10, 0x48
};
static const unsigned char PROLOGUE_SETAREA[] = {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x44, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x55
};
static const unsigned char PROLOGUE_UPDATE[] = {
	0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x40, 0x18, 0x48, 0x89, 0x50, 0x10, 0x48, 0x89, 0x48, 0x08, 0x55
};
static const unsigned char PROLOGUE_WORLDGEN[] = {
	0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x20, 0x4C, 0x89, 0x40, 0x18, 0x48, 0x89, 0x50, 0x10, 0x48
};

static GmlScriptFn g_FnMinimapExplore = nullptr;
static GmlScriptFn g_OrigMinimapIni = nullptr;
static GmlScriptFn g_OrigMinimapRefresh = nullptr;
static GmlScriptFn g_OrigMinimapSetArea = nullptr;
static GmlScriptFn g_OrigMinimapUpdate = nullptr;
static GmlScriptFn g_OrigWorldGenStep = nullptr;

static volatile LONG g_Ready = 0;
static volatile LONG g_RevealEnabled = 1;   // 机制 A
static volatile LONG g_GatePatched = 0;     // 机制 B
static volatile LONG g_InReveal = 0;

static volatile LONG64 g_IniCalls = 0;
static volatile LONG64 g_RefreshCalls = 0;
static volatile LONG64 g_SetAreaCalls = 0;
static volatile LONG64 g_UpdateCalls = 0;
static volatile LONG64 g_WorldGenCalls = 0;
static volatile LONG64 g_Reveals = 0;

// ------------------------------------------------------------------ helpers

static bool BytesEqual(const unsigned char* address, const unsigned char* expected, size_t size)
{
	for (size_t i = 0; i < size; i++)
	{
		if (address[i] != expected[i])
			return false;
	}
	return true;
}

static void* GameAddress(uintptr_t rva)
{
	return reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr)) + rva;
}

static void LogCall(const char* name, volatile LONG64* counter, void* self, void* other)
{
	LONG64 calls = InterlockedIncrement64(counter);
	if (calls <= 2)
		DbgPrintEx(LOG_SEVERITY_INFO, "[MapReveal] %s call #%lld self=%p other=%p", name, calls, self, other);
	else if (calls % 600 == 0)
		DbgPrintEx(LOG_SEVERITY_INFO, "[MapReveal] %s calls=%lld", name, calls);
}

static void RevealMap(void* self, void* other)
{
	if (!g_FnMinimapExplore || !self)
		return;

	if (InterlockedCompareExchange(&g_InReveal, 1, 0) != 0)
		return;

	unsigned char result[16] = { 0 };
	g_FnMinimapExplore(self, other, result, 0, nullptr);
	InterlockedIncrement64(&g_Reveals);
	InterlockedExchange(&g_InReveal, 0);
}

// 机制 B: 让绘制循环不再跳过 "未探索" 格子
static bool SetDrawGatePatch(bool enable)
{
	unsigned char* gate = reinterpret_cast<unsigned char*>(GameAddress(RVA_DRAW_GATE));
	const unsigned char* expected = enable ? DRAW_GATE_ORIGINAL : DRAW_GATE_PATCHED;
	const unsigned char* wanted = enable ? DRAW_GATE_PATCHED : DRAW_GATE_ORIGINAL;

	if (!BytesEqual(gate, expected, sizeof(DRAW_GATE_ORIGINAL)))
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[MapReveal] draw-gate bytes unexpected at %p, patch skipped", gate);
		return false;
	}

	DWORD old_protection = 0;
	if (!VirtualProtect(gate, sizeof(DRAW_GATE_ORIGINAL), PAGE_EXECUTE_READWRITE, &old_protection))
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[MapReveal] draw-gate VirtualProtect failed (%lu)", GetLastError());
		return false;
	}

	std::memcpy(gate, wanted, sizeof(DRAW_GATE_ORIGINAL));

	DWORD ignored = 0;
	VirtualProtect(gate, sizeof(DRAW_GATE_ORIGINAL), old_protection, &ignored);
	FlushInstructionCache(GetCurrentProcess(), gate, sizeof(DRAW_GATE_ORIGINAL));

	InterlockedExchange(&g_GatePatched, enable ? 1 : 0);
	DbgPrintEx(LOG_SEVERITY_INFO, "[MapReveal] draw-gate patch = %s", enable ? "ON" : "OFF");
	return true;
}

static void HandleHotkeys()
{
	if (GetAsyncKeyState(VK_F5) & 0x1)
	{
		LONG current = InterlockedCompareExchange(&g_RevealEnabled, 1, 1);
		InterlockedExchange(&g_RevealEnabled, current ? 0 : 1);
		DbgPrintEx(LOG_SEVERITY_INFO, "[MapReveal] reveal mechanism = %s", current ? "OFF" : "ON");
	}

	if (GetAsyncKeyState(VK_F6) & 0x1)
		DbgPrintEx(
			LOG_SEVERITY_INFO,
			"[MapReveal] status: ini=%lld refresh=%lld setArea=%lld update=%lld worldGen=%lld reveals=%lld gate=%d auto=%d",
			g_IniCalls, g_RefreshCalls, g_SetAreaCalls, g_UpdateCalls, g_WorldGenCalls, g_Reveals,
			InterlockedCompareExchange(&g_GatePatched, 0, 0),
			InterlockedCompareExchange(&g_RevealEnabled, 0, 0)
		);

	if (GetAsyncKeyState(VK_F7) & 0x1)
		SetDrawGatePatch(InterlockedCompareExchange(&g_GatePatched, 0, 0) == 0);
}

// ------------------------------------------------------------------ detours

static void* MinimapIniDetour(void* self, void* other, void* result, int argument_count, void** arguments)
{
	LogCall("minimapIni", &g_IniCalls, self, other);
	void* value = g_OrigMinimapIni(self, other, result, argument_count, arguments);
	InterlockedExchange(&g_Ready, 1);
	return value;
}

static void* MinimapRefreshDetour(void* self, void* other, void* result, int argument_count, void** arguments)
{
	LogCall("minimapRefresh", &g_RefreshCalls, self, other);

	if (InterlockedCompareExchange(&g_RevealEnabled, 0, 0))
		RevealMap(self, other);

	return g_OrigMinimapRefresh(self, other, result, argument_count, arguments);
}

static void* MinimapSetAreaDetour(void* self, void* other, void* result, int argument_count, void** arguments)
{
	LogCall("minimapSetArea", &g_SetAreaCalls, self, other);
	void* value = g_OrigMinimapSetArea(self, other, result, argument_count, arguments);
	InterlockedExchange(&g_Ready, 1);

	if (InterlockedCompareExchange(&g_RevealEnabled, 0, 0))
		RevealMap(self, other);

	return value;
}

static void* MinimapUpdateDetour(void* self, void* other, void* result, int argument_count, void** arguments)
{
	LogCall("minimapUpdate", &g_UpdateCalls, self, other);

	if (InterlockedCompareExchange(&g_RevealEnabled, 0, 0))
		RevealMap(self, other);

	return g_OrigMinimapUpdate(self, other, result, argument_count, arguments);
}

static void* WorldGenStepDetour(void* self, void* other, void* result, int argument_count, void** arguments)
{
	LONG64 calls = InterlockedIncrement64(&g_WorldGenCalls);

	HandleHotkeys();

	if ((calls % 30) == 0 && InterlockedCompareExchange(&g_RevealEnabled, 0, 0))
		RevealMap(self, other);

	if (calls % 600 == 0)
		DbgPrintEx(
			LOG_SEVERITY_INFO,
			"[MapReveal] tick: worldGen=%lld reveals=%lld update=%lld refresh=%lld",
			calls, g_Reveals, g_UpdateCalls, g_RefreshCalls
		);

	return g_OrigWorldGenStep(self, other, result, argument_count, arguments);
}

// ------------------------------------------------------------------ install

static bool InstallHook(
	const char* name,
	uintptr_t rva,
	const unsigned char* prologue,
	size_t prologue_size,
	void* detour,
	void** original
)
{
	void* target = GameAddress(rva);

	if (!BytesEqual(reinterpret_cast<const unsigned char*>(target), prologue, prologue_size))
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[MapReveal] %s prologue mismatch, hook skipped", name);
		return false;
	}

	MH_STATUS status = MH_CreateHook(target, detour, original);
	if (status != MH_OK)
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[MapReveal] MH_CreateHook(%s) failed: %d", name, (int)status);
		return false;
	}

	status = MH_EnableHook(target);
	if (status != MH_OK)
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[MapReveal] MH_EnableHook(%s) failed: %d", name, (int)status);
		return false;
	}

	DbgPrintEx(LOG_SEVERITY_INFO, "[MapReveal] %s hooked at %p", name, target);
	return true;
}

EXPORTED AurieStatus ModuleInitialize(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	(void)Module;
	(void)ModulePath;

	DbgPrintEx(LOG_SEVERITY_INFO, "[MapReveal] init (multi-trigger + draw-gate patch)");

	unsigned char* explore = reinterpret_cast<unsigned char*>(GameAddress(RVA_MINIMAP_EXPLORE));
	if (!BytesEqual(explore, PROLOGUE_EXPLORE, sizeof(PROLOGUE_EXPLORE)))
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[MapReveal] minimapExplore signature mismatch, aborting");
		return AURIE_VERIFICATION_FAILURE;
	}
	g_FnMinimapExplore = reinterpret_cast<GmlScriptFn>(explore);

	MH_STATUS status = MH_Initialize();
	if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[MapReveal] MH_Initialize failed: %d", (int)status);
		return AURIE_MODULE_INITIALIZATION_FAILED;
	}

	InstallHook("minimapIni", RVA_MINIMAP_INI, PROLOGUE_INI, sizeof(PROLOGUE_INI),
		reinterpret_cast<void*>(MinimapIniDetour), reinterpret_cast<void**>(&g_OrigMinimapIni));

	InstallHook("minimapRefresh", RVA_MINIMAP_REFRESH, PROLOGUE_REFRESH, sizeof(PROLOGUE_REFRESH),
		reinterpret_cast<void*>(MinimapRefreshDetour), reinterpret_cast<void**>(&g_OrigMinimapRefresh));

	InstallHook("minimapSetArea", RVA_MINIMAP_SETAREA, PROLOGUE_SETAREA, sizeof(PROLOGUE_SETAREA),
		reinterpret_cast<void*>(MinimapSetAreaDetour), reinterpret_cast<void**>(&g_OrigMinimapSetArea));

	InstallHook("minimapUpdate", RVA_MINIMAP_UPDATE, PROLOGUE_UPDATE, sizeof(PROLOGUE_UPDATE),
		reinterpret_cast<void*>(MinimapUpdateDetour), reinterpret_cast<void**>(&g_OrigMinimapUpdate));

	InstallHook("world_gen_step", RVA_WORLD_GEN_STEP, PROLOGUE_WORLDGEN, sizeof(PROLOGUE_WORLDGEN),
		reinterpret_cast<void*>(WorldGenStepDetour), reinterpret_cast<void**>(&g_OrigWorldGenStep));

	// 机制 B: 默认开启 (与机制 A 配合才能稳定生效)
	SetDrawGatePatch(true);

	DbgPrintEx(LOG_SEVERITY_INFO, "[MapReveal] loaded. F5=reveal F6=status F7=draw-gate");
	return AURIE_SUCCESS;
}
