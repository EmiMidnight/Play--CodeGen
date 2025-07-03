#pragma once

#include "Jitter_Symbol.h"
#include "maybe_unused.h"

namespace Jitter
{
	class CSymbolRef final
	{
	public:
		static constexpr int UNVERSIONED = -1;

		CSymbolRef(SymbolPtr symbol, int version = UNVERSIONED)
		    : m_symbol(symbol)
		    , m_version(version)
		{
		}

		SymbolPtr GetSymbol() const
		{
			return m_symbol;
		}

		std::string ToString() const
		{
			return GetSymbol()->ToString();
		}

		bool Equals(CSymbolRef* symbolRef) const
		{
			if(!symbolRef) return false;
			return (m_version == symbolRef->m_version) && GetSymbol()->Equals(symbolRef->GetSymbol());
		}

		int GetVersion() const
		{
			return m_version;
		}

		bool IsVersioned() const
		{
			return m_version != UNVERSIONED;
		}

	private:
		SymbolPtr m_symbol;
		int m_version = UNVERSIONED;
	};

	typedef CSymbolRef* SymbolRefPtr;

	FRAMEWORK_MAYBE_UNUSED
	static CSymbol* dynamic_symbolref_cast(SYM_TYPE type, SymbolRefPtr symbolRef)
	{
		if(!symbolRef) return nullptr;
		auto result = symbolRef->GetSymbol();
		if(!result) return nullptr;
		if(result->m_type != type) return nullptr;
		return result;
	}
}
