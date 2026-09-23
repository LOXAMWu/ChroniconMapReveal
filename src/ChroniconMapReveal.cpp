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
// 工作方式 (默认):
//   B (绘制判定补丁) 常开, 保证所有格子都会被绘制;
//   A (反复调用 minimapExplore) 只在进入区域后脉冲一次 —— 进区域 1 秒后开启, 0.5 秒后关闭,
//   把 "已探索" 网格刷成全开就够了, 不需要一直调用。
//
// 快捷键: 小键盘 1 = 手动脉冲一次机制 A, 2 = 开关机制 B, 3 = 输出状态,
//         0 = 显示/隐藏状态面板, 9 = 改键模式 (每按一次改下一个动作)
// 键位保存在 <Mod 同目录>\ChroniconMapReveal.ini, 在游戏里改键会写回该文件,
// 手动编辑该文件后游戏内最多 1 秒自动生效 (见 src/hotkeys.cpp)。
// 状态面板: 屏幕右上角实时显示两个机制的开启状态 (见 src/overlay.cpp)
//
// 所有目标函数在挂钩/改写前都会校验机器码签名, 版本不符则跳过 (不破坏游戏)。

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <MinHook.h>
#include <Aurie/shared.hpp>

#include "hotkeys.hpp"
#include "overlay.hpp"

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
static volatile LONG g_RevealEnabled = 0;   // 机制 A (由脉冲逻辑驱动, 不是长开)
static volatile LONG g_GatePatched = 0;     // 机制 B
static volatile LONG g_InReveal = 0;
static volatile LONG64 g_ZoneEnterTick = 0;    // 进入区域的时间 (0 = 还没进过区域)
static volatile LONG64 g_ManualPulseUntil = 0; // 手动脉冲的结束时间

static volatile LONG64 g_IniCalls = 0;
static volatile LONG64 g_RefreshCalls = 0;
static volatile LONG64 g_SetAreaCalls = 0;
static volatile LONG64 g_UpdateCalls = 0;
static volatile LONG64 g_WorldGenCalls = 0;
static volatile LONG64 g_Reveals = 0;

// ------------------------------------------------------------------ hotkeys

static void OverlayLog(const char* message)
{
	DbgPrintEx(LOG_SEVERITY_INFO, "[MapReveal] %s", message);
}

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

// ------------------------------------------------- 面板文字 / 提示 / 改键状态

static int g_RebindAction = 0;			// 下一次改键改的是哪个动作
static ULONGLONG g_ToastUntil = 0;		// 提示文字什么时候过期 (0 = 没有提示)
static wchar_t g_ToastText[160] = L"";
static unsigned g_PanelVersion = 0xFFFFFFFF;
static bool g_PanelCapturing = false;

static void RefreshPanelText(bool force)
{
	const unsigned version = mr_hotkeys::BindingVersion();
	const bool capturing = mr_hotkeys::IsCapturing();
	if (!force && version == g_PanelVersion && capturing == g_PanelCapturing)
		return;

	g_PanelVersion = version;
	g_PanelCapturing = capturing;

	mr_overlay::SetRowKey(0, mr_hotkeys::BindingText(mr_hotkeys::ACTION_PULSE).c_str());
	mr_overlay::SetRowKey(1, mr_hotkeys::BindingText(mr_hotkeys::ACTION_DRAW_GATE).c_str());

	if (capturing)
	{
		wchar_t text[160] = L"";
		_snwprintf(
			text, 160, L"【改键】%s → 请按新键",
			mr_hotkeys::ActionName(static_cast<mr_hotkeys::Action>(g_RebindAction))
		);
		mr_overlay::SetBottomLine(text);
		return;
	}

	if (g_ToastUntil != 0 && GetTickCount64() < g_ToastUntil)
	{
		mr_overlay::SetBottomLine(g_ToastText);
		return;
	}

	wchar_t text[160] = L"";
	_snwprintf(
		text, 160, L"面板 %s · 改键 %s",
		mr_hotkeys::BindingText(mr_hotkeys::ACTION_PANEL).c_str(),
		mr_hotkeys::BindingText(mr_hotkeys::ACTION_REBIND).c_str()
	);
	mr_overlay::SetBottomLine(text);
}

