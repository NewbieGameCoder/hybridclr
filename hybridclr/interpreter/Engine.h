#pragma once

#include <stack>

#include "../CommonDef.h"

#include "gc/GarbageCollector.h"
#include "vm/String.h"
#include "vm/Exception.h"
#include "vm/StackTrace.h"

#include "../metadata/MetadataUtil.h"
#include "../Config.h"

#include "InterpreterDefs.h"
#include "MemoryUtil.h"
#include "MethodBridge.h"


//#if DEBUG
//#define PUSH_STACK_FRAME(method) do { \
//	Il2CppStackFrameInfo stackFrameInfo = { method, (uintptr_t)method->methodPointer }; \
//	il2cpp::vm::StackTrace::PushFrame(stackFrameInfo); \
//} while(0)
//
//#define POP_STACK_FRAME() do { il2cpp::vm::StackTrace::PopFrame(); } while(0)
//
//#else 
#define PUSH_STACK_FRAME(method)
#define POP_STACK_FRAME() 
//#endif

namespace hybridclr
{
namespace interpreter
{

	class MachineState
	{
	public:
		MachineState()
		{
			Config& hc = Config::GetIns();
			_stackSize = -1;
			_stackBase = nullptr;
			_stackTopIdx = 0;
			_opCodesStartIndex = 0;
			_localPoolBottomIdx = -1;

			_frameBase = nullptr;
			_frameCount = -1;
			_frameTopIdx = 0;

			_exceptionFlowBase = nullptr;
			_exceptionFlowCount = -1;
			_exceptionFlowTopIdx = 0;
		}

		~MachineState()
		{
			if (_stackBase)
			{
				//il2cpp::gc::GarbageCollector::FreeFixed(_stackBase);
				il2cpp::gc::GarbageCollector::UnregisterDynamicRoot(this);
				IL2CPP_FREE(_stackBase);
			}
			if (_frameBase)
			{
				IL2CPP_FREE(_frameBase);
			}
			if (_exceptionFlowBase)
			{
				IL2CPP_FREE(_exceptionFlowBase);
			}
		}

		static std::pair<char*, size_t> GetGCRootData(void* root)
		{
			MachineState* machineState = (MachineState*)root;
			if (machineState->_stackBase && machineState->_stackTopIdx > 0)
			{
				return std::make_pair((char*)machineState->_stackBase, machineState->_stackTopIdx * sizeof(StackObject));
			}
			else
			{
				return std::make_pair(nullptr, 0);
			}
		}

		StackObject* AllocArgments(int32_t argCount)
		{
			return AllocStackSlot(argCount);
		}

		StackObject* GetStackBasePtr() const
		{
			return _stackBase;
		}

		int32_t GetopCodesStartIndex() const
		{
			return _opCodesStartIndex;
		}

		int32_t GetStackTop() const
		{
			return _stackTopIdx;
		}

		StackObject* AllocStackSlot(int32_t slotNum)
		{
			if (_stackTopIdx + slotNum > _localPoolBottomIdx)
			{
				if (!_stackBase)
				{
					InitEvalStack();
				}
				if (_stackTopIdx + slotNum > _localPoolBottomIdx)
				{
					il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetStackOverflowException("AllocStackSlot"));
				}
			}
			StackObject* dataPtr = _stackBase + _stackTopIdx;
			_stackTopIdx += slotNum;
#if DEBUG
			std::memset(dataPtr, 0xEA, slotNum * sizeof(StackObject));
#endif
			return dataPtr;
		}

		void* AllocLocalloc(size_t size)
		{
			IL2CPP_ASSERT(size % 8 == 0);
			int32_t slotNum = (int32_t)(size / 8);
			IL2CPP_ASSERT(slotNum > 0);
			if (_stackTopIdx + slotNum > _localPoolBottomIdx)
			{
				if (!_stackBase)
				{
					InitEvalStack();
				}
				if (_stackTopIdx + slotNum > _localPoolBottomIdx)
				{
					il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetStackOverflowException("AllocLocalloc"));
				}
			}
			_localPoolBottomIdx -= slotNum;
			return _stackBase + _localPoolBottomIdx;
		}

		void SetStackTop(int32_t oldTop)
		{
			_stackTopIdx = oldTop;
		}

		uint32_t GetFrameTopIdx() const
		{
			return _frameTopIdx;
		}

		int32_t GetLocalPoolBottomIdx() const
		{
			return _localPoolBottomIdx;
		}

		void SetLocalPoolBottomIdx(int32_t idx)
		{
			_localPoolBottomIdx = idx;
		}

