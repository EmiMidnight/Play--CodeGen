#include "Jitter.h"
#include <iostream>
#include <set>

#ifdef _DEBUG
//#define DUMP_STATEMENTS
#endif

#ifdef DUMP_STATEMENTS
#include <iostream>
#endif
#include <algorithm>

using namespace Jitter;

void CJitter::AllocateRegisters(BASIC_BLOCK& basicBlock)
{
	auto& symbolTable = basicBlock.symbolTable;

	std::multimap<unsigned int, STATEMENT> loadStatements;
	std::multimap<unsigned int, STATEMENT> spillStatements;

#ifdef DUMP_STATEMENTS
	DumpStatementList(basicBlock.statements);
	std::cout << std::endl;
#endif

	//Register allocation is done per "range". A range is a sequence of instructions
	//that ends with a OP_CALL or with the block's end. We do allocation per range
	//because changes to relative symbols might need to be visible by functions
	//called by the block.

	//There's a downside to this which is that temporaries also get the same treatment
	//and are spilled at the end of a range which might not always be useful.
	//Keep in mind that a temporary can remain live across a OP_CALL.

	auto allocRanges = ComputeAllocationRanges(basicBlock);
	for(const auto& allocRange : allocRanges)
	{
		bool isLastRange = (allocRange.second + 1) == basicBlock.statements.size();

		SymbolRegAllocInfo symbolRegAllocs;
		ComputeLivenessForRange(basicBlock, allocRange, symbolRegAllocs);

		MarkAliasedSymbols(basicBlock, allocRange, symbolRegAllocs);

		AssociateSymbolsToRegisters(symbolRegAllocs);

		//Replace all references to symbols by references to allocated registers
		for(const auto& statementInfo : IndexedStatementList(basicBlock.statements))
		{
			auto& statement(statementInfo.statement);
			const auto& statementIdx(statementInfo.index);
			if(statementIdx < allocRange.first) continue;
			if(statementIdx > allocRange.second) break;

			statement.VisitOperands(
			    [&](SymbolRefPtr& symbolRef, bool) {
				    auto symbol = symbolRef->GetSymbol();
				    auto symbolRegAllocIterator = symbolRegAllocs.find(symbol);
				    if(symbolRegAllocIterator != std::end(symbolRegAllocs))
				    {
					    const auto& symbolRegAlloc = symbolRegAllocIterator->second;
					    if(symbolRegAlloc.registerId != -1)
					    {
						    symbolRef = MakeSymbolRef(
						        symbolTable.MakeSymbol(symbolRegAlloc.registerType, symbolRegAlloc.registerId));
					    }
				    }
			    });
		}

		//Prepare load and spills
		for(const auto& symbolRegAllocPair : symbolRegAllocs)
		{
			const auto& symbol = symbolRegAllocPair.first;
			const auto& symbolRegAlloc = symbolRegAllocPair.second;

			//Check if it's actually allocated
			if(symbolRegAlloc.registerId == -1) continue;

			//firstUse == -1 means it is written to but never used afterwards in this block

			//Do we need to load register at the beginning?
			//If symbol is read and we use this symbol before we define it, so we need to load it first
			if((symbolRegAlloc.firstUse != -1) && (symbolRegAlloc.firstUse <= symbolRegAlloc.firstDef))
			{
				STATEMENT statement;
				statement.op = OP_MOV;
				statement.dst = MakeSymbolRef(
				    symbolTable.MakeSymbol(symbolRegAlloc.registerType, symbolRegAlloc.registerId));
				statement.src1 = MakeSymbolRef(symbol);

				loadStatements.insert(std::make_pair(allocRange.first, statement));
			}

			//If symbol is defined, we need to save it at the end
			//Exception: Temporaries can be discarded if we're in the last range of the block
			bool deadTemporary = symbol->IsTemporary() && isLastRange;
			if(!deadTemporary && (symbolRegAlloc.firstDef != -1))
			{
				STATEMENT statement;
				statement.op = OP_MOV;
				statement.dst = MakeSymbolRef(symbol);
				statement.src1 = MakeSymbolRef(
				    symbolTable.MakeSymbol(symbolRegAlloc.registerType, symbolRegAlloc.registerId));

				spillStatements.insert(std::make_pair(allocRange.second, statement));
			}
		}
	}

#ifdef DUMP_STATEMENTS
	DumpStatementList(basicBlock.statements);
	std::cout << std::endl;
#endif

	std::map<unsigned int, StatementList::const_iterator> loadPoints;
	std::map<unsigned int, StatementList::const_iterator> spillPoints;

	robin_hood::unordered_set<unsigned int> loadStatementIndices;
	robin_hood::unordered_set<unsigned int> spillStatementIndices;

	for(const auto& stmt : loadStatements)
	{
		loadStatementIndices.insert(stmt.first);
	}
	for(const auto& stmt : spillStatements)
	{
		spillStatementIndices.insert(stmt.first);
	}

	for(const auto& statementInfo : ConstIndexedStatementList(basicBlock.statements))
	{
		const auto& statementIdx(statementInfo.index);
		if(loadStatementIndices.count(statementIdx))
		{
			loadPoints.emplace(statementIdx, statementInfo.iterator);
		}
	}

	for(const auto& statementInfo : ConstIndexedStatementList(basicBlock.statements))
	{
		const auto& statementIdx(statementInfo.index);
		if(spillStatementIndices.count(statementIdx))
		{
			const auto& statement = statementInfo.statement;
			auto statementIterator = statementInfo.iterator;
			if(
			    (statement.op != OP_CONDJMP) &&
			    (statement.op != OP_JMP) &&
			    (statement.op != OP_CALL) &&
			    (statement.op != OP_EXTERNJMP) &&
			    (statement.op != OP_EXTERNJMP_DYN))
			{
				statementIterator++;
			}
			spillPoints.emplace(statementIdx, statementIterator);
		}
	}

	//Loads
	for(const auto& loadPoint : loadPoints)
	{
		unsigned int statementIndex = loadPoint.first;
		for(auto statementIterator = loadStatements.lower_bound(statementIndex);
		    statementIterator != loadStatements.upper_bound(statementIndex);
		    statementIterator++)
		{
			const auto& statement(statementIterator->second);
			basicBlock.statements.insert(loadPoint.second, statement);
		}
	}

	//Spills
	for(const auto& spillPoint : spillPoints)
	{
		unsigned int statementIndex = spillPoint.first;
		for(auto statementIterator = spillStatements.lower_bound(statementIndex);
		    statementIterator != spillStatements.upper_bound(statementIndex);
		    statementIterator++)
		{
			const auto& statement(statementIterator->second);
			basicBlock.statements.insert(spillPoint.second, statement);
		}
	}

#ifdef DUMP_STATEMENTS
	DumpStatementList(basicBlock.statements);
	std::cout << std::endl;
#endif
}

