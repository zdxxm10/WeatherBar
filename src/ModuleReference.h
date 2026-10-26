#pragma once
#include <windows.h>

extern void LockModule(BOOL lock);

// 异步任务持有引用，避免 Explorer 在后台任务结束前卸载 DLL。
class ModuleReference {
public:
	ModuleReference() { LockModule(TRUE); }
	~ModuleReference() { LockModule(FALSE); }
	ModuleReference(const ModuleReference&) = delete;
	ModuleReference& operator=(const ModuleReference&) = delete;
};
