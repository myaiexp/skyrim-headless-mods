#include "papyrus_call.h"

#include <type_traits>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "trace.h"

namespace
{
	// Hand the VM a runtime-typed argument list. CommonLib's MakeFunctionArguments packs a
	// compile-time tuple; a command's args only exist at run time, so this implements the same
	// one-virtual interface over a vector instead. The VM copies the slots into the new stack
	// frame synchronously inside DispatchStaticCall and never keeps the pointer — a stack
	// instance is safe (CommonLib's own event senders delete theirs right after the dispatch).
	class JsonArguments : public RE::BSScript::IFunctionArguments
	{
	public:
		explicit JsonArguments(const std::vector<engine::PapyrusArg>& a_args) :
			_args(a_args)
		{}

		bool operator()(RE::BSScrapArray<RE::BSScript::Variable>& a_dst) const override
		{
			a_dst.resize(static_cast<std::uint32_t>(_args.size()));
			for (std::size_t i = 0; i < _args.size(); ++i) {
				auto& slot = a_dst[static_cast<std::uint32_t>(i)];
				std::visit([&slot](const auto& v) {
					using T = std::decay_t<decltype(v)>;
					if constexpr (std::is_same_v<T, std::int32_t>) {
						slot.SetSInt(v);
					} else if constexpr (std::is_same_v<T, float>) {
						slot.SetFloat(v);
					} else if constexpr (std::is_same_v<T, bool>) {
						slot.SetBool(v);
					} else {
						slot.SetString(v);
					}
				}, _args[i]);
			}
			return true;
		}

	private:
		const std::vector<engine::PapyrusArg>& _args;
	};

	// The completion side. Owned by the VM through the intrusive smart pointer it is handed and
	// deleted when the stack releases it, so it carries copies of its strings, never references
	// into the command closure. Runs on the VM's thread: it touches nothing but those strings,
	// the result Variable it is given, and the mutex-guarded trace writer.
	class CompletionFunctor : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		CompletionFunctor(std::string a_class, std::string a_function) :
			_class(std::move(a_class)),
			_function(std::move(a_function))
		{}

		void operator()(RE::BSScript::Variable a_result) override
		{
			trace::json result;  // stays null for None, objects and arrays — scalars only
			if (a_result.IsBool()) {
				result = a_result.GetBool();
			} else if (a_result.IsInt()) {
				result = a_result.GetSInt();
			} else if (a_result.IsFloat()) {
				result = a_result.GetFloat();
			} else if (a_result.IsString()) {
				result = std::string(a_result.GetString());
			}
			trace::Write(trace::json{ { "src", "papyrus-call" }, { "class", _class },
				{ "function", _function }, { "ok", true }, { "result", std::move(result) } });
		}

		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

	private:
		std::string _class;
		std::string _function;
	};
}

engine::PapyrusCallResult engine::DispatchPapyrusStatic(const std::string& a_class,
	const std::string& a_function, const std::vector<PapyrusArg>& a_args)
{
	auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
	if (!vm) {
		return PapyrusCallResult::kNoVM;
	}

	JsonArguments                                              args(a_args);
	RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{
		new CompletionFunctor(a_class, a_function)
	};
	const bool queued = vm->DispatchStaticCall(RE::BSFixedString(a_class), RE::BSFixedString(a_function),
		&args, callback);
	SKSE::log::info("SkytestProbe: papyrus-call {}.{} ({} args) -> {}", a_class, a_function,
		a_args.size(), queued ? "queued" : "refused");
	return queued ? PapyrusCallResult::kOk : PapyrusCallResult::kRefused;
}