// void CJitter::AssociateSymbolsToRegisters(SymbolRegAllocInfo& symbolRegAllocs) const
// {
// 	//Some notes:
// 	//- MD and FP registers are lumped together since MD registers are used for both
// 	//  MD and FP operations on all of our target platforms.

// 	std::multimap<SYM_TYPE, unsigned int> availableRegisters;
// 	{
// 		unsigned int regCount = m_codeGen->GetAvailableRegisterCount();
// 		for(unsigned int i = 0; i < regCount; i++)
// 		{
// 			availableRegisters.insert(std::make_pair(SYM_REGISTER, i));
// 		}
// 	}

// 	{
// 		unsigned int regCount = m_codeGen->GetAvailableMdRegisterCount();
// 		for(unsigned int i = 0; i < regCount; i++)
// 		{
// 			availableRegisters.insert(std::make_pair(SYM_REGISTER128, i));
// 		}
// 	}

// 	auto isRegisterAllocatable =
// 	    [](SYM_TYPE symbolType) {
// 		    return (symbolType == SYM_RELATIVE) || (symbolType == SYM_TEMPORARY) ||
// 		           (symbolType == SYM_REL_REFERENCE) || (symbolType == SYM_TMP_REFERENCE) ||
// 		           (symbolType == SYM_FP_RELATIVE32) || (symbolType == SYM_FP_TEMPORARY32) ||
// 		           (symbolType == SYM_RELATIVE128) || (symbolType == SYM_TEMPORARY128);
// 	    };

