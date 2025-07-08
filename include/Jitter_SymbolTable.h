#pragma once

#include <unordered_set>
#include <vector>
#include "Jitter_Symbol.h"
#include "robin_hood.h"

namespace Jitter
{
	class CSymbolTable final
	{
	public:
		typedef robin_hood::unordered_set<SymbolPtr, SymbolHasher, SymbolComparator> SymbolSet;
		typedef SymbolSet::iterator SymbolIterator;

		CSymbolTable();
		~CSymbolTable();

		CSymbolTable(const CSymbolTable&) = delete;
		CSymbolTable& operator=(const CSymbolTable&) = delete;

		CSymbolTable(CSymbolTable&& other) noexcept;
		CSymbolTable& operator=(CSymbolTable&& other) noexcept;

		SymbolPtr MakeSymbol(SymbolPtr);
		SymbolPtr MakeSymbol(SYM_TYPE, uint32, uint32 = 0);
		SymbolIterator RemoveSymbol(const SymbolIterator&);

		SymbolSet& GetSymbols();

		void Clear();

	private:
		SymbolSet m_symbols;
		std::vector<CSymbol*> m_allocatedSymbols;
	};
}