static void SetToast(const wchar_t* text)
{
	lstrcpynW(g_ToastText, text, 160);
	g_ToastUntil = GetTickCount64() + 2500;
	RefreshPanelText(true);
}

static void PrintStatus()
{
	DbgPrintEx(
		LOG_SEVERITY_INFO,
		"[MapReveal] status: ini=%lld refresh=%lld setArea=%lld update=%lld worldGen=%lld reveals=%lld gate=%d auto=%d",
		g_IniCalls, g_RefreshCalls, g_SetAreaCalls, g_UpdateCalls, g_WorldGenCalls, g_Reveals,
		InterlockedCompareExchange(&g_GatePatched, 0, 0),
		InterlockedCompareExchange(&g_RevealEnabled, 0, 0)
	);

	DbgPrintEx(
		LOG_SEVERITY_INFO,
		"[MapReveal] keys: pulse=%ls draw_gate=%ls status=%ls panel=%ls rebind=%ls | auto=%d delay=%dms hold=%dms",
		mr_hotkeys::BindingText(mr_hotkeys::ACTION_PULSE).c_str(),
		mr_hotkeys::BindingText(mr_hotkeys::ACTION_DRAW_GATE).c_str(),
		mr_hotkeys::BindingText(mr_hotkeys::ACTION_STATUS).c_str(),
		mr_hotkeys::BindingText(mr_hotkeys::ACTION_PANEL).c_str(),
		mr_hotkeys::BindingText(mr_hotkeys::ACTION_REBIND).c_str(),
		mr_hotkeys::AutoPulseEnabled() ? 1 : 0,
		mr_hotkeys::AutoPulseDelayMs(),
		mr_hotkeys::AutoPulseHoldMs()
	);
}