// 	//Sort symbols by usage count
// 	std::vector<SymbolRegAllocInfo::value_type*> sortedSymbols;
// 	sortedSymbols.reserve(symbolRegAllocs.size());
// 	for(auto& symbolRegAllocPair : symbolRegAllocs)
// 	{
// 		const auto& symbol(symbolRegAllocPair.first);
// 		const auto& symbolRegAlloc(symbolRegAllocPair.second);
// 		if(!isRegisterAllocatable(symbol->m_type)) continue;
// 		if(symbolRegAlloc.aliased) continue;
// 		sortedSymbols.push_back(&symbolRegAllocPair);
// 	}

// 	std::sort(sortedSymbols.begin(), sortedSymbols.end(),
// 	          [](SymbolRegAllocInfo::value_type* symbolRegAllocPair1, SymbolRegAllocInfo::value_type* symbolRegAllocPair2) {
// 		          const auto& symbol1(symbolRegAllocPair1->first);
// 		          const auto& symbol2(symbolRegAllocPair2->first);
// 		          const auto& symbolRegAlloc1(symbolRegAllocPair1->second);
// 		          const auto& symbolRegAlloc2(symbolRegAllocPair2->second);
// 		          if(symbolRegAlloc1.useCount == symbolRegAlloc2.useCount)
// 		          {
// 			          if(symbol1->m_type == symbol2->m_type)
// 			          {
// 				          return symbol1->m_valueLow > symbol2->m_valueLow;
// 			          }
// 			          else
// 			          {
// 				          return symbol1->m_type > symbol2->m_type;
// 			          }
// 		          }
// 		          else
// 		          {
// 			          return symbolRegAlloc1.useCount > symbolRegAlloc2.useCount;
// 		          }
// 	          });

// 	for(auto& symbolRegAllocPair : sortedSymbols)
// 	{
// 		if(availableRegisters.empty()) break;

// 		const auto& symbol = symbolRegAllocPair->first;
// 		auto& symbolRegAlloc = symbolRegAllocPair->second;

// 		//Find suitable register for this symbol
// 		auto registerIterator = std::end(availableRegisters);
// 		auto registerIteratorEnd = std::end(availableRegisters);
// 		auto registerSymbolType = SYM_REGISTER;
// 		if((symbol->m_type == SYM_RELATIVE) || (symbol->m_type == SYM_TEMPORARY))
// 		{
// 			registerIterator = availableRegisters.lower_bound(SYM_REGISTER);
// 			registerIteratorEnd = availableRegisters.upper_bound(SYM_REGISTER);
// 			registerSymbolType = SYM_REGISTER;
// 		}
// 		else if((symbol->m_type == SYM_REL_REFERENCE) || (symbol->m_type == SYM_TMP_REFERENCE))
// 		{
// 			registerIterator = availableRegisters.lower_bound(SYM_REGISTER);
// 			registerIteratorEnd = availableRegisters.upper_bound(SYM_REGISTER);
// 			registerSymbolType = SYM_REG_REFERENCE;
// 		}
// 		else if((symbol->m_type == SYM_FP_RELATIVE32) || (symbol->m_type == SYM_FP_TEMPORARY32))
// 		{
// 			registerIterator = availableRegisters.lower_bound(SYM_REGISTER128);
// 			registerIteratorEnd = availableRegisters.upper_bound(SYM_REGISTER128);
// 			registerSymbolType = SYM_FP_REGISTER32;
// 		}
// 		else if((symbol->m_type == SYM_RELATIVE128) || (symbol->m_type == SYM_TEMPORARY128))
// 		{
// 			registerIterator = availableRegisters.lower_bound(SYM_REGISTER128);
// 			registerIteratorEnd = availableRegisters.upper_bound(SYM_REGISTER128);
// 			registerSymbolType = SYM_REGISTER128;
// 		}
// 		if(registerIterator != registerIteratorEnd)
// 		{
// 			symbolRegAlloc.registerType = registerSymbolType;
// 			symbolRegAlloc.registerId = registerIterator->second;
// 			availableRegisters.erase(registerIterator);
// 		}
// 	}
// }

