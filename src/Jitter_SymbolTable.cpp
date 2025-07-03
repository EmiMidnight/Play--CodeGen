#include <assert.h>
#include <algorithm>
#include "Jitter_SymbolTable.h"

using namespace Jitter;

CSymbolTable::CSymbolTable()
{
	m_allocatedSymbols.reserve(512);
	m_symbols.reserve(512);
}

CSymbolTable::~CSymbolTable()
{
	Clear();
}

CSymbolTable::CSymbolTable(CSymbolTable&& other) noexcept
    : m_symbols(std::move(other.m_symbols))
    , m_allocatedSymbols(std::move(other.m_allocatedSymbols))
{
}

CSymbolTable& CSymbolTable::operator=(CSymbolTable&& other) noexcept
{
	if(this != &other)
	{
		Clear();
		m_symbols = std::move(other.m_symbols);
		m_allocatedSymbols = std::move(other.m_allocatedSymbols);
	}
	return *this;
}

CSymbolTable::SymbolSet& CSymbolTable::GetSymbols()
{
	return m_symbols;
}

CSymbolTable::SymbolIterator CSymbolTable::RemoveSymbol(const SymbolIterator& symbolIterator)
{
	return m_symbols.erase(symbolIterator);
}

SymbolPtr CSymbolTable::MakeSymbol(SymbolPtr srcSymbol)
{
	auto symbolIterator(m_symbols.find(srcSymbol));
	if(symbolIterator != m_symbols.end())
	{
		return *symbolIterator;
	}
	
	auto* result = new CSymbol(*srcSymbol);
	m_allocatedSymbols.push_back(result);
	m_symbols.insert(result);
	return result;
}

SymbolPtr CSymbolTable::MakeSymbol(SYM_TYPE type, uint32 valueLow, uint32 valueHigh)
{
	CSymbol symbol(type, valueLow, valueHigh);
	return MakeSymbol(&symbol);
}

void CSymbolTable::Clear()
{
	//m_symbols.clear();
	SymbolSet().swap(m_symbols);
	for(auto* symbol : m_allocatedSymbols)
	{
		delete symbol;
	}
	m_allocatedSymbols.clear();
}