		InterpFrame* PushFrame()
		{
			if (_frameTopIdx >= _frameCount)
			{
				if (!_frameBase)
				{
					InitFrames();
				}
				else
				{
					il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetStackOverflowException("AllocFrame"));
				}
			}

			return _frameBase + _frameTopIdx++;
		}

		void PopFrame()
		{
			IL2CPP_ASSERT(_frameTopIdx > 0);
			InterpFrame* frame = GetTopFrame();
			--_frameTopIdx;

			_opCodesStartIndex = frame->oldOpCodesStartIndex;
		}

		void PopFrameN(int32_t count)
		{
			IL2CPP_ASSERT(count > 0 && _frameTopIdx >= count);
			_frameTopIdx -= count;
		}

		InterpFrame* GetTopFrame() const
		{
			if (_frameTopIdx > 0)
			{
				return _frameBase + _frameTopIdx - 1;
			}
			else
			{
				return nullptr;
			}
		}

		ExceptionFlowInfo* AllocExceptionFlow(int32_t count)
		{
			if (_exceptionFlowTopIdx + count >= _exceptionFlowCount)
			{
				if (!_exceptionFlowBase)
				{
					InitExceptionFlows();
				}
				if (_exceptionFlowTopIdx + count >= _exceptionFlowCount)
				{
					il2cpp::vm::Exception::Raise(il2cpp::vm::Exception::GetExecutionEngineException("AllocExceptionFlowZero"));
				}
			}
			ExceptionFlowInfo* efi = _exceptionFlowBase + _exceptionFlowTopIdx;
			_exceptionFlowTopIdx += count;
			return efi;
		}

		uint32_t GetExceptionFlowTopIdx() const
		{
			return _exceptionFlowTopIdx;
		}

		void SetExceptionFlowTopIdx(uint32_t exTopIdx)
		{
			_exceptionFlowTopIdx = exTopIdx;
		}

		void SetExceptionFlowTop(ExceptionFlowInfo* top)
		{
			_exceptionFlowTopIdx = (int32_t)(top - _exceptionFlowBase);
			IL2CPP_ASSERT(_exceptionFlowTopIdx >= 0 && _exceptionFlowTopIdx <= _exceptionFlowCount);
		}

		void PushExecutingImage(const Il2CppImage* image)
		{
			_executingImageStack.push(image);
		}

		void PopExecutingImage()
		{
			_executingImageStack.pop();
		}

		const Il2CppImage* GetTopExecutingImage() const
		{
			if (_executingImageStack.empty())
			{
				return nullptr;
			}
			else
			{
				return _executingImageStack.top();
			}
		}

		void CollectFrames(il2cpp::vm::StackFrames* stackFrames)
		{
			for (int32_t i = 0; i < _frameTopIdx; i++)
			{
				InterpFrame* frame = _frameBase + i;
				const MethodInfo* method = frame->method->method;
				Il2CppStackFrameInfo stackFrameInfo = {
					method
#if HYBRIDCLR_UNITY_2020_OR_NEW
					, (uintptr_t)method->methodPointer
#endif
				};
				stackFrames->push_back(stackFrameInfo);
			}
		}

		void CollectOpCodes(Il2CppException* ex)
		{
			if (ex == nullptr || _ilOpcodesDeque.size() == 0)
			{
				return;
			}

			std::string opCodesStr = GetExecutedOpCodeInfo();
			if (opCodesStr.empty() == false)
			{
				std::string msg = "";
				if (ex->message != nullptr && ex->message->length > 0)
				{
					msg = il2cpp::utils::StringUtils::Utf16ToUtf8(ex->message->chars);
					msg.append("\n");
				}
				msg.append("opCodes: ");
				msg.append(opCodesStr);
				ex->message = il2cpp::vm::String::New(msg.c_str());
			}
			_opCodesStartIndex = 0;
			_ilOpcodesDeque.clear();
		}

		void PushOpcode(const std::string& opcodeStr)
		{
			if (_opCodesStartIndex >= _ilOpcodesDeque.size())
			{
				_ilOpcodesDeque.push_back(opcodeStr);
			}
			else
			{
				_ilOpcodesDeque[_opCodesStartIndex] = opcodeStr;
			}
			_opCodesStartIndex++;
		}

	private:

		void InitEvalStack()
		{
			Config& hc = Config::GetIns();
			_stackSize = (int32_t)hc.GetInterpreterThreadObjectStackSize();
			_stackBase = (StackObject*)IL2CPP_MALLOC_ZERO(hc.GetInterpreterThreadObjectStackSize() * sizeof(StackObject));
			_stackTopIdx = 0;
			_localPoolBottomIdx = _stackSize;
			il2cpp::gc::GarbageCollector::RegisterDynamicRoot(this, GetGCRootData);
		}

		void InitFrames()
		{
			Config& hc = Config::GetIns();
			_frameBase = (InterpFrame*)IL2CPP_CALLOC(hc.GetInterpreterThreadFrameStackSize(), sizeof(InterpFrame));
			_frameCount = (int32_t)hc.GetInterpreterThreadFrameStackSize();
			_frameTopIdx = 0;
		}

		void InitExceptionFlows()
		{
			Config& hc = Config::GetIns();
			_exceptionFlowBase = (ExceptionFlowInfo*)IL2CPP_CALLOC(hc.GetInterpreterThreadExceptionFlowSize(), sizeof(ExceptionFlowInfo));
			_exceptionFlowCount = (int32_t)hc.GetInterpreterThreadExceptionFlowSize();
			_exceptionFlowTopIdx = 0;
		}

		std::string GetExecutedOpCodeInfo()
		{
			std::string name;

			InterpFrame* frame = GetTopFrame();
			int32_t startIndex = frame->oldOpCodesStartIndex;
			if (_opCodesStartIndex - startIndex > MaxOpCodesCount)
			{
				startIndex = _opCodesStartIndex - MaxOpCodesCount;
			}

			for (int32_t j = startIndex; j < _opCodesStartIndex; j++)
			{
				name.append(_ilOpcodesDeque[j]);
				if (j < _opCodesStartIndex - 1)
				{
					name.append("->");
				}
			}

			return name;
		}

		StackObject* _stackBase;
		int32_t _stackSize;
		int32_t _stackTopIdx;
		int32_t _localPoolBottomIdx;

		InterpFrame* _frameBase;
		int32_t _frameTopIdx;
		int32_t _frameCount;

		ExceptionFlowInfo* _exceptionFlowBase;
		int32_t _exceptionFlowTopIdx;
		int32_t _exceptionFlowCount;

		const int MaxOpCodesCount = 30;
		int32_t _opCodesStartIndex;
		std::vector<std::string> _ilOpcodesDeque;
		std::stack<const Il2CppImage*> _executingImageStack;
	};

	class ExecutingInterpImageScope
	{
	public:
		ExecutingInterpImageScope(MachineState& state, const Il2CppImage* image) : _state(state)
		{
			_state.PushExecutingImage(image);
		}

		~ExecutingInterpImageScope()
		{
			_state.PopExecutingImage();
		}
		
	private:
		MachineState& _state;
	};

	class InterpFrameGroup
	{
	public:
		InterpFrameGroup(MachineState& ms) : _machineState(ms), _stackBaseIdx(ms.GetStackTop()), _frameBaseIdx(ms.GetFrameTopIdx())
		{

		}

		void CleanUpFrames()
		{
			IL2CPP_ASSERT(_machineState.GetFrameTopIdx() >= _frameBaseIdx);
			uint32_t n = _machineState.GetFrameTopIdx() - _frameBaseIdx;
			if (n > 0)
			{
				for (uint32_t i = 0; i < n; i++)
				{
					LeaveFrame();
				}
			}
		}

		InterpFrame* EnterFrameFromInterpreter(const InterpMethodInfo* imi, StackObject* argBase);


		InterpFrame* EnterFrameFromNative(const InterpMethodInfo* imi, StackObject* argBase);

		InterpFrame* LeaveFrame();

		void* AllocLoc(size_t originSize, bool fillZero)
		{
			if (originSize == 0)
			{
				return nullptr;
			}
			size_t size = (originSize + 7) & ~(size_t)7;
			void* data = _machineState.AllocLocalloc(size);
			if (fillZero)
			{
				std::memset(data, 0, size);
			}
			return data;
 		}

		size_t GetFrameCount() const { return _machineState.GetFrameTopIdx() - _frameBaseIdx; }
	private:
		MachineState& _machineState;
		int32_t _stackBaseIdx;
		uint32_t _frameBaseIdx;
	};

	class StackObjectAllocScope
	{
	private:
		MachineState& _state;
		const int32_t _originStackTop;
		const int32_t _count;
		StackObject* _data;
	public:
		StackObjectAllocScope(MachineState& state, int32_t count) : _state(state), _count(count), _originStackTop(_state.GetStackTop())
		{
			_data = state.AllocStackSlot(count);
		}

		~StackObjectAllocScope()
		{
			IL2CPP_ASSERT(_state.GetStackTop() > _originStackTop);
			_state.SetStackTop(_originStackTop);
		}

		StackObject* GetData() const { return _data; }
	};
}
}