void CJitter::AssociateSymbolsToRegisters(SymbolRegAllocInfo& symbolRegAllocs) const
{
	std::vector<unsigned int> availableGpRegisters;
	std::vector<unsigned int> availableMdRegisters;

	{
		unsigned int regCount = m_codeGen->GetAvailableRegisterCount();
		availableGpRegisters.reserve(regCount);
		for(unsigned int i = 0; i < regCount; i++)
		{
			availableGpRegisters.push_back(i);
		}
	}

	{
		unsigned int regCount = m_codeGen->GetAvailableMdRegisterCount();
		availableMdRegisters.reserve(regCount);
		for(unsigned int i = 0; i < regCount; i++)
		{
			availableMdRegisters.push_back(i);
		}
	}

	auto isRegisterAllocatable =
	    [](SYM_TYPE symbolType) {
		    return (symbolType == SYM_RELATIVE) || (symbolType == SYM_TEMPORARY) ||
		           (symbolType == SYM_REL_REFERENCE) || (symbolType == SYM_TMP_REFERENCE) ||
		           (symbolType == SYM_FP_RELATIVE32) || (symbolType == SYM_FP_TEMPORARY32) ||
		           (symbolType == SYM_RELATIVE128) || (symbolType == SYM_TEMPORARY128);
	    };

	std::vector<SymbolRegAllocInfo::value_type*> sortedSymbols;
	sortedSymbols.reserve(symbolRegAllocs.size());
	for(auto& symbolRegAllocPair : symbolRegAllocs)
	{
		const auto& symbol(symbolRegAllocPair.first);
		const auto& symbolRegAlloc(symbolRegAllocPair.second);
		if(!isRegisterAllocatable(symbol->m_type)) continue;
		if(symbolRegAlloc.aliased) continue;
		sortedSymbols.push_back(&symbolRegAllocPair);
	}

	std::sort(sortedSymbols.begin(), sortedSymbols.end(),
	          [](SymbolRegAllocInfo::value_type* symbolRegAllocPair1, SymbolRegAllocInfo::value_type* symbolRegAllocPair2) {
		          const auto& symbol1(symbolRegAllocPair1->first);
		          const auto& symbol2(symbolRegAllocPair2->first);
		          const auto& symbolRegAlloc1(symbolRegAllocPair1->second);
		          const auto& symbolRegAlloc2(symbolRegAllocPair2->second);
		          if(symbolRegAlloc1.useCount == symbolRegAlloc2.useCount)
		          {
			          if(symbol1->m_type == symbol2->m_type)
			          {
				          return symbol1->m_valueLow > symbol2->m_valueLow;
			          }
			          else
			          {
				          return symbol1->m_type > symbol2->m_type;
			          }
		          }
		          else
		          {
			          return symbolRegAlloc1.useCount > symbolRegAlloc2.useCount;
		          }
	          });

	for(auto& symbolRegAllocPair : sortedSymbols)
	{
		const auto& symbol = symbolRegAllocPair->first;
		auto& symbolRegAlloc = symbolRegAllocPair->second;

		std::vector<unsigned int>* availableRegisters = nullptr;
		SYM_TYPE registerSymbolType = SYM_REGISTER;

		if((symbol->m_type == SYM_RELATIVE) || (symbol->m_type == SYM_TEMPORARY))
		{
			availableRegisters = &availableGpRegisters;
			registerSymbolType = SYM_REGISTER;
		}
		else if((symbol->m_type == SYM_REL_REFERENCE) || (symbol->m_type == SYM_TMP_REFERENCE))
		{
			availableRegisters = &availableGpRegisters;
			registerSymbolType = SYM_REG_REFERENCE;
		}
		else if((symbol->m_type == SYM_FP_RELATIVE32) || (symbol->m_type == SYM_FP_TEMPORARY32))
		{
			availableRegisters = &availableMdRegisters;
			registerSymbolType = SYM_FP_REGISTER32;
		}
		else if((symbol->m_type == SYM_RELATIVE128) || (symbol->m_type == SYM_TEMPORARY128))
		{
			availableRegisters = &availableMdRegisters;
			registerSymbolType = SYM_REGISTER128;
		}

		if(availableRegisters && !availableRegisters->empty())
		{
			symbolRegAlloc.registerType = registerSymbolType;
			symbolRegAlloc.registerId = availableRegisters->back();
			availableRegisters->pop_back();
		}
	}
}