static void HandleHotkeys()
{
	const ULONGLONG now = GetTickCount64();

	// 改键结果 (Esc = 取消)
	mr_hotkeys::KeySpec captured;
	if (mr_hotkeys::TakeCapturedKey(captured))
	{
		if (captured.vk == VK_ESCAPE && !captured.use_scan && captured.mods == 0)
		{
			SetToast(L"已取消改键");
		}
		else
		{
			const mr_hotkeys::Action action = static_cast<mr_hotkeys::Action>(g_RebindAction);
			if (mr_hotkeys::SetBinding(action, captured))
			{
				wchar_t text[160] = L"";
				_snwprintf(
					text, 160, L"已绑定 %s = %s",
					mr_hotkeys::ActionName(action),
					mr_hotkeys::BindingText(action).c_str()
				);
				SetToast(text);
				g_RebindAction = (g_RebindAction + 1) % mr_hotkeys::ACTION_COUNT;
			}
			else
			{
				SetToast(L"这个键已经被其它动作占用");
			}
		}
		mr_hotkeys::CaptureNextKey(false);
	}

	// 进入 / 退出改键模式
	if (mr_hotkeys::TakeAction(mr_hotkeys::ACTION_REBIND))
	{
		if (mr_hotkeys::IsCapturing())
		{
			mr_hotkeys::CaptureNextKey(false);
			SetToast(L"已取消改键");
		}
		else
		{
			mr_overlay::SetVisible(true);	// 改键提示一定要看得见
			mr_hotkeys::CaptureNextKey(true);
			DbgPrintEx(
				LOG_SEVERITY_INFO,
				"[MapReveal] rebind: waiting for a key for %ls",
				mr_hotkeys::ActionName(static_cast<mr_hotkeys::Action>(g_RebindAction))
			);
			RefreshPanelText(true);
		}
	}

	// 手动脉冲一次机制 A
	if (mr_hotkeys::TakeAction(mr_hotkeys::ACTION_PULSE))
	{
		const int hold = mr_hotkeys::AutoPulseHoldMs();
		InterlockedExchange64(&g_ManualPulseUntil, static_cast<LONG64>(now + hold));
		DbgPrintEx(LOG_SEVERITY_INFO, "[MapReveal] manual pulse: mechanism A on for %d ms", hold);
	}

	// 机制 B 开关 (默认常开)
	if (mr_hotkeys::TakeAction(mr_hotkeys::ACTION_DRAW_GATE))
		SetDrawGatePatch(InterlockedCompareExchange(&g_GatePatched, 0, 0) == 0);

	if (mr_hotkeys::TakeAction(mr_hotkeys::ACTION_STATUS))
		PrintStatus();

	if (mr_hotkeys::TakeAction(mr_hotkeys::ACTION_PANEL))
		mr_overlay::ToggleVisible();

	// ini 被手动改过就重新加载
	mr_hotkeys::PollConfigFile();

	// 提示文字到期后恢复成键位提示
	if (g_ToastUntil != 0 && now >= g_ToastUntil)
	{
		g_ToastUntil = 0;
		g_ToastText[0] = 0;
		RefreshPanelText(true);
	}
	RefreshPanelText(false);

	// 机制 A: 进区域后 delay 开启 / hold 后关闭, 外加手动脉冲
	bool enabled = false;
	const wchar_t* reason = L"idle";

	if (mr_hotkeys::AutoPulseEnabled())
	{
		const ULONGLONG zone = static_cast<ULONGLONG>(InterlockedCompareExchange64(&g_ZoneEnterTick, 0, 0));
		if (zone != 0)
		{
			const ULONGLONG elapsed = now - zone;
			const ULONGLONG delay = static_cast<ULONGLONG>(mr_hotkeys::AutoPulseDelayMs());
			const ULONGLONG hold = static_cast<ULONGLONG>(mr_hotkeys::AutoPulseHoldMs());
			if (elapsed >= delay && elapsed < delay + hold)
			{
				enabled = true;
				reason = L"zone pulse";
			}
		}
	}

	if (now < static_cast<ULONGLONG>(InterlockedCompareExchange64(&g_ManualPulseUntil, 0, 0)))
	{
		enabled = true;
		reason = L"manual pulse";
	}

	const LONG wanted = enabled ? 1 : 0;
	if (InterlockedExchange(&g_RevealEnabled, wanted) != wanted)
		DbgPrintEx(LOG_SEVERITY_INFO, "[MapReveal] mechanism A = %s (%ls)", enabled ? "ON" : "OFF", reason);
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

	// 进入 / 生成区域: 记下时间, 机制 A 会在 delay 毫秒后自动脉冲一次 (hold 毫秒后关闭)
	const ULONGLONG zone_tick = GetTickCount64();
	const LONG64 previous_zone = InterlockedExchange64(&g_ZoneEnterTick, static_cast<LONG64>(zone_tick));
	if (mr_hotkeys::AutoPulseEnabled() && (previous_zone == 0 || zone_tick - static_cast<ULONGLONG>(previous_zone) > 1000))
		DbgPrintEx(
			LOG_SEVERITY_INFO,
			"[MapReveal] zone entered: mechanism A pulses in %d ms for %d ms",
			mr_hotkeys::AutoPulseDelayMs(),
			mr_hotkeys::AutoPulseHoldMs()
		);

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

	DbgPrintEx(LOG_SEVERITY_INFO, "[MapReveal] init (draw-gate always on + reveal pulse)");

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

	// 热键 + 设置文件 (与 <Mod>.dll 同目录: mods\aurie\ChroniconMapReveal.ini)
	std::wstring ini_path;
	if (!ModulePath.empty())
	{
		ini_path = ModulePath.wstring();
		const size_t dot = ini_path.find_last_of(L'.');
		const size_t slash = ini_path.find_last_of(L"\\/");
		if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash))
			ini_path.erase(dot);
		ini_path += L".ini";
	}
	mr_hotkeys::Start(ini_path);

	// 右上角状态面板
	mr_overlay::Start(&g_RevealEnabled, &g_GatePatched, OverlayLog);
	RefreshPanelText(true);

	DbgPrintEx(
		LOG_SEVERITY_INFO,
		"[MapReveal] loaded: B always on, A pulses %d ms after a zone entry for %d ms",
		mr_hotkeys::AutoPulseDelayMs(),
		mr_hotkeys::AutoPulseHoldMs()
	);
	PrintStatus();
	return AURIE_SUCCESS;
}
