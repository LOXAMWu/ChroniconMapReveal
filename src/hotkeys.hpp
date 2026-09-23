// ChroniconMapReveal —— 可改键的热键模块 + 设置文件 (ini)
#pragma once

#include <string>

namespace mr_hotkeys
{
	enum Action
	{
		ACTION_PULSE = 0,		// 手动触发一次机制 A
		ACTION_DRAW_GATE = 1,	// 开关机制 B (绘制判定补丁)
		ACTION_STATUS = 2,		// 输出运行状态到日志
		ACTION_PANEL = 3,		// 显示 / 隐藏状态面板
		ACTION_REBIND = 4,		// 进入改键模式
		ACTION_COUNT = 5
	};

	enum Modifier : unsigned
	{
		MR_MOD_CTRL = 1,
		MR_MOD_ALT = 2,
		MR_MOD_SHIFT = 4,
		MR_MOD_WIN = 8
	};

	// 一个键位: 主键 + 需要的修饰键
	struct KeySpec
	{
		int vk = 0;
		int scan = 0;
		bool use_scan = false;	// true = 按扫描码匹配 (小键盘数字键, 与 NumLock 无关)
		unsigned mods = 0;
		bool valid = false;
	};

	// 启动键盘钩子线程并读取 ini (ini_path 为空则不读写配置)
	void Start(const std::wstring& ini_path);

	// ---------------------------------------------------------------- 游戏线程

	// 取一次 "该动作被按下" 的事件 (取过即清空)
	bool TakeAction(Action action);

	// 让钩子把下一次按键当作新键位捕获下来 (enable = false 取消)
	void CaptureNextKey(bool enable);
	bool IsCapturing();
	bool TakeCapturedKey(KeySpec& key);		// 取一次捕获结果 (取过即清空)

	// 改键并写回 ini; 与其它动作冲突 / 非法键位时返回 false
	bool SetBinding(Action action, const KeySpec& key);

	// 每帧调用: 检查 ini 是否被外部修改过 (最多每秒一次)
	void PollConfigFile();

	// 解析键名, 例如 "CTRL+F5" / "Numpad1" / "Space"
	bool ParseKey(const std::wstring& text, KeySpec& key);

	// ---------------------------------------------------------------- 显示 / 日志

	std::wstring BindingText(Action action);	// 面板显示用, 例如 "小键盘1"
	std::wstring BindingName(Action action);	// ini 写法, 例如 "NUMPAD1"
	const wchar_t* ActionName(Action action);

	unsigned BindingVersion();		// 键位变化时递增, 用来决定要不要刷面板

	bool AutoPulseEnabled();
	int AutoPulseDelayMs();
	int AutoPulseHoldMs();
}