CJitter::AllocationRangeArray CJitter::ComputeAllocationRanges(const BASIC_BLOCK& basicBlock)
{
	AllocationRangeArray result;
	result.reserve(basicBlock.statements.size() / 2 + 1); //Reserve enough space for ranges
	unsigned int currentStart = 0;
	for(const auto& statementInfo : ConstIndexedStatementList(basicBlock.statements))
	{
		const auto& statement(statementInfo.statement);
		const auto& statementIdx(statementInfo.index);
		if(statement.op == OP_CALL)
		{
			//Gotta split here
			result.push_back(std::make_pair(currentStart, statementIdx));
			currentStart = statementIdx + 1;
		}
	}
	result.push_back(std::make_pair(currentStart, basicBlock.statements.size() - 1));
	return result;
}

void CJitter::ComputeLivenessForRange(const BASIC_BLOCK& basicBlock, const AllocationRange& allocRange, SymbolRegAllocInfo& symbolRegAllocs) const
{
	for(const auto& statementInfo : ConstIndexedStatementList(basicBlock.statements))
	{
		const auto& statement(statementInfo.statement);
		unsigned int statementIdx(statementInfo.index);
		if(statementIdx < allocRange.first) continue;
		if(statementIdx > allocRange.second) continue;

		statement.VisitDestination(
		    [&](const SymbolRefPtr& symbolRef, bool) {
			    auto symbol(symbolRef->GetSymbol());
			    auto& symbolRegAlloc = symbolRegAllocs[symbol];
			    symbolRegAlloc.useCount++;
			    if(symbolRegAlloc.firstDef == -1)
			    {
				    symbolRegAlloc.firstDef = statementIdx;
			    }
			    if((symbolRegAlloc.lastDef == -1) || (statementIdx > symbolRegAlloc.lastDef))
			    {
				    symbolRegAlloc.lastDef = statementIdx;
			    }
		    });

		statement.VisitSources(
		    [&](const SymbolRefPtr& symbolRef, bool) {
			    auto symbol(symbolRef->GetSymbol());
			    auto& symbolRegAlloc = symbolRegAllocs[symbol];
			    symbolRegAlloc.useCount++;
			    if(symbolRegAlloc.firstUse == -1)
			    {
				    symbolRegAlloc.firstUse = statementIdx;
			    }
			    if((symbolRegAlloc.lastUse == -1) || (statementIdx > symbolRegAlloc.lastUse))
			    {
				    symbolRegAlloc.lastUse = statementIdx;
			    }
		    });
	}
}

void CJitter::MarkAliasedSymbols(const BASIC_BLOCK& basicBlock, const AllocationRange& allocRange, SymbolRegAllocInfo& symbolRegAllocs) const
{
	for(const auto& statementInfo : ConstIndexedStatementList(basicBlock.statements))
	{
		auto& statement(statementInfo.statement);
		const auto& statementIdx(statementInfo.index);
		if(statementIdx < allocRange.first) continue;
		if(statementIdx > allocRange.second) break;
		if(statement.op == OP_PARAM_RET)
		{
			//This symbol will end up being written to by the callee, thus will be aliased
			auto& symbolRegAlloc = symbolRegAllocs[statement.src1->GetSymbol()];
			symbolRegAlloc.aliased = true;
		}
		for(auto& symbolRegAlloc : symbolRegAllocs)
		{
			if(symbolRegAlloc.second.aliased) continue;
			auto testedSymbol = symbolRegAlloc.first;
			statement.VisitOperands(
			    [&](const SymbolRefPtr& symbolRef, bool) {
				    auto symbol = symbolRef->GetSymbol();
				    if(symbol->Equals(testedSymbol)) return;
				    if(symbol->Aliases(testedSymbol))
				    {
					    symbolRegAlloc.second.aliased = true;
				    }
			    });
		}
	}
}